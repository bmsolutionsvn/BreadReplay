import { fireEvent, render, screen } from '@testing-library/react';

import type { EventStore } from '../api/types';
import { Timeline, timeToX, xToTime } from './Timeline';

const view = { fromUs: 0, toUs: 10_000_000 };

const events: EventStore = {
  version: 1,
  nextId: 2,
  nextListId: 2,
  currentList: 'g1',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  lists: [{ id: 'g1', name: 'Game 1', order: [1] }],
  events: [
    {
      id: 1,
      listId: 'g1',
      name: '',
      tags: [],
      angleNotes: {},
      inUs: 2_000_000,
      outUs: 9_000_000,
      angles: [
        { tUs: 2_000_000, inputId: 'cam1' },
        { tUs: 5_000_000, inputId: 'cam2' },
      ],
      speeds: [{ tUs: 2_000_000, speed: 0.5 }],
      zooms: [],
    },
  ],
};

test('Timeline: time maps to x and back within one pixel', () => {
  for (const x of [0, 137, 500, 901, 1000]) {
    expect(timeToX(xToTime(x, view, 1000), view, 1000)).toBeCloseTo(x, 0);
  }
  expect(timeToX(5_000_000, view, 1000)).toBe(500);
  expect(xToTime(500, view, 1000)).toBe(5_000_000);
});

test('Timeline: dragging an angle block sends event.update', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  const { container } = render(<Timeline client={client} events={events} selectedId={1} view={view} width={1000} />);
  const block = screen.getByTestId('angle-block-1');
  fireEvent.mouseDown(block, { clientX: 500 });
  fireEvent.mouseMove(container.querySelector('[data-testid="timeline-track"]')!, { clientX: 600 });
  fireEvent.mouseUp(container.querySelector('[data-testid="timeline-track"]')!, { clientX: 600 });
  expect(client.send).toHaveBeenCalledWith({
    cmd: 'event.update',
    id: 1,
    angles: [
      { tUs: 2_000_000, inputId: 'cam1' },
      { tUs: 6_000_000, inputId: 'cam2' },
    ],
  });
});