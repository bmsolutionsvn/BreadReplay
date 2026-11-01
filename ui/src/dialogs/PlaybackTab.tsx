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
      <h3>Playback</h3>
      <label className="field">
        <span>Show frame rate</span>
        <select className="ctl" aria-label="Show frame rate" value={value.rate} onChange={(e) => onChange({ rate: e.target.value })}>
          {RATES.map(([v, l]) => (
            <option key={v} value={v}>
              {l}
            </option>
          ))}
        </select>
      </label>
      <label className="field">
        <span>Last N (s)</span>
        <input
          className="ctl mono"
          aria-label="Last N (s)"
          type="number"
          min={1}
          value={value.lastNSeconds}
          onChange={(e) => onChange({ lastNSeconds: Number(e.target.value) })}
        />
      </label>
      <label className="field">
        <span>Cue pre-roll (s)</span>
        <input
          className="ctl mono"
          aria-label="Cue pre-roll (s)"
          type="number"
          min={0}
          step={0.5}
          value={value.preRollS}
          onChange={(e) => onChange({ preRollS: Number(e.target.value) })}
        />
      </label>
      <label className="field">
        <span>Cue post-roll (s)</span>
        <input
          className="ctl mono"
          aria-label="Cue post-roll (s)"
          type="number"
          min={0}
          step={0.5}
          value={value.postRollS}
          onChange={(e) => onChange({ postRollS: Number(e.target.value) })}
        />
      </label>
      <label className="field">
        <span>Default speed</span>
        <select className="ctl" aria-label="Default speed" value={value.defaultSpeed} onChange={(e) => onChange({ defaultSpeed: Number(e.target.value) })}>
          <option value={1}>100%</option>
          <option value={0.75}>75%</option>
          <option value={0.5}>50%</option>
          <option value={0.25}>25%</option>
        </select>
      </label>
      <label className="field">
        <span>Transition</span>
        <select className="ctl" aria-label="Transition" value={value.transition} onChange={(e) => onChange({ transition: e.target.value as ShowSettings['transition'] })}>
          <option value="fade">Fade</option>
          <option value="cut">Cut</option>
        </select>
      </label>
      <label className="field">
        <span>Fade (s)</span>
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
        <span>End action</span>
        <select className="ctl" aria-label="End action" value={value.endAction} onChange={(e) => onChange({ endAction: e.target.value as ShowSettings['endAction'] })}>
          <option value="hold">Hold the last frame</option>
          <option value="slate">Show the slate</option>
        </select>
      </label>
      <label className="field">
        <span>Slate image</span>
        <input className="ctl mono" aria-label="Slate image" value={value.slatePath} placeholder="C:\Shows\slate.png" onChange={(e) => onChange({ slatePath: e.target.value })} />
      </label>
    </div>
  );
}