import { useState } from 'react';

import type { Client } from '../api/client';
import type { ShowSettings } from '../api/types';

type Preflight = {
  canRecord: boolean;
  minutesFree: number;
  writeMBps: number;
  warnings: string[];
  error: string | null;
};

type Props = {
  client: Pick<Client, 'send'>;
  value: ShowSettings;
  onChange: (patch: Partial<ShowSettings>) => void;
  limitHours: string;
  onLimitHours: (v: string) => void;
  limitError: string;
};

export function RecordingTab({ client, value, onChange, limitHours, onLimitHours, limitError }: Props) {
  const [preflight, setPreflight] = useState<Preflight | null>(null);
  const [error, setError] = useState('');

  async function run() {
    setError('');
    try {
      const reply = await client.send({ cmd: 'record.preflight' });
      if (!reply.ok) return setError(reply.error);
      setPreflight({
        canRecord: Boolean(reply.canRecord),
        minutesFree: Number(reply.minutesFree),
        writeMBps: Number(reply.writeMBps),
        warnings: (reply.warnings as string[]) ?? [],
        error: (reply.error as string | null) ?? null,
      });
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }

  return (
    <div className="tab-recording">
      <h3>Recording</h3>
      <label className="field">
        <span>Recording folder</span>
        <input
          className="ctl mono"
          aria-label="Recording folder"
          value={value.recordingFolder}
          placeholder="D:\Replay"
          onChange={(e) => onChange({ recordingFolder: e.target.value })}
        />
      </label>
      <label className="field">
        <span>Recording limit (hours)</span>
        <input
          className="ctl mono"
          aria-label="Recording limit (hours)"
          aria-invalid={!!limitError}
          value={limitHours}
          placeholder="No limit"
          onChange={(e) => onLimitHours(e.target.value)}
        />
        {limitError && (
          <span className="hint err" role="alert">
            {limitError}
          </span>
        )}
      </label>
      <label className="field">
        <span>VMX quality</span>
        <select className="ctl" aria-label="VMX quality" value={value.quality} onChange={(e) => onChange({ quality: e.target.value as ShowSettings['quality'] })}>
          <option value="LQ">LQ</option>
          <option value="SQ">SQ</option>
          <option value="HQ">HQ</option>
        </select>
      </label>
      <label className="field">
        <span>Low-space warning (min)</span>
        <input
          className="ctl mono"
          aria-label="Low-space warning (min)"
          type="number"
          min={1}
          value={value.lowSpaceMinutes}
          onChange={(e) => onChange({ lowSpaceMinutes: Number(e.target.value) })}
        />
      </label>
      <div className="row-end">
        <button className="btn" onClick={run}>
          Run pre-flight
        </button>
      </div>
      {error && (
        <div className="block danger" role="alert">
          {error}
        </div>
      )}
      {preflight && (
        <div className="pf" role="status">
          <div>
            <b>{preflight.canRecord ? 'Ready to record' : 'Cannot record'}</b> · {Math.round(preflight.minutesFree)} min free ·{' '}
            {Math.round(preflight.writeMBps)} MB/s
          </div>
          {preflight.error && <div className="block danger">{preflight.error}</div>}
          {preflight.warnings.map((w) => (
            <div key={w} className="block warning">
              {w}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}