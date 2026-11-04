import { useEffect, useRef } from 'react';

import type { Client, Command } from '../api/client';
import type { EventStore, Status } from '../api/types';
import { useContextMenu, type MenuItem } from '../ContextMenu';
import { formatClockTenths, formatRemaining } from '../format';
import { keyLabel } from '../hotkeys';
import { cameraColor, cameraIndex, type MarkStamp } from './cameraColors';
import { NativeVideo } from './NativeVideo';

type Props = {
  status: Status | null;
  fps?: number;
  client?: (Pick<Client, 'onPreview'> & Partial<Pick<Client, 'send'>>) | null;
  events?: EventStore | null;
  hotkeys?: Record<string, string>;
  lastN?: number;
  native?: boolean;
  stamp?: MarkStamp | null;
  onMark?: (c: Command) => void;
};

// Seconds the on-air event takes to play at its speed points (the lever's speed when it has none).
export function playSeconds(ev: EventStore['events'][number], fallbackSpeed: number): number {
  const pts = ev.speeds.length ? ev.speeds : [{ tUs: ev.inUs, speed: fallbackSpeed }];
  let total = 0;
  for (let i = 0; i < pts.length; i++) {
    const from = Math.max(ev.inUs, i === 0 ? ev.inUs : pts[i].tUs);
    const to = i + 1 < pts.length ? pts[i + 1].tUs : ev.outUs;
    if (to > from) total += (to - from) / 1e6 / Math.max(0.01, pts[i].speed || fallbackSpeed || 1);
  }
  return total;
}

function inputName(status: Status | null, id: string | undefined): string {
  return status?.inputs.find((i) => i.id === id)?.name || id || 'No camera';
}

// Decodes the engine's H.264 preview with WebCodecs (hardware decode in WebView2).
function MonitorCanvas({ client, id }: { client: Pick<Client, 'onPreview'>; id: 'preview' | 'output' }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    if (typeof VideoDecoder === 'undefined') return;
    let configured = false;
    const decoder = new VideoDecoder({
      output: (frame) => {
        const c = canvas.current;
        if (c) {
          c.width = frame.displayWidth;
          c.height = frame.displayHeight;
          c.getContext('2d')?.drawImage(frame, 0, 0);
        }
        frame.close();
      },
      error: () => {
        configured = false;
      },
    });
    const off = client.onPreview(id, (m) => {
      if (!configured) {
        if (!m.keyframe) return;
        const codec = codecFromSps(m.data);
        if (!codec) return;
        decoder.configure({ codec, optimizeForLatency: true });
        configured = true;
      }
      if (decoder.state !== 'configured') return;
      decoder.decode(new EncodedVideoChunk({ type: m.keyframe ? 'key' : 'delta', timestamp: m.ptsUs, data: m.data }));
    });
    return () => {
      off();
      if (decoder.state !== 'closed') decoder.close();
    };
  }, [client, id]);
  return <canvas ref={canvas} aria-hidden="true" />;
}

