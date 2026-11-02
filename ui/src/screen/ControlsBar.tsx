import type { Client } from '../api/client';
import { keyLabel } from '../hotkeys';

type Props = { client: Pick<Client, 'send'>; lastN: number; hotkeys?: Record<string, string> };

// The operator's main buttons, each with its key so the mouse teaches the keyboard.
export function ControlsBar({ client, lastN, hotkeys = {} }: Props) {
  const key = (id: string) => <kbd>{keyLabel(id, hotkeys)}</kbd>;
  return (
    <div className="controls" role="toolbar" aria-label="Controls">
      <button className="k" onClick={() => client.send({ cmd: 'mark.in' })}>
        Mark In {key('mark.in')}
      </button>
      <button className="k" onClick={() => client.send({ cmd: 'mark.out' })}>
        Mark Out {key('mark.out')}
      </button>
      <button className="k hot" onClick={() => client.send({ cmd: 'mark.lastN', n: lastN })}>
        Last {lastN} s {key('mark.lastN')}
      </button>
      <span className="jog">
        <button className="k" aria-label="Step back" title={`Step back (${keyLabel('step.back', hotkeys)})`} onClick={() => client.send({ cmd: 'player.step', player: 'preview', frames: -1 })}>
          ◀
        </button>
        <button className="k" aria-label="Step forward" title={`Step forward (${keyLabel('step.fwd', hotkeys)})`} onClick={() => client.send({ cmd: 'player.step', player: 'preview', frames: 1 })}>
          ▶
        </button>
      </span>
      <button className="k" onClick={() => client.send({ cmd: 'player.live', player: 'preview' })}>
        Back to live
      </button>
      <button className="k go" onClick={() => client.send({ cmd: 'take' })}>
        Take {key('take')}
      </button>
    </div>
  );
}
