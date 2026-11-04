import { defaultHotkeys, hotkeyMap, keyToCommand, type HotkeyContext } from './hotkeys';

const ctx: HotkeyContext = {
  inputs: [{ id: 'cam1' }, { id: 'cam2' }, { id: 'cam3' }, { id: 'cam4' }],
  previewLocked: true,
  previewPlaying: false,
  selectedEventId: 7,
  fps: 60,
  lastNSeconds: 12,
};

function key(key: string, extra: Partial<KeyboardEvent> = {}): KeyboardEvent {
  return { key, shiftKey: false, ctrlKey: false, altKey: false, metaKey: false, target: document.body, ...extra } as KeyboardEvent;
}

test('Space sends mark.lastN', () => {
  expect(keyToCommand(key(' '), defaultHotkeys, ctx)).toEqual({ cmd: 'mark.lastN' });
});

test('F3 sets Preview angle to the third input', () => {
  expect(keyToCommand(key('F3'), defaultHotkeys, ctx)).toEqual({ cmd: 'player.angle', player: 'preview', inputId: 'cam3' });
});

test('keys are ignored in text fields', () => {
  const input = document.createElement('input');
  expect(keyToCommand(key(' ', { target: input }), defaultHotkeys, ctx)).toBeNull();
  const area = document.createElement('textarea');
  expect(keyToCommand(key('i', { target: area }), defaultHotkeys, ctx)).toBeNull();
});

test('a remapped key wins', () => {
  const map = { ...defaultHotkeys, j: { cmd: 'mark.in' }, i: { cmd: 'take' } };
  expect(keyToCommand(key('j'), map, ctx)).toEqual({ cmd: 'mark.in' });
  expect(keyToCommand(key('i'), map, ctx)).toEqual({ cmd: 'take' });
});

test('Shift+Space sends a longer Last N and the step keys scale with fps', () => {
  expect(keyToCommand(key(' ', { shiftKey: true }), defaultHotkeys, ctx)).toEqual({ cmd: 'mark.lastN', n: 24 });
  expect(keyToCommand(key('ArrowRight', { shiftKey: true }), defaultHotkeys, ctx)).toEqual({ cmd: 'player.step', frames: 60 });
  expect(keyToCommand(key('ArrowLeft'), defaultHotkeys, ctx)).toEqual({ cmd: 'player.step', frames: -1 });
});

test('L and P resolve against the Preview state', () => {
  expect(keyToCommand(key('l'), defaultHotkeys, ctx)).toEqual({ cmd: 'player.lock', locked: false });
  expect(keyToCommand(key('p'), defaultHotkeys, ctx)).toEqual({ cmd: 'player.play', player: 'preview' });
});

test('trim needs a selected event', () => {
  expect(keyToCommand(key(']'), defaultHotkeys, ctx)).toEqual({ cmd: 'event.trim', id: 7, outFrames: 1 });
  expect(keyToCommand(key(']'), defaultHotkeys, { ...ctx, selectedEventId: null })).toBeNull();
});

test('Ctrl+A selects all even when a list row has focus; Space on a focused button does not mark', () => {
  const button = document.createElement('button');
  expect(keyToCommand(new KeyboardEvent('keydown', { key: 'a', ctrlKey: true }), hotkeyMap())).toEqual({ cmd: 'ui.selectAll' });
  const onButton = (init: KeyboardEventInit) => { const e = new KeyboardEvent('keydown', init); Object.defineProperty(e, 'target', { value: button }); return e; };
  expect(keyToCommand(onButton({ key: 'a', ctrlKey: true }), hotkeyMap())).toEqual({ cmd: 'ui.selectAll' });
  expect(keyToCommand(onButton({ key: ' ' }), hotkeyMap())).toBeNull();
});

test('a key without a mapping returns null', () => {
  expect(keyToCommand(key('q'), defaultHotkeys, ctx)).toBeNull();
});

test('a saved remap moves Mark In and frees the old key', () => {
  const map = hotkeyMap({ 'mark.in': 'J' });
  expect(keyToCommand(key('j'), map, ctx)).toEqual({ cmd: 'mark.in' });
  expect(keyToCommand(key('i'), map, ctx)).toBeNull();
  // Actions the operator did not touch keep their defaults.
  expect(keyToCommand(key('o'), map, ctx)).toEqual({ cmd: 'mark.out' });
  // Keys with no editable row, like the 1-second step, survive a remap map.
  expect(keyToCommand(key('ArrowLeft', { shiftKey: true }), hotkeyMap(), ctx)).toEqual({ cmd: 'player.step', frames: -60 });
});