import { useEffect, useRef } from 'react';

import type { Client, Command } from '../api/client';
import type { EventStore, Status } from '../api/types';
import { useContextMenu, type MenuItem } from '../ContextMenu';
import { formatRemaining } from '../format';
import { keyLabel } from '../hotkeys';

type Props = {
  status: Status | null;
  fps?: number;
  client?: (Pick<Client, 'onPreview'> & Partial<Pick<Client, 'send'>>) | null;
  events?: EventStore | null;
  hotkeys?: Record<string, string>;
  lastN?: number;
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
  return status?.inputs.find((i) => i.id === id)?.name || id || 'No angle';
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

export function Monitors({ status, fps = 59.94, client, events, hotkeys = {}, lastN = 12 }: Props) {
  const menu = useContextMenu();
  const preview = status?.players.preview;
  const output = status?.players.output;
  const onAir = !!output?.eventId;
  const onAirEvent = onAir ? events?.events.find((e) => e.id === output?.eventId) : undefined;
  const total = onAirEvent ? playSeconds(onAirEvent, output?.speed ?? 1) : 0;
  const progress = total > 0 && output?.remainingS != null ? Math.min(1, Math.max(0, 1 - output.remainingS / total)) : 0;
  const send = (c: Command) => void client?.send?.(c);

  const previewMenu: MenuItem[] = [
    { label: 'Mark In', hint: keyLabel('mark.in', hotkeys), onSelect: () => send({ cmd: 'mark.in' }) },
    { label: 'Mark Out', hint: keyLabel('mark.out', hotkeys), onSelect: () => send({ cmd: 'mark.out' }) },
    { label: `Save last ${lastN} s`, hint: keyLabel('mark.lastN', hotkeys), onSelect: () => send({ cmd: 'mark.lastN', n: lastN }) },
    'separator',
    { label: preview?.playing ? 'Pause' : 'Play', hint: keyLabel('player.play', hotkeys), onSelect: () => send({ cmd: preview?.playing ? 'player.pause' : 'player.play', player: 'preview' }) },
    { label: 'Back to live', onSelect: () => send({ cmd: 'player.live', player: 'preview' }) },
    { label: preview?.locked ? 'Unlock speed lever' : 'Lock speed lever', hint: keyLabel('player.lock', hotkeys), onSelect: () => send({ cmd: 'player.lock', player: 'preview', locked: !preview?.locked }) },
    'separator',
    { label: 'Take to air', hint: keyLabel('take', hotkeys), onSelect: () => send({ cmd: 'take' }) },
  ];
  const outputMenu: MenuItem[] = [
    { label: output?.playing ? 'Pause output' : 'Play output', disabled: !onAir, onSelect: () => send({ cmd: output?.playing ? 'player.pause' : 'player.play', player: 'output' }) },
  ];

  return (
    <div className="mons">
      <div className="mon pv" aria-label="Preview monitor" onContextMenu={(e) => menu(e, previewMenu)}>
        {client && <MonitorCanvas client={client} id="preview" />}
        <span className="tag">PREVIEW · operator</span>
        <span className="tc">{preview?.tc ?? '--:--:--:--'}</span>
        <div className="ident">
          <b>{inputName(status, preview?.angle)}</b>
          <small>{preview?.eventId ? `Event ${preview.eventId} cued` : 'Live'}</small>
        </div>
      </div>
      <div className={`mon pg ${onAir ? 'onair' : ''}`} aria-label="Output monitor" onContextMenu={(e) => menu(e, outputMenu)}>
        {client && <MonitorCanvas client={client} id="output" />}
        <span className="tag">{onAir ? 'OUTPUT · on air' : 'OUTPUT · idle'}</span>
        <span className="tc">
          {onAir ? `${output?.tc} · ${Math.round((output?.speed ?? 1) * 100)}%` : '--:--:--:--'}
        </span>
        <div className="remain">
          <small>{output?.eventId ? `Event ${output.eventId} · ${inputName(status, output.angle)} · remaining` : 'No event on air'}</small>
          <div className="big" data-testid="remaining">
            {formatRemaining(output?.remainingS, fps)}
          </div>
          <div className="prog" role="progressbar" aria-label="Event progress" aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(progress * 100)}>
            <span style={{ width: `${progress * 100}%` }} />
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