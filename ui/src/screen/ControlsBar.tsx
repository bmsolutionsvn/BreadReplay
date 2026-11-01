import type { Client } from '../api/client';

export function ControlsBar({ client, lastN }: { client: Pick<Client, 'send'>; lastN: number }) {
  return (
    <div className="controls" role="toolbar" aria-label="Controls">
      <button className="k" onClick={() => client.send({ cmd: 'mark.in' })}>
        In
      </button>
      <button className="k" onClick={() => client.send({ cmd: 'mark.out' })}>
        Out
      </button>
      <button className="k hot" onClick={() => client.send({ cmd: 'mark.lastN', n: lastN })}>
        Last {lastN} s
      </button>
      <span className="jog">
        <button className="k" aria-label="Step back" onClick={() => client.send({ cmd: 'player.step', player: 'preview', frames: -1 })}>
          ◀
        </button>
        <button className="k" aria-label="Step forward" onClick={() => client.send({ cmd: 'player.step', player: 'preview', frames: 1 })}>
          ▶
        </button>
      </span>
      <button className="k go" onClick={() => client.send({ cmd: 'take' })}>
        Take ▶
      </button>
    </div>
  );
}