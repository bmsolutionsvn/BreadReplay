import { useEffect, useRef } from 'react';

import type { Client } from '../api/client';
import type { Status } from '../api/types';

type Props = {
  status: Status | null;
  client: Pick<Client, 'send'> & Partial<Pick<Client, 'onAngleRow'>>;
};

// Every input at Preview's timecode; click switches Preview's angle (spec §7).
export function AngleRow({ status, client }: Props) {
  const canvases = useRef<Array<HTMLCanvasElement | null>>([]);
  const preview = status?.players.preview;

  useEffect(() => {
    if (!client.onAngleRow) return;
    const off = client.onAngleRow((f) => {
      const c = canvases.current[f.index];
      if (!c) return;
      c.width = f.width;
      c.height = f.height;
      c.getContext('2d')?.putImageData(new ImageData(new Uint8ClampedArray(f.bgra), f.width, f.height), 0, 0);
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
          >
            <canvas ref={(el) => (canvases.current[i] = el)} aria-hidden="true" />
            <b>{input.name}</b>
            <small className={`st ${input.state}`}>{input.state}</small>
          </button>
        ))}
      </div>
    </section>
  );
}