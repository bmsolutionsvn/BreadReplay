import { fireEvent, render, screen } from '@testing-library/react';

import type { EventStore } from '../api/types';
import { EventList } from './EventList';

const local = (h: number, m: number, s: number) => new Date(1970, 0, 1, h, m, s).getTime() * 1000;

const events: EventStore = {
  version: 1,
  nextId: 4,
  nextListId: 2,
  currentList: 'g1',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  lists: [{ id: 'g1', name: 'Game 3', order: [1, 2, 3] }],
  events: [
    { id: 1, listId: 'g1', name: '', tags: [], angleNotes: {}, inUs: local(1, 5, 10), outUs: local(1, 5, 22), angles: [], speeds: [], zooms: [] },
    { id: 3, listId: 'g1', name: '', tags: [], angleNotes: {}, inUs: local(1, 12, 29), outUs: local(1, 12, 41), angles: [], speeds: [], zooms: [] },
  ],
};

test('EventList: Highlight plays the current list', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<EventList client={client} events={events} selectedId={3} onSelect={() => {}} />);
  expect(screen.getByText('Replays · Game 3')).toBeInTheDocument();
  expect(screen.getByRole('button', { name: '3 · 01:12:29 · 0:12' })).toHaveClass('sel');
  fireEvent.click(screen.getByRole('button', { name: 'Play all replays (Game 3)' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'highlight.play', listId: 'g1' });
});