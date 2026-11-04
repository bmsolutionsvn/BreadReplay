import { useEffect, useRef } from 'react';

import type { Client } from '../api/client';
import type { Status } from '../api/types';
import { useContextMenu } from '../ContextMenu';
import { signalHint } from '../format';
import { keyLabel } from '../hotkeys';
import { cameraColor } from './cameraColors';
import { NativeVideo } from './NativeVideo';

type Props = {
  status: Status | null;
  client: Pick<Client, 'send'> & Partial<Pick<Client, 'onAngleRow'>>;
  hotkeys?: Record<string, string>;
  native?: boolean;
};

// Every input at Preview's timecode; click switches Preview's angle (spec §7).
export function AngleRow({ status, client, hotkeys = {}, native = false }: Props) {
  const menu = useContextMenu();
  const canvases = useRef<Array<HTMLCanvasElement | null>>([]);
  const preview = status?.players.preview;
  const output = status?.players.output;
  const onAir = !!output?.eventId;

  useEffect(() => {
    if (!client.onAngleRow) return;
    const off = client.onAngleRow((f) => {
      const c = canvases.current[f.index];
      if (!c) return;
      c.width = f.width;
      c.height = f.height;
      // The engine sends BGRA; ImageData wants RGBA.
      const rgba = new Uint8ClampedArray(f.bgra);
      for (let p = 0; p < rgba.length; p += 4) {
        const b = rgba[p];
        rgba[p] = rgba[p + 2];
        rgba[p + 2] = b;
      }
      c.getContext('2d')?.putImageData(new ImageData(rgba, f.width, f.height), 0, 0);
    });
    return () => {
      off();
    };
  }, [client]);

  return (
    <section className="angles" aria-label="Angles">
      <h5 title="Every camera at the same moment as Preview. Click a camera to show it in Preview.">{`All cameras at ${preview?.tc ?? '--:--:--:--'}`}</h5>
      <div className="angle-row">
        {(status?.inputs ?? []).map((input, i) => {
          const target = preview?.angle === input.id;
          const onPgm = onAir && output?.angle === input.id;
          const key = i < 8 ? keyLabel(`player.angle${i + 1}`, hotkeys) : '';
          return (
          <button
            key={input.id}
            className={`angle ${target ? 'target' : 'dim'}`}
            style={target ? { boxShadow: `inset 0 0 0 3px ${cameraColor(i)}` } : undefined}
            aria-label={`Angle ${input.name}`}
            title={`Show ${input.name} in Preview${key ? ` (${key})` : ''}. Right-click for more.`}
            onClick={() => client.send({ cmd: 'player.angle', player: 'preview', inputId: input.id })}
            onContextMenu={(e) =>
              menu(e, [
                {
                  label: `Show ${input.name} in Preview`,
                  hint: keyLabel(`player.angle${i + 1}`, hotkeys),
                  onSelect: () => void client.send({ cmd: 'player.angle', player: 'preview', inputId: input.id }),
                },
                {
                  label: `Watch ${input.name} live now`,
                  onSelect: async () => {
                    await client.send({ cmd: 'player.angle', player: 'preview', inputId: input.id });
                    await client.send({ cmd: 'player.live', player: 'preview' });
                  },
                },
              ])
            }
          >
            <NativeVideo
              id={`angle:${input.id}`}
              native={native}
              fallback={<canvas ref={(el) => (canvases.current[i] = el)} aria-hidden="true" />}
            />
            {target && <span className="aim" style={{ borderColor: cameraColor(i), color: cameraColor(i) }} />}
            <span className="foot">
              <span className="num" style={{ background: cameraColor(i) }}>{i + 1}</span>
              <b>{input.name}</b>
              <span className="tally">
                {onPgm && <i className="pgm">PGM</i>}
                {target && <i className="pvw">PVW</i>}
              </span>
            </span>
            <small className={`st ${input.state}`} title={signalHint(input.state)}>
              {input.state}
            </small>
          </button>
          );
        })}
      </div>
    </section>
  );
}