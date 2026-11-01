import { useState } from 'react';

export const HOTKEY_ROWS: Array<{ id: string; label: string }> = [
  { id: 'mark.in', label: 'Mark In' },
  { id: 'mark.out', label: 'Mark Out' },
  { id: 'mark.lastN', label: 'Last N' },
  { id: 'mark.lastN2', label: 'Last 2N' },
  { id: 'player.angle1', label: 'Angle 1' },
  { id: 'player.angle2', label: 'Angle 2' },
  { id: 'player.angle3', label: 'Angle 3' },
  { id: 'player.angle4', label: 'Angle 4' },
  { id: 'player.angle5', label: 'Angle 5' },
  { id: 'player.angle6', label: 'Angle 6' },
  { id: 'player.angle7', label: 'Angle 7' },
  { id: 'player.angle8', label: 'Angle 8' },
  { id: 'player.preset1', label: 'Speed 100%' },
  { id: 'player.preset2', label: 'Speed 75%' },
  { id: 'player.preset3', label: 'Speed 50%' },
  { id: 'player.preset4', label: 'Speed 25%' },
  { id: 'player.lock', label: 'Lock lever' },
  { id: 'take', label: 'Take' },
  { id: 'player.play', label: 'Play/pause' },
  { id: 'step.back', label: 'Step back' },
  { id: 'step.fwd', label: 'Step forward' },
  { id: 'zoom', label: 'Zoom mode' },
  { id: 'trim.in', label: 'Trim In' },
  { id: 'trim.out', label: 'Trim Out' },
];

// Spec §6.2 defaults, in the action -> key form the show file keeps.
export const defaultHotkeyKeys: Record<string, string> = {
  'mark.in': 'I',
  'mark.out': 'O',
  'mark.lastN': 'Space',
  'mark.lastN2': 'Shift+Space',
  'player.angle1': 'F1',
  'player.angle2': 'F2',
  'player.angle3': 'F3',
  'player.angle4': 'F4',
  'player.angle5': 'F5',
  'player.angle6': 'F6',
  'player.angle7': 'F7',
  'player.angle8': 'F8',
  'player.preset1': '1',
  'player.preset2': '2',
  'player.preset3': '3',
  'player.preset4': '4',
  'player.lock': 'L',
  take: 'Enter',
  'player.play': 'P',
  'step.back': '←',
  'step.fwd': '→',
  zoom: 'Z',
  'trim.in': '[',
  'trim.out': ']',
};

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