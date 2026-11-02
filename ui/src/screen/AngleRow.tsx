import { useEffect, useRef } from 'react';

import type { Client } from '../api/client';
import type { Status } from '../api/types';
import { useContextMenu } from '../ContextMenu';
import { keyLabel } from '../hotkeys';

type Props = {
  status: Status | null;
  client: Pick<Client, 'send'> & Partial<Pick<Client, 'onAngleRow'>>;
  hotkeys?: Record<string, string>;
};

// Every input at Preview's timecode; click switches Preview's angle (spec §7).
export function AngleRow({ status, client, hotkeys = {} }: Props) {
  const menu = useContextMenu();
  const canvases = useRef<Array<HTMLCanvasElement | null>>([]);
  const preview = status?.players.preview;

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
      <h5>{`All angles at Preview time ${preview?.tc ?? '--:--:--:--'}`}</h5>
      <div className="angle-row">
        {(status?.inputs ?? []).map((input, i) => (
          <button
            key={input.id}
            className={`angle ${preview?.angle === input.id ? 'sel' : ''}`}
            aria-label={`Angle ${input.name}`}
            onClick={() => client.send({ cmd: 'player.angle', player: 'preview', inputId: input.id })}
            onContextMenu={(e) =>
              menu(e, [
                {
                  label: `Show ${input.name} on Preview`,
                  hint: keyLabel(`player.angle${i + 1}`, hotkeys),
                  onSelect: () => void client.send({ cmd: 'player.angle', player: 'preview', inputId: input.id }),
                },
                {
                  label: `Go live on ${input.name}`,
                  onSelect: async () => {
                    await client.send({ cmd: 'player.angle', player: 'preview', inputId: input.id });
                    await client.send({ cmd: 'player.live', player: 'preview' });
                  },
                },
              ])
            }
          >
            <canvas ref={(el) => (canvases.current[i] = el)} aria-hidden="true" />
            {i < 8 && <kbd className="slot">{keyLabel(`player.angle${i + 1}`, hotkeys)}</kbd>}
            <b>{input.name}</b>
            <small className={`st ${input.state}`}>{input.state}</small>
          </button>
        ))}
      </div>
    </section>
  );
}