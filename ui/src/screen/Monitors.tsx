import { useEffect, useRef } from 'react';

import type { Client } from '../api/client';
import type { Status } from '../api/types';
import { formatRemaining } from '../format';

type Props = {
  status: Status | null;
  fps?: number;
  client?: Pick<Client, 'onPreview'> | null;
};

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

export function Monitors({ status, fps = 59.94, client }: Props) {
  const preview = status?.players.preview;
  const output = status?.players.output;
  return (
    <div className="mons">
      <div className="mon pv" aria-label="Preview monitor">
        {client && <MonitorCanvas client={client} id="preview" />}
        <span className="tag">PREVIEW · operator</span>
        <span className="tc">{preview?.tc ?? '--:--:--:--'}</span>
        <div className="ident">
          <b>{inputName(status, preview?.angle)}</b>
          <small>{preview?.eventId ? `Event ${preview.eventId} cued` : 'Live'}</small>
        </div>
      </div>
      <div className="mon pg onair" aria-label="Output monitor">
        {client && <MonitorCanvas client={client} id="output" />}
        <span className="tag">OUTPUT · on air</span>
        <span className="tc">
          {output?.tc ?? '--:--:--:--'}
          {output ? ` · ${Math.round(output.speed * 100)}%` : ''}
        </span>
        <div className="remain">
          <small>{output?.eventId ? `Event ${output.eventId} · ${inputName(status, output.angle)} · remaining` : 'No event on air'}</small>
          <div className="big" data-testid="remaining">
            {formatRemaining(output?.remainingS, fps)}
          </div>
          <div className="prog">
            <span />
          </div>
        </div>
      </div>
    </div>
  );
}

// The status has no event length, so the bar stays empty; the countdown carries the call.

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