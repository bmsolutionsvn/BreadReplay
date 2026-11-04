import { fireEvent, render, screen } from '@testing-library/react';

import type { EventStore } from './api/types';
import { ContextMenuHost } from './ContextMenu';
import { EventList } from './screen/EventList';

const events: EventStore = {
  version: 1,
  nextId: 3,
  nextListId: 2,
  currentList: 'g1',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  rootOrder: [],
  lists: [{ id: 'g1', name: 'Game 1', order: [2] }],
  events: [{ id: 2, listId: 'g1', name: '', tags: [], angleNotes: {}, inUs: 0, outUs: 12_000_000, angles: [], speeds: [], zooms: [] }],
};

function setup() {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(
    <ContextMenuHost>
      <EventList client={client} events={events} selectedId={null} selectedIds={[]} onSelect={() => {}} />
      <input aria-label="Name" />
    </ContextMenuHost>,
  );
  return client;
}

test('ContextMenu: the browser menu never shows outside text fields', () => {
  setup();
  const onList = new MouseEvent('contextmenu', { bubbles: true, cancelable: true });
  screen.getByLabelText('Event list').dispatchEvent(onList);
  expect(onList.defaultPrevented).toBe(true);
  const onField = new MouseEvent('contextmenu', { bubbles: true, cancelable: true });
  screen.getByLabelText('Name').dispatchEvent(onField);
  expect(onField.defaultPrevented).toBe(false);
});

test('ContextMenu: right-click an event, Delete sends event.delete and closes', () => {
  const client = setup();
  fireEvent.contextMenu(screen.getByRole('button', { name: /^2 · / }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Delete replay 2' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'event.delete', id: 2 });
  expect(screen.queryByRole('menu')).not.toBeInTheDocument();
});

test('ContextMenu: keys go to the menu, never to the operator hotkeys (Enter = Take)', () => {
  const client = setup();
  const hotkey = vi.fn();
  window.addEventListener('keydown', hotkey);
  fireEvent.contextMenu(screen.getByRole('button', { name: /^2 · / }));
  fireEvent.keyDown(window, { key: 'Enter' });
  expect(hotkey).not.toHaveBeenCalled();
  fireEvent.keyDown(window, { key: 'ArrowDown' });
  fireEvent.keyDown(window, { key: 'Enter' });
  expect(client.send).toHaveBeenCalledWith({ cmd: 'player.cue', player: 'preview', eventId: 2 });
  expect(screen.queryByRole('menu')).not.toBeInTheDocument();
  fireEvent.keyDown(window, { key: 'i' });
  expect(hotkey).toHaveBeenCalledTimes(1);
  window.removeEventListener('keydown', hotkey);
});
