import { useState } from 'react';

import { HOTKEY_ROWS, defaultHotkeyKeys } from '../hotkeys';

export { defaultHotkeyKeys };

function conflictOf(keys: Record<string, string>): string | null {
  const norm = (k: string) => k.trim().toLowerCase();
  for (let i = 0; i < HOTKEY_ROWS.length; i++) {
    const a = keys[HOTKEY_ROWS[i].id] ?? '';
    if (!a.trim()) continue;
    const other = HOTKEY_ROWS.slice(i + 1).find((r) => norm(keys[r.id] ?? '') === norm(a));
    if (other) return `Already used by ${HOTKEY_ROWS[i].label}`;
  }
  return null;
}

type Props = {
  value: Record<string, string>;
  onChange: (keys: Record<string, string>) => void;
  onConflict: (message: string | null) => void;
};

export function HotkeysTab({ value, onChange, onConflict }: Props) {
  const [local, setLocal] = useState(value);
  const conflict = conflictOf(local);

  function change(id: string, key: string) {
    const next = { ...local, [id]: key };
    setLocal(next);
    onChange(next);
    onConflict(conflictOf(next));
  }

  return (
    <div className="tab-hotkeys">
      <h3>Hotkeys</h3>
      <p className="note">Every key is remappable. A key can only do one thing.</p>
      {conflict && (
        <div className="block danger" role="alert">
          {conflict}
        </div>
      )}
      <div className="hk-list">
        {HOTKEY_ROWS.map((r) => (
          <label className="field hk" key={r.id}>
            <span>{r.label}</span>
            <input className="ctl mono" aria-label={`${r.label} key`} value={local[r.id] ?? ''} onChange={(e) => change(r.id, e.target.value)} />
          </label>
        ))}
      </div>
    </div>
  );
}