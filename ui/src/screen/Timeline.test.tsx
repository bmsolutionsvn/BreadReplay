import { fireEvent, render, screen, within } from '@testing-library/react';

import type { EventStore, InputStatus, Status } from '../api/types';
import { Timeline } from './Timeline';

const input = (id: string, name: string, coverage: Array<[number, number]>): InputStatus => ({
  id,
  name,
  state: 'live',
  fpsIn: 60,
  dropped: 0,
  queued: 0,
  coverage,
});

const events: EventStore = {
  version: 1,
  nextId: 3,
  nextListId: 2,
  currentList: 'g1',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  rootOrder: [],
  lists: [{ id: 'g1', name: 'Game 1', order: [1, 2] }],
  events: [
    {
      id: 1,
      listId: 'g1',
      name: 'Fight',
      tags: [],
      angleNotes: {},
      inUs: 0,
      outUs: 10_000_000,
      angles: [
        { tUs: 0, inputId: 'c1' },
        { tUs: 4_000_000, inputId: 'c2' },
        { tUs: 7_000_000, inputId: 'c3' },
      ],
      speeds: [{ tUs: 0, speed: 1 }],
      zooms: [],
    },
    {
      id: 2,
      listId: 'g1',
      name: 'Goal',
      tags: [],
      angleNotes: {},
      inUs: 12_000_000,
      outUs: 15_000_000,
      angles: [{ tUs: 12_000_000, inputId: 'c1' }],
      speeds: [{ tUs: 12_000_000, speed: 1 }],
      zooms: [],
    },
  ],
};

const status = (previewPos = 0, outputPos = 0, onAir = false): Status => ({
  recording: false,
  inputs: [
    input('c1', 'Cam 1', [[0, 15_000_000]]),
    input('c2', 'Cam 2', [[0, 15_000_000]]),
    input('c3', 'Cam 3', [[0, 15_000_000]]),
  ],
  disk: { freeBytes: 0, secondsLeft: null, writeMBps: 0 },
  alerts: [],
  players: {
    preview: {
      posUs: previewPos,
      tc: '00:00:00:00',
      eventId: null,
      angle: 'c1',
      playing: false,
      speed: 1,
      locked: true,
      zoom: { cx: 0.5, cy: 0.5, scale: 1 },
    },
    output: {
      posUs: outputPos,
      tc: '00:00:00:00',
      eventId: onAir ? 1 : null,
      angle: onAir ? 'c1' : '',
      playing: onAir,
      speed: 1,
      locked: true,
      zoom: { cx: 0.5, cy: 0.5, scale: 1 },
      remainingS: null,
    },
  },
  events: { version: 1 },
});

function renderTimeline(over: Partial<Parameters<typeof Timeline>[0]> = {}) {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  const noop = () => {};
  const view = render(
    <Timeline
      client={client}
      events={events}
      selectedId={1}
      selectedIds={[1]}
      onSelect={noop}
      selectedBlock={null}
      onSelectBlock={noop}
      fitKey={0}
      status={status()}
      width={1000}
      {...over}
    />,
  );
  const section = view.container.querySelector('section.tl') as HTMLElement;
  return { client, section, ...view };
}

test('Timeline: one track per camera, each block on its camera', () => {
  renderTimeline();
  expect(within(screen.getByTestId('track-c2')).getByTestId('block-1')).toBeInTheDocument();
  expect(within(screen.getByTestId('track-c3')).getByTestId('block-2')).toBeInTheDocument();
});

test('Timeline: dragging Out sends one event.update on release', () => {
  const { client, section } = renderTimeline();
  fireEvent.mouseDown(screen.getByTestId('handle-out'), { clientX: 1000 });
  fireEvent.mouseMove(section, { clientX: 900 });
  expect(client.send).not.toHaveBeenCalled();
  fireEvent.mouseUp(section, { clientX: 900 });
  expect(client.send).toHaveBeenCalledTimes(1);
  expect(client.send).toHaveBeenCalledWith(expect.objectContaining({ cmd: 'event.update', id: 1, outUs: 9_000_000 }));
});

test('Timeline: dragging a block to another track changes its camera', () => {
  const { client } = renderTimeline();
  const block = screen.getByTestId('block-2');
  const target = screen.getByTestId('track-c1');
  fireEvent.mouseDown(block, { clientX: 850, clientY: 100 });
  fireEvent.mouseMove(target, { clientX: 850, clientY: 140 });
  fireEvent.mouseUp(target, { clientX: 850, clientY: 140 });
  expect(client.send).toHaveBeenCalledTimes(1);
  const sent = client.send.mock.calls[0][0];
  expect(sent.cmd).toBe('event.update');
  expect(sent.angles[2].inputId).toBe('c1');
});

test('Timeline: dragging a switch edge moves that cut', () => {
  const { client, section } = renderTimeline();
  fireEvent.mouseDown(screen.getByTestId('switch-1'), { clientX: 400 });
  fireEvent.mouseMove(section, { clientX: 450 });
  fireEvent.mouseUp(section, { clientX: 450 });
  const sent = client.send.mock.calls.at(-1)?.[0];
  expect(sent.angles[1].tUs).toBe(4_500_000);
});

test('Timeline: Shift turns snapping off', () => {
  const snapping = renderTimeline({ status: status(4_520_000) });
  fireEvent.mouseDown(screen.getByTestId('switch-1'), { clientX: 400 });
  fireEvent.mouseMove(snapping.section, { clientX: 452 });
  fireEvent.mouseUp(snapping.section, { clientX: 452 });
  expect(snapping.client.send.mock.calls.at(-1)?.[0].angles[1].tUs).toBe(4_520_000);
  snapping.unmount();

  const free = renderTimeline({ status: status(4_520_000) });
  fireEvent.mouseDown(screen.getByTestId('switch-1'), { clientX: 400 });
  fireEvent.mouseMove(free.section, { clientX: 452, shiftKey: true });
  fireEvent.mouseUp(free.section, { clientX: 452, shiftKey: true });
  expect(free.client.send.mock.calls.at(-1)?.[0].angles[1].tUs).toBe(4_500_000);
});

test('Timeline: wheel pans by a tenth and Ctrl+wheel zooms at the pointer', () => {
  const panned = renderTimeline();
  fireEvent.wheel(panned.section, { deltaY: 100 });
  expect(Number(panned.section.dataset.from)).toBe(1_000_000);
  panned.unmount();

  const zoomed = renderTimeline();
  fireEvent.wheel(zoomed.section, { deltaY: -100, ctrlKey: true, clientX: 500 });
  const from = Number(zoomed.section.dataset.from);
  const to = Number(zoomed.section.dataset.to);
  expect(to - from).toBe(8_000_000);
  expect((from + to) / 2).toBe(5_000_000);
});

test('Timeline: playheads take the tally colours', () => {
  renderTimeline({ status: status(3_000_000, 2_000_000, true) });
  expect(screen.getByTestId('ph-pvw')).toHaveClass('pvw');
  expect(screen.getByTestId('ph-pgm')).toHaveClass('pgm');
});

test('Timeline: the play-order strip reorders with event.move', () => {
  const { client } = renderTimeline({ selectedIds: [1, 2] });
  fireEvent.mouseDown(screen.getByTestId('order-2'));
  fireEvent.mouseUp(screen.getByTestId('order-1'));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'event.move', ids: [2], listId: 'g1', beforeId: 1 });
});