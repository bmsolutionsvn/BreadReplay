import { act, render, screen } from '@testing-library/react';

import { App } from './App';
import type { Client } from './api/client';
import type { EventStore } from './api/types';

function fakeClient() {
  let onEvents: (e: EventStore) => void = () => {};
  const client = {
    send: vi.fn().mockResolvedValue({ ok: true }),
    onStatus: () => () => {},
    onEvents: (fn: (e: EventStore) => void) => {
      onEvents = fn;
      return () => {};
    },
    onPreview: () => () => {},
    onAngleRow: () => () => {},
  };
  return { client: client as unknown as Client, push: (e: EventStore) => act(() => onEvents(e)) };
}

const ev = (id: number) => ({ id, listId: 'g1', name: '', tags: [], angleNotes: {}, inUs: id * 60e6, outUs: id * 60e6 + 12e6, angles: [], speeds: [], zooms: [] });
const store = (ids: number[]): EventStore => ({
  version: ids.length,
  nextId: ids.length + 1,
  nextListId: 2,
  currentList: 'g1',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  lists: [{ id: 'g1', name: 'Game 1', order: ids }],
  events: ids.map(ev),
});

test('App: the title bar is the first element of the page', () => {
  const { client } = fakeClient();
  const { container } = render(<App client={client} />);
  expect(container.querySelector('.app')!.firstElementChild).toHaveClass('titlebar');
});

test('App: a newly marked event becomes the selected one', () => {
  const { client, push } = fakeClient();
  render(<App client={client} />);
  push(store([1, 2]));
  expect(screen.getByRole('button', { name: /^2 · / })).toHaveClass('sel');
  act(() => screen.getByRole('button', { name: /^1 · / }).click());
  expect(screen.getByRole('button', { name: /^1 · / })).toHaveClass('sel');
  push(store([1, 2, 3]));
  expect(screen.getByRole('button', { name: /^3 · / })).toHaveClass('sel');
});
