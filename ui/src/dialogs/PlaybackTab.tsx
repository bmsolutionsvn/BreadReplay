import type { ShowSettings } from '../api/types';

const RATES: Array<[string, string]> = [
  ['15/1', '15'],
  ['24000/1001', '23.976'],
  ['24/1', '24'],
  ['25/1', '25'],
  ['30000/1001', '29.97'],
  ['30/1', '30'],
  ['50/1', '50'],
  ['60000/1001', '59.94'],
  ['60/1', '60'],
  ['120000/1001', '119.88'],
  ['120/1', '120'],
];

export function PlaybackTab({ value, onChange }: { value: ShowSettings; onChange: (patch: Partial<ShowSettings>) => void }) {
  return (
    <div className="tab-playback">
      <h3>Replays</h3>
      <label className="field">
        <span>Frame rate (frames per second)</span>
        <select className="ctl" aria-label="Show frame rate" value={value.rate} onChange={(e) => onChange({ rate: e.target.value })}>
          {RATES.map(([v, l]) => (
            <option key={v} value={v}>
              {l}
            </option>
          ))}
        </select>
        <span className="hint">Match your production. Usually 59.94 or 50.</span>
      </label>
      <label className="field">
        <span>Save last length (seconds)</span>
        <input
          className="ctl mono"
          aria-label="Last N (s)"
          type="number"
          min={1}
          value={value.lastNSeconds}
          onChange={(e) => onChange({ lastNSeconds: Number(e.target.value) })}
        />
        <span className="hint">How much the Save last button keeps. 10 to 15 seconds fits most plays.</span>
      </label>
      <label className="field">
        <span>Start a little early (seconds)</span>
        <input
          className="ctl mono"
          aria-label="Cue pre-roll (s)"
          type="number"
          min={0}
          step={0.5}
          value={value.preRollS}
          onChange={(e) => onChange({ preRollS: Number(e.target.value) })}
        />
        <span className="hint">A loaded replay starts this much before its start mark.</span>
      </label>
      <label className="field">
        <span>Run a little longer (seconds)</span>
        <input
          className="ctl mono"
          aria-label="Cue post-roll (s)"
          type="number"
          min={0}
          step={0.5}
          value={value.postRollS}
          onChange={(e) => onChange({ postRollS: Number(e.target.value) })}
        />
        <span className="hint">A replay keeps playing this much after its end mark.</span>
      </label>
      <label className="field">
        <span>Speed for new replays</span>
        <select className="ctl" aria-label="Default speed" value={value.defaultSpeed} onChange={(e) => onChange({ defaultSpeed: Number(e.target.value) })}>
          <option value={1}>100% (normal)</option>
          <option value={0.75}>75% (slow motion)</option>
          <option value={0.5}>50% (slow motion)</option>
          <option value={0.25}>25% (very slow)</option>
        </select>
      </label>
      <label className="field">
        <span>Between replays in Play all</span>
        <select className="ctl" aria-label="Transition" value={value.transition} onChange={(e) => onChange({ transition: e.target.value as ShowSettings['transition'] })}>
          <option value="fade">Fade (smooth)</option>
          <option value="cut">Cut (instant)</option>
        </select>
      </label>
      <label className="field">
        <span>Fade length (seconds)</span>
        <input
          className="ctl mono"
          aria-label="Fade (s)"
          type="number"
          min={0}
          step={0.1}
          value={value.fadeS}
          onChange={(e) => onChange({ fadeS: Number(e.target.value) })}
        />
      </label>
      <label className="field">
        <span>When a replay ends</span>
        <select className="ctl" aria-label="End action" value={value.endAction} onChange={(e) => onChange({ endAction: e.target.value as ShowSettings['endAction'] })}>
          <option value="hold">Freeze on the last picture</option>
          <option value="slate">Show the slate image</option>
        </select>
      </label>
      <label className="field">
        <span>Slate image</span>
        <input className="ctl mono" aria-label="Slate image" value={value.slatePath} placeholder="C:\Shows\slate.png" onChange={(e) => onChange({ slatePath: e.target.value })} />
        <span className="hint">A picture (logo or sponsor) shown when a replay ends, if chosen above.</span>
      </label>
    </div>
  );
}