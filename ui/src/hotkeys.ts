import type { Command } from './api/client';

export interface HotkeyContext {
  inputs?: Array<{ id: string }>;
  previewLocked?: boolean;
  previewPlaying?: boolean;
  selectedEventId?: number | null;
  fps?: number;
  lastNSeconds?: number;
}

// The actions the Settings dialog can remap, and their spec §6.2 default keys.
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

// Spec §6.2 defaults. Values the engine needs from the running show (input ids, lock state,
// fps) are placeholders resolved by keyToCommand.
export const defaultHotkeys: Record<string, Command> = {
  i: { cmd: 'mark.in' },
  o: { cmd: 'mark.out' },
  space: { cmd: 'mark.lastN' },
  'shift+space': { cmd: 'mark.lastN', n: 24 }, // the default Last N is 12 s
  f1: { cmd: 'player.angle', player: 'preview', input: 1 },
  f2: { cmd: 'player.angle', player: 'preview', input: 2 },
  f3: { cmd: 'player.angle', player: 'preview', input: 3 },
  f4: { cmd: 'player.angle', player: 'preview', input: 4 },
  f5: { cmd: 'player.angle', player: 'preview', input: 5 },
  f6: { cmd: 'player.angle', player: 'preview', input: 6 },
  f7: { cmd: 'player.angle', player: 'preview', input: 7 },
  f8: { cmd: 'player.angle', player: 'preview', input: 8 },
  '1': { cmd: 'player.preset', speed: 1 },
  '2': { cmd: 'player.preset', speed: 0.75 },
  '3': { cmd: 'player.preset', speed: 0.5 },
  '4': { cmd: 'player.preset', speed: 0.25 },
  l: { cmd: 'player.lock', locked: 'toggle' },
  enter: { cmd: 'take' },
  p: { cmd: 'player.toggle' },
  left: { cmd: 'player.step', frames: -1 },
  right: { cmd: 'player.step', frames: 1 },
  'shift+left': { cmd: 'player.step', frames: -1, seconds: 1 },
  'shift+right': { cmd: 'player.step', frames: 1, seconds: 1 },
  z: { cmd: 'ui.zoomMode' }, // the screen consumes this; the engine has no zoom-mode command
  '[': { cmd: 'event.trim', inFrames: 1 },
  ']': { cmd: 'event.trim', outFrames: 1 },
};

function comboOf(key: string): string {
  const parts = key.trim().toLowerCase().split('+').map((p) => p.trim());
  const last = parts.pop() ?? '';
  const base = last === ' ' || last === 'space' ? 'space' : last === '←' ? 'left' : last === '→' ? 'right' : last;
  return parts.length ? `${parts.join('+')}+${base}` : base;
}

// The show file saves action -> key; the screen needs key -> command. Actions the
// operator did not remap fall back to their spec §6.2 default.
export function hotkeyMap(saved: Record<string, string> = {}): Record<string, Command> {
  // Start from every default so keys with no editable row (Shift+arrow) survive.
  const out = { ...defaultHotkeys };
  for (const row of HOTKEY_ROWS) {
    const def = defaultHotkeyKeys[row.id];
    if (!def) continue;
    const defKey = comboOf(def);
    const want = comboOf(saved[row.id] ?? def);
    if (want === defKey) continue;
    const command = defaultHotkeys[defKey];
    delete out[defKey];
    if (want && command) out[want] = command;
  }
  return out;
}

function combo(e: KeyboardEvent): string {
  const key = e.key === ' ' ? 'space' : e.key.toLowerCase().replace(/^arrow/, '');
  return e.shiftKey ? `shift+${key}` : key;
}

function isTyping(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  return target.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT', 'BUTTON'].includes(target.tagName);
}

export function keyToCommand(e: KeyboardEvent, map: Record<string, Command> = defaultHotkeys, ctx: HotkeyContext = {}): Command | null {
  if (e.ctrlKey || e.altKey || e.metaKey || isTyping(e.target)) return null;
  const found = map[combo(e)];
  if (!found) return null;
  const cmd: Command = { ...found };

  if (cmd.cmd === 'player.angle' && typeof cmd.input === 'number') {
    const id = ctx.inputs?.[cmd.input - 1]?.id;
    delete cmd.input;
    if (!id) return null;
    cmd.inputId = id;
  }
  if (cmd.cmd === 'player.lock' && cmd.locked === 'toggle') cmd.locked = !ctx.previewLocked;
  if (cmd.cmd === 'player.toggle') {
    cmd.cmd = ctx.previewPlaying ? 'player.pause' : 'player.play';
    cmd.player = cmd.player ?? 'preview';
  }
  if (cmd.cmd === 'player.step' && typeof cmd.seconds === 'number') {
    const sign = (cmd.frames as number) < 0 ? -1 : 1;
    cmd.frames = sign * Math.round((ctx.fps ?? 60) * cmd.seconds);
    delete cmd.seconds;
  }
  if (cmd.cmd === 'event.trim') {
    if (ctx.selectedEventId == null) return null;
    cmd.id = ctx.selectedEventId;
  }
  return cmd;
}
// The key shown beside a button or menu item: the operator's remap, else the default.
export function keyLabel(id: string, saved: Record<string, string> = {}): string {
  return saved[id] ?? defaultHotkeyKeys[id] ?? '';
}
