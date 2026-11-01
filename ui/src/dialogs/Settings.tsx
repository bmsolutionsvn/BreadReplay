import { useEffect, useState } from 'react';

import type { Client } from '../api/client';
import type { OutputConfig, ShowSettings, Status } from '../api/types';
import { HotkeysTab, defaultHotkeyKeys } from './HotkeysTab';
import { InputsTab } from './InputsTab';
import { OutputsTab } from './OutputsTab';
import { PlaybackTab } from './PlaybackTab';
import { RecordingTab } from './RecordingTab';

// Spec §10 defaults; the engine has no show snapshot on the wire, so the dialog starts here.
export const defaultSettings: ShowSettings = {
  rate: '60000/1001',
  outW: 1920,
  outH: 1080,
  quality: 'SQ',
  lastNSeconds: 12,
  preRollS: 0,
  postRollS: 0,
  defaultSpeed: 0.5,
  transition: 'fade',
  fadeS: 0.5,
  lowSpaceMinutes: 30,
  endAction: 'hold',
  slatePath: '',
  hotkeys: {},
  recordingFolder: '',
};

type Tab = 'inputs' | 'outputs' | 'recording' | 'playback' | 'hotkeys';
const TABS: Array<[Tab, string]> = [
  ['inputs', 'Inputs'],
  ['outputs', 'Outputs'],
  ['recording', 'Recording'],
  ['playback', 'Playback'],
  ['hotkeys', 'Hotkeys'],
];

// Settings is a dialog, not a page (spec §7). Saving sends only what the operator changed.
const DIFF_KEYS: Array<keyof ShowSettings> = [
  'rate', 'outW', 'outH', 'quality', 'lastNSeconds', 'preRollS', 'postRollS', 'defaultSpeed',
  'transition', 'fadeS', 'lowSpaceMinutes', 'endAction', 'slatePath', 'recordingFolder',
];

type Props = {
  client: Pick<Client, 'send'>;
  settings?: ShowSettings;
  status?: Status | null;
  outputs?: OutputConfig[];
  onClose: () => void;
  onHotkeys?: (keys: Record<string, string>) => void;
};

export function Settings({ client, settings = defaultSettings, status = null, outputs = [], onClose, onHotkeys }: Props) {
  const [tab, setTab] = useState<Tab>('inputs');
  const [draft, setDraft] = useState<ShowSettings>(settings);
  const [limitHours, setLimitHours] = useState(settings.limit?.hours != null ? String(settings.limit.hours) : '');
  const [hotkeys, setHotkeys] = useState<Record<string, string>>(() => ({ ...defaultHotkeyKeys, ...settings.hotkeys }));
  const [hotkeyConflict, setHotkeyConflict] = useState<string | null>(null);
  const [error, setError] = useState('');
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && onClose();
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  const limitError = limitHours.trim() !== '' && !(Number(limitHours) > 0) ? 'Recording limit must be more than 0 hours, or leave it empty for no limit' : '';
  const set = (patch: Partial<ShowSettings>) => setDraft((d) => ({ ...d, ...patch }));

  async function save() {
    if (limitError || hotkeyConflict) {
      setTab(limitError ? 'recording' : 'hotkeys');
      return;
    }
    const args: Record<string, unknown> = {};
    for (const k of DIFF_KEYS) {
      if (JSON.stringify(draft[k]) !== JSON.stringify(settings[k])) args[k] = draft[k];
    }
    const initialLimit = settings.limit?.hours != null ? String(settings.limit.hours) : '';
    if (limitHours !== initialLimit) args.limit = limitHours.trim() ? { hours: Number(limitHours) } : {};
    if (JSON.stringify(hotkeys) !== JSON.stringify({ ...defaultHotkeyKeys, ...settings.hotkeys })) args.hotkeys = hotkeys;
    if (Object.keys(args).length === 0) return onClose();
    setSaving(true);
    try {
      const reply = await client.send({ cmd: 'settings.set', ...args });
      if (!reply.ok) {
        setError(reply.error);
        return;
      }
      if (args.hotkeys) onHotkeys?.(hotkeys);
      onClose();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setSaving(false);
    }
  }

  return (
    <div className="scrim">
      <div className="dlg wide" role="dialog" aria-modal="true" aria-label="Settings">
        <div className="dlg-h">
          Settings
          <button className="x" aria-label="Close" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="dlg-b">
          <div className="cats" role="tablist" aria-label="Settings pages">
            {TABS.map(([id, label]) => (
              <button key={id} role="tab" aria-selected={tab === id} className={tab === id ? 'on' : ''} onClick={() => setTab(id)}>
                {label}
              </button>
            ))}
          </div>
          <div className="pane" role="tabpanel">
            {error && (
              <div className="block danger" role="alert">
                {error}
              </div>
            )}
            {tab === 'inputs' && <InputsTab client={client} inputs={status?.inputs ?? []} />}
            {tab === 'outputs' && <OutputsTab client={client} outputs={outputs} />}
            {tab === 'recording' && (
              <RecordingTab
                client={client}
                value={draft}
                onChange={set}
                limitHours={limitHours}
                onLimitHours={setLimitHours}
                limitError={limitError}
              />
            )}
            {tab === 'playback' && <PlaybackTab value={draft} onChange={set} />}
            {tab === 'hotkeys' && <HotkeysTab value={hotkeys} onChange={setHotkeys} onConflict={setHotkeyConflict} />}
          </div>
        </div>
        <div className="dlg-f">
          <button className="btn" onClick={onClose}>
            Cancel
          </button>
          <button className="btn primary" disabled={saving || !!limitError || !!hotkeyConflict} onClick={save}>
            Save
          </button>
        </div>
      </div>
    </div>
  );
}