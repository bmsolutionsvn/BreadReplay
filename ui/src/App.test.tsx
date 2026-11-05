import { act, fireEvent, render, screen } from '@testing-library/react';

import { App } from './App';
import type { Client } from './api/client';
import type { EventStore, Status } from './api/types';

function fakeClient() {
  let onEvents: (e: EventStore) => void = () => {};
  let onStatus: (s: Status) => void = () => {};
  const client = {
    send: vi.fn().mockResolvedValue({ ok: true }),
    onStatus: (fn: (s: Status) => void) => {
      onStatus = fn;
      return () => {};
    },
    onEvents: (fn: (e: EventStore) => void) => {
      onEvents = fn;
      return () => {};
    },
    onPreview: () => () => {},
    onAngleRow: () => () => {},
    wantPreviews: vi.fn().mockResolvedValue(undefined),
  };
  return {
    client: client as unknown as Client,
    push: (e: EventStore) => act(() => onEvents(e)),
    pushStatus: (s: Status) => act(() => onStatus(s)),
  };
}

const status = (previewPos = 0, edit?: Status['edit']): Status => ({
  recording: false,
  inputs: [
    { id: 'c1', name: 'Cam 1', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] },
    { id: 'c2', name: 'Cam 2', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] },
  ],
  disk: { freeBytes: 0, secondsLeft: null, writeMBps: 0 },
  alerts: [],
  players: {
    preview: { posUs: previewPos, tc: '00:00:00:00', eventId: null, angle: 'c1', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 } },
    output: { posUs: 0, tc: '00:00:00:00', eventId: null, angle: '', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 }, remainingS: null },
  },
  events: { version: 1 },
  edit,
});

const ev = (id: number) => ({ id, listId: 'g1', name: '', tags: [], angleNotes: {}, inUs: id * 60e6, outUs: id * 60e6 + 12e6, angles: [], speeds: [], zooms: [] });
const store = (ids: number[]): EventStore => ({
  version: ids.length,
  nextId: ids.length + 1,
  nextListId: 2,
  currentList: 'g1',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  rootOrder: [],
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

test('App: Delete with two replays selected deletes both in one command', () => {
  const { client, push, pushStatus } = fakeClient();
  render(<App client={client} />);
  push(store([1, 3]));
  pushStatus(status());
  fireEvent.click(screen.getByRole('button', { name: /^1 · / }), { ctrlKey: true });
  fireEvent.keyDown(window, { key: 'Delete' });
  expect(client.send).toHaveBeenCalledWith({ cmd: 'event.delete', ids: [1, 3] });
});

test('App: S splits the primary replay at the Preview playhead', () => {
  const { client, push, pushStatus } = fakeClient();
  render(<App client={client} />);
  const fight: EventStore = {
    ...store([1]),
    events: [
      {
        id: 1,
        listId: 'g1',
        name: '',
        tags: [],
        angleNotes: {},
        inUs: 0,
        outUs: 10_000_000,
        angles: [
          { tUs: 0, inputId: 'c1' },
          { tUs: 4_000_000, inputId: 'c2' },
        ],
        speeds: [{ tUs: 0, speed: 1 }],
        zooms: [],
      },
    ],
  };
  push(fight);
  pushStatus(status(5_000_000));
  fireEvent.keyDown(window, { key: 's' });
  expect(client.send).toHaveBeenCalledTimes(1);
  const sent = (client.send as unknown as ReturnType<typeof vi.fn>).mock.calls[0][0];
  expect(sent.cmd).toBe('event.update');
  expect(sent.id).toBe(1);
  expect(sent.angles.some((a: { tUs: number; inputId: string }) => a.inputId === 'c2' && Math.abs(a.tUs - 5_000_000) < 20_000)).toBe(true);
});

test('App: S splits on the show frame grid', () => {
  const { client, push, pushStatus } = fakeClient();
  render(<App client={client} />);
  const fight: EventStore = {
    ...store([1]),
    events: [
      {
        id: 1,
        listId: 'g1',
        name: '',
        tags: [],
        angleNotes: {},
        inUs: 0,
        outUs: 10_000_000,
        angles: [
          { tUs: 0, inputId: 'c1' },
          { tUs: 4_000_000, inputId: 'c2' },
        ],
        speeds: [{ tUs: 0, speed: 1 }],
        zooms: [],
      },
    ],
  };
  push(fight);
  pushStatus({ ...status(4_031_000), rate: { num: 50, den: 1 } });
  fireEvent.keyDown(window, { key: 's' });
  const sent = (client.send as unknown as ReturnType<typeof vi.fn>).mock.calls[0][0];
  expect(sent.cmd).toBe('event.update');
  expect(sent.angles.some((a: { tUs: number }) => a.tUs === 4_040_000)).toBe(true);
});

test('App: S on an existing cut sends nothing', () => {
  const { client, push, pushStatus } = fakeClient();
  render(<App client={client} />);
  const fight: EventStore = {
    ...store([1]),
    events: [
      {
        id: 1,
        listId: 'g1',
        name: '',
        tags: [],
        angleNotes: {},
        inUs: 0,
        outUs: 10_000_000,
        angles: [
          { tUs: 0, inputId: 'c1' },
          { tUs: 4_000_000, inputId: 'c2' },
        ],
        speeds: [{ tUs: 0, speed: 1 }],
        zooms: [],
      },
    ],
  };
  push(fight);
  pushStatus({ ...status(4_000_000), rate: { num: 50, den: 1 } });
  fireEvent.keyDown(window, { key: 's' });
  expect(client.send).not.toHaveBeenCalled();
});

test('App: Undo is disabled until the engine says it can undo', () => {
  const { client, pushStatus } = fakeClient();
  render(<App client={client} />);
  expect(screen.getByRole('button', { name: 'Undo' })).toBeDisabled();
  pushStatus(status(0, { canUndo: true, canRedo: false }));
  expect(screen.getByRole('button', { name: 'Undo' })).toBeEnabled();
});
