import type { EventStore, Status } from './types';

export type Command = { cmd: string } & Record<string, unknown>;
export type Reply = ({ ok: true } & Record<string, unknown>) | { ok: false; error: string };

export interface PreviewFrame {
  id: string;
  keyframe: boolean;
  ptsUs: number;
  data: Uint8Array;
}

export interface AngleFrame {
  index: number;
  width: number;
  height: number;
  bgra: Uint8Array;
}

export interface SurfaceRequest {
  id: string;
  w: number;
  h: number;
}

export interface SurfaceReply {
  pid: number;
  surfaces: { id: string; handle: string; w: number; h: number }[];
}

type Pending = { cmd: Command; resolve: (r: Reply) => void; reject: (e: Error) => void };

// One WebSocket to the engine; reconnects every second while the engine restarts.
// The engine answers a command on the same socket with one {"ok":...} reply and sends no
// request ids, so commands go out one at a time and the next reply settles the in-flight one.
export class Client {
  private ws: WebSocket | null = null;
  private queue: Pending[] = [];
  private inFlight: Pending | null = null;
  private closed = false;
  private retry: ReturnType<typeof setTimeout> | undefined;
  private statusListeners = new Set<(s: Status) => void>();
  private eventListeners = new Set<(e: EventStore) => void>();
  private previewListeners = new Map<string, Set<(f: PreviewFrame) => void>>();
  private angleListeners = new Set<(f: AngleFrame) => void>();
  private surfaceListeners = new Set<(r: SurfaceReply) => void>();
  private surfaceSub: SurfaceRequest[] | null = null;
  private fallbackPreviews = false;

  constructor(
    private url: string,
    private wsFactory: (url: string) => WebSocket = (u) => new WebSocket(u),
  ) {
    this.open();
  }

  private open() {
    const ws = this.wsFactory(this.url);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => {
      // Sent through the queue: the engine answers on the same socket and sends no ids.
      if (this.fallbackPreviews) void this.send({ cmd: 'wantPreviews' }).catch(() => {});
      if (this.surfaceSub) void this.subscribeSurfaces(this.surfaceSub).catch(() => {});
      this.pump();
    };
    ws.onmessage = (e) => this.onMessage(e.data);
    ws.onclose = () => {
      const err = new Error('disconnected from the engine');
      this.inFlight?.reject(err);
      this.inFlight = null;
      for (const p of this.queue) p.reject(err);
      this.queue = [];
      if (!this.closed) this.retry = setTimeout(() => this.open(), 1000);
    };
    this.ws = ws;
  }

  close() {
    this.closed = true;
    clearTimeout(this.retry);
    this.ws?.close();
  }

  send(cmd: Command): Promise<Reply> {
    return new Promise<Reply>((resolve, reject) => {
      this.queue.push({ cmd, resolve, reject });
      this.pump();
    });
  }

  private pump() {
    if (this.inFlight || this.queue.length === 0 || this.ws?.readyState !== 1) return;
    this.inFlight = this.queue.shift() ?? null;
    if (this.inFlight) this.ws.send(JSON.stringify(this.inFlight.cmd));
  }

  onStatus(fn: (s: Status) => void) {
    this.statusListeners.add(fn);
    return () => this.statusListeners.delete(fn);
  }

  onEvents(fn: (e: EventStore) => void) {
    this.eventListeners.add(fn);
    return () => this.eventListeners.delete(fn);
  }

  onPreview(id: string, fn: (f: PreviewFrame) => void) {
    let set = this.previewListeners.get(id);
    if (!set) this.previewListeners.set(id, (set = new Set()));
    set.add(fn);
    return () => set!.delete(fn);
  }

  onAngleRow(fn: (f: AngleFrame) => void) {
    this.angleListeners.add(fn);
    return () => this.angleListeners.delete(fn);
  }

  // The registry re-subscribes on every reconnect so the shell gets the new engine's pid and handles.
  onSurfaces(fn: (r: SurfaceReply) => void) {
    this.surfaceListeners.add(fn);
    return () => this.surfaceListeners.delete(fn);
  }

  subscribeSurfaces(surfaces: SurfaceRequest[]): Promise<SurfaceReply> {
    this.surfaceSub = surfaces;
    return this.send({ cmd: 'subscribeSurfaces', surfaces }).then((r) => {
      if (!r.ok) throw new Error(r.error);
      const reply = { pid: r.pid as number, surfaces: r.surfaces as SurfaceReply['surfaces'] };
      for (const fn of this.surfaceListeners) fn(reply);
      return reply;
    });
  }

  // Fallback mode: H.264/raw pictures for this page, now and after every reconnect.
  wantPreviews(): Promise<Reply> {
    this.fallbackPreviews = true;
    return this.send({ cmd: 'wantPreviews' });
  }

  private onMessage(data: unknown) {
    if (data instanceof ArrayBuffer) {
      const frame = parseBinary(data);
      if (frame?.kind === 'preview') for (const fn of this.previewListeners.get(frame.id) ?? []) fn(frame);
      if (frame?.kind === 'angle') for (const fn of this.angleListeners) fn(frame);
      return;
    }
    let msg: Record<string, unknown>;
    try {
      msg = JSON.parse(String(data));
    } catch {
      return;
    }
    if (msg.type === 'status') {
      for (const fn of this.statusListeners) fn(msg as unknown as Status);
      return;
    }
    if (msg.type === 'events') {
      for (const fn of this.eventListeners) fn(msg.events as EventStore);
      return;
    }
    if (!('ok' in msg)) return;
    const pending = this.inFlight;
    this.inFlight = null;
    pending?.resolve(msg as Reply);
    this.pump();
  }
}

type ParsedBinary = ({ kind: 'preview' } & PreviewFrame) | ({ kind: 'angle' } & AngleFrame);

function parseBinary(buf: ArrayBuffer): ParsedBinary | null {
  const v = new DataView(buf);
  if (buf.byteLength >= 16 && v.getUint8(0) === 0x42 && v.getUint8(1) === 0x52 && v.getUint8(2) === 0x41 && v.getUint8(3) === 0x50) {
    const width = v.getUint32(8, true);
    const height = v.getUint32(12, true);
    if (buf.byteLength < 16 + width * height * 4) return null;
    return { kind: 'angle', index: v.getUint32(4, true), width, height, bgra: new Uint8Array(buf, 16) };
  }
  if (buf.byteLength < 4 || v.getUint8(0) !== 1) return null;
  const idLen = v.getUint16(2, true);
  const ptsAt = 4 + idLen;
  if (buf.byteLength < ptsAt + 8) return null;
  return {
    kind: 'preview',
    id: new TextDecoder().decode(new Uint8Array(buf, 4, idLen)),
    keyframe: (v.getUint8(1) & 1) === 1,
    ptsUs: Number(v.getBigInt64(ptsAt, true)),
    data: new Uint8Array(buf, ptsAt + 8),
  };
}

export function connect(port: number | string, token: string, wsFactory?: (url: string) => WebSocket): Client {
  return new Client(`ws://127.0.0.1:${port}/?token=${encodeURIComponent(token)}`, wsFactory);
}