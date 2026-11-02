import type { Client } from '../api/client';
import type { Status } from '../api/types';
import { keyLabel } from '../hotkeys';

const PRESETS = [1, 0.75, 0.5, 0.25];

// Spec §5: 0–100 % (no fast-forward in v1), locked unless unlocked so a stray drag cannot change it.
export function SpeedLever({ status, client, hotkeys = {} }: { status: Status | null; client: Pick<Client, 'send'>; hotkeys?: Record<string, string> }) {
  const player = status?.players.preview;
  const speed = player?.speed ?? 1;
  const locked = player?.locked ?? true;
  return (
    <div className={`lever ${locked ? 'locked' : ''}`}>
      <span className="mono pct" aria-live="polite">
        {Math.round(speed * 100)}%
      </span>
      <input
        type="range"
        aria-label="Speed lever"
        min={0}
        max={1}
        step={0.05}
        value={Math.min(1, speed)}
        disabled={locked}
        title={locked ? `Locked: press ${keyLabel('player.lock', hotkeys)} or click Locked to ride the lever` : undefined}
        onChange={(e) => client.send({ cmd: 'player.lever', player: 'preview', speed: Number(e.target.value) })}
      />
      <div className="presets">
        {PRESETS.map((p, i) => (
          <button
            key={p}
            className={Math.abs(speed - p) < 0.01 ? 'on' : ''}
            title={`${p * 100}% (${keyLabel(`player.preset${i + 1}`, hotkeys)})`}
            onClick={() => client.send({ cmd: 'player.preset', player: 'preview', speed: p })}
          >
            {p * 100}
          </button>
        ))}
      </div>
      <button
        className={`lock ${locked ? 'on' : ''}`}
        aria-pressed={locked}
        title={`Lock or unlock the lever (${keyLabel('player.lock', hotkeys)})`}
        onClick={() => client.send({ cmd: 'player.lock', player: 'preview', locked: !locked })}
      >
        {locked ? 'Locked' : 'Free'}
      </button>
    </div>
  );
}
