import type { Client, Command } from '../api/client';
import { keyLabel } from '../hotkeys';

type Props = {
  client: Pick<Client, 'send'>;
  lastN: number;
  hotkeys?: Record<string, string>;
  target?: { num: number; color: string };
  onMark?: (c: Command) => void;
};

// The operator's main buttons, each with its key so the mouse teaches the keyboard, and a
// one-line tooltip in plain words (the industry term in brackets for those who know it).
export function ControlsBar({ client, lastN, hotkeys = {}, target, onMark }: Props) {
  const key = (id: string) => <kbd>{keyLabel(id, hotkeys)}</kbd>;
  const mark = (c: Command) => (onMark ? onMark(c) : void client.send(c));
  const badge = target && (
    <>
      <span className="aimmini" style={{ borderColor: target.color, color: target.color }} />
      <span className="num" style={{ background: target.color }}>{target.num}</span>
    </>
  );
  return (
    <div className="controls" role="toolbar" aria-label="Controls">
      <button
        className="k hot"
        title={`Saves the last ${lastN} seconds as a new replay. Press it right after a good play.`}
        onClick={() => mark({ cmd: 'mark.lastN', n: lastN })}
      >
        Save last {lastN} s {key('mark.lastN')}
      </button>
      <button className="k mark" style={target ? { borderColor: target.color } : undefined} title="Marks where a replay starts, at the picture in Preview (Mark In)." onClick={() => mark({ cmd: 'mark.in' })}>
        Mark start {badge} {key('mark.in')}
      </button>
      <button className="k mark" style={target ? { borderColor: target.color } : undefined} title="Marks where the replay ends, at the picture in Preview (Mark Out)." onClick={() => mark({ cmd: 'mark.out' })}>
        Mark end {badge} {key('mark.out')}
      </button>
      <span className="jog">
        <button
          className="k"
          aria-label="One frame back"
          title={`One frame back (${keyLabel('step.back', hotkeys)})`}
          onClick={() => client.send({ cmd: 'player.step', player: 'preview', frames: -1 })}
        >
          ◀
        </button>
        <button
          className="k"
          aria-label="One frame forward"
          title={`One frame forward (${keyLabel('step.fwd', hotkeys)})`}
          onClick={() => client.send({ cmd: 'player.step', player: 'preview', frames: 1 })}
        >
          ▶
        </button>
      </span>
      <button className="k" title="Jumps Preview back to what is happening right now." onClick={() => client.send({ cmd: 'player.live', player: 'preview' })}>
        Back to live
      </button>
      <button
        className="k go"
        title="Plays what is in Preview on air, where viewers see it (Take)."
        onClick={() => client.send({ cmd: 'take' })}
      >
        Play on air {key('take')}
      </button>
    </div>
  );
}
