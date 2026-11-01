import type { Client } from '../api/client';
import type { Status } from '../api/types';

export function SpeedLever({ status, client }: { status: Status | null; client: Pick<Client, 'send'> }) {
  const player = status?.players.preview;
  const speed = player?.speed ?? 1;
  const locked = player?.locked ?? true;
  return (
    <div className="lever">
      <span className="lbl">100</span>
      <input
        type="range"
        aria-label="Speed lever"
        min={0}
        max={2}
        step={0.05}
        value={speed}
        onChange={(e) => client.send({ cmd: 'player.lever', player: 'preview', speed: Number(e.target.value) })}
      />
      <span className="mono pct">{Math.round(speed * 100)}%</span>
      <button
        className={`lock ${locked ? 'on' : ''}`}
        aria-pressed={locked}
        onClick={() => client.send({ cmd: 'player.lock', player: 'preview', locked: !locked })}
      >
        {locked ? 'Locked' : 'Free'}
      </button>
    </div>
  );
}