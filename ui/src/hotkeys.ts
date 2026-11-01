import type { Command } from './api/client';

export interface HotkeyContext {
  inputs?: Array<{ id: string }>;
  previewLocked?: boolean;
  previewPlaying?: boolean;
  selectedEventId?: number | null;
  fps?: number;
  lastNSeconds?: number;
}

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