export function Monitors({ status, fps = 59.94, client, events, hotkeys = {}, lastN = 12, native = false, stamp = null, onMark }: Props) {
  const menu = useContextMenu();
  const preview = status?.players.preview;
  const output = status?.players.output;
  const onAir = !!output?.eventId;
  const pvwIndex = cameraIndex(status, preview?.angle);
  const pgmIndex = cameraIndex(status, output?.angle);
  const onAirEvent = onAir ? events?.events.find((e) => e.id === output?.eventId) : undefined;
  const total = onAirEvent ? playSeconds(onAirEvent, output?.speed ?? 1) : 0;
  const progress = total > 0 && output?.remainingS != null ? Math.min(1, Math.max(0, 1 - output.remainingS / total)) : 0;
  const send = (c: Command) => void client?.send?.(c);
  const mark = (c: Command) => (onMark ? onMark(c) : send(c));

  const previewMenu: MenuItem[] = [
    { label: 'Mark replay start here', hint: keyLabel('mark.in', hotkeys), onSelect: () => mark({ cmd: 'mark.in' }) },
    { label: 'Mark replay end here', hint: keyLabel('mark.out', hotkeys), onSelect: () => mark({ cmd: 'mark.out' }) },
    { label: `Save the last ${lastN} seconds as a replay`, hint: keyLabel('mark.lastN', hotkeys), onSelect: () => mark({ cmd: 'mark.lastN', n: lastN }) },
    'separator',
    { label: preview?.playing ? 'Pause' : 'Play', hint: keyLabel('player.play', hotkeys), onSelect: () => send({ cmd: preview?.playing ? 'player.pause' : 'player.play', player: 'preview' }) },
    { label: 'Back to live (what is happening now)', onSelect: () => send({ cmd: 'player.live', player: 'preview' }) },
    { label: preview?.locked ? 'Unlock the speed slider' : 'Lock the speed slider', hint: keyLabel('player.lock', hotkeys), onSelect: () => send({ cmd: 'player.lock', player: 'preview', locked: !preview?.locked }) },
    'separator',
    { label: 'Play this on air', hint: keyLabel('take', hotkeys), onSelect: () => send({ cmd: 'take' }) },
  ];
  const outputMenu: MenuItem[] = [
    { label: output?.playing ? 'Pause what is on air' : 'Continue playing on air', disabled: !onAir, onSelect: () => send({ cmd: output?.playing ? 'player.pause' : 'player.play', player: 'output' }) },
  ];

  return (
    <div className="mons">
      <div
        className="mon pv"
        aria-label="Preview monitor"
        onContextMenu={(e) => menu(e, previewMenu)}
      >
        {client && (
          <NativeVideo id="preview" native={native} fallback={<MonitorCanvas client={client} id="preview" />} />
        )}
        <span className="tag" title="Only you see Preview. Find the moment here, then press Play on air.">
          PVW {pvwIndex >= 0 && <span className="cam">{pvwIndex + 1}</span>}
        </span>
        {stamp && (
          <span className="stamp" data-testid="mark-stamp">
            {stamp.edge === 'in' ? 'IN' : 'OUT'}
            <span className="num" style={{ background: cameraColor(cameraIndex(status, stamp.inputId)) }}>
              {cameraIndex(status, stamp.inputId) + 1}
            </span>
            {formatClockTenths(stamp.tUs)}
          </span>
        )}
        <span className="tc">{preview?.tc ?? '--:--:--:--'}</span>
        <div className="ident">
          <b>{inputName(status, preview?.angle)}</b>
        </div>
      </div>
      <div
        className={`mon pg ${onAir ? 'onair' : ''}`}
        aria-label="Output monitor"
        onContextMenu={(e) => menu(e, outputMenu)}
      >
        {client && (
          <NativeVideo id="output" native={native} fallback={<MonitorCanvas client={client} id="output" />} />
        )}
        <span className="tag" title="What viewers see. The director takes this feed. Called 'Program' or 'Output' elsewhere.">
          PGM {onAir && pgmIndex >= 0 && <span className="cam">{pgmIndex + 1}</span>}
        </span>
        <span className="tc">
          {onAir ? `${output?.tc} · ${Math.round((output?.speed ?? 1) * 100)}%` : '--:--:--:--'}
        </span>
        <div className="remain">
          {onAir && <small>{inputName(status, output?.angle)}</small>}
          <div className="big" data-testid="remaining">
            {formatRemaining(output?.remainingS, fps)}
          </div>
          <div className="prog" role="progressbar" aria-label="Event progress" aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(progress * 100)}>
            <span style={{ transform: `scaleX(${progress})` }} />
          </div>
        </div>
      </div>
    </div>
  );
}


const hex = (n: number) => n.toString(16).toUpperCase().padStart(2, '0');

// WebCodecs codec string ("avc1.PPCCLL") from the SPS inside an Annex-B access unit.
function codecFromSps(au: Uint8Array): string | null {
  for (let i = 0; i + 6 < au.length; i++) {
    if (au[i] === 0 && au[i + 1] === 0 && au[i + 2] === 1 && (au[i + 3] & 0x1f) === 7) {
      return `avc1.${hex(au[i + 4])}${hex(au[i + 5])}${hex(au[i + 6])}`;
    }
  }
  return null;
}