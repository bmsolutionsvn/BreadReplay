import { render, screen } from '@testing-library/react';

import type { Status } from '../api/types';
import { formatClockTenths } from '../format';
import { Monitors, playSeconds } from './Monitors';

const input = (id: string, name: string) => ({ id, name, state: 'live' as const, fpsIn: 60, dropped: 0, queued: 0, coverage: [] });

const status: Status = {
  recording: true,
  inputs: [input('cam1', 'Cam 1'), input('cam2', 'Cam 2')],
  disk: { freeBytes: 0, secondsLeft: null, writeMBps: 0 },
  alerts: [],
  players: {
    preview: { posUs: 0, tc: '01:14:02:11', eventId: 4, angle: 'cam2', playing: false, speed: 0.5, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 } },
    output: { posUs: 0, tc: '01:12:33:04', eventId: 3, angle: 'cam1', playing: true, speed: 0.5, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 }, remainingS: 7.4 },
  },
  events: { version: 1 },
};

test('Monitors: tags name the camera on each monitor', () => {
  render(<Monitors status={status} fps={59.94} />);
  const preview = screen.getByLabelText('Preview monitor');
  const output = screen.getByLabelText('Output monitor');
  expect(preview).toHaveTextContent('PVW');
  expect(preview).toHaveTextContent('2');
  expect(output).toHaveTextContent('PGM');
  expect(output).toHaveTextContent('1');
  expect(screen.queryByText(/only you see this/)).toBeNull();
});

test('Monitors: a mark stamp shows edge, camera and time', () => {
  const tUs = new Date(1970, 0, 1, 1, 12, 29).getTime() * 1000 + 400_000;
  render(<Monitors status={status} fps={59.94} stamp={{ edge: 'in', inputId: 'cam2', tUs }} />);
  const stamp = screen.getByTestId('mark-stamp');
  expect(stamp).toHaveTextContent('IN');
  expect(stamp).toHaveTextContent('2');
  expect(stamp).toHaveTextContent(formatClockTenths(tUs));
});

test('Monitors: Output shows the remaining countdown and a red frame', () => {
  render(<Monitors status={status} fps={59.94} />);
  expect(screen.getByTestId('remaining')).toHaveTextContent('-00:07:24');
  expect(screen.getByLabelText('Output monitor')).toHaveClass('onair');
});

test('Monitors: an idle Output shows no timecode, no countdown and an empty bar', () => {
  const idle: Status = { ...status, players: { ...status.players, output: { ...status.players.output, eventId: null, remainingS: null } } };
  render(<Monitors status={idle} fps={59.94} />);
  const out = screen.getByLabelText('Output monitor');
  expect(out).not.toHaveClass('onair');
  expect(out).toHaveTextContent('--:--:--:--');
  expect(out).toHaveTextContent('PGM');
  expect(out).not.toHaveTextContent('1');
  expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '0');
});

test('Monitors: the progress bar follows the on-air event at its speed points', () => {
  const ev = { id: 3, listId: 'g1', name: '', tags: [], angleNotes: {}, inUs: 0, outUs: 10_000_000, angles: [], speeds: [{ tUs: 0, speed: 0.5 }], zooms: [] };
  expect(playSeconds(ev, 1)).toBeCloseTo(20);
  const events = { version: 1, nextId: 4, nextListId: 2, currentList: 'g1', preRollUs: 0, postRollUs: 0, defaultSpeed: 0.5, rootOrder: [], lists: [], events: [ev] };
  const mid: Status = { ...status, players: { ...status.players, output: { ...status.players.output, remainingS: 5 } } };
  render(<Monitors status={mid} fps={59.94} events={events} />);
  expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '75');
});