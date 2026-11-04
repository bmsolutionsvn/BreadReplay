import { fireEvent, render, screen, within } from '@testing-library/react';

import type { Status } from '../api/types';
import { AngleRow } from './AngleRow';

const status = {
  inputs: [
    { id: 'cam1', name: 'Obs 1', state: 'live' },
    { id: 'cam2', name: 'Obs 2', state: 'live' },
    { id: 'cam3', name: 'Obs 3', state: 'offline' },
  ],
  players: { preview: { posUs: 0, tc: '01:14:02:11', angle: 'cam3' } },
} as unknown as Status;

const two: Status = {
  recording: true,
  inputs: [
    { id: 'cam1', name: 'Cam 1', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] },
    { id: 'cam2', name: 'Cam 2', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] },
  ],
  disk: { freeBytes: 0, secondsLeft: null, writeMBps: 0 },
  alerts: [],
  players: {
    preview: { posUs: 0, tc: '01:00:00:00', eventId: null, angle: 'cam2', playing: false, speed: 0.5, locked: false, zoom: { cx: 0.5, cy: 0.5, scale: 1 } },
    output: { posUs: 0, tc: '01:00:00:00', eventId: 1, angle: 'cam1', playing: true, speed: 1, locked: false, zoom: { cx: 0.5, cy: 0.5, scale: 1 }, remainingS: 2 },
  },
  events: { version: 1 },
};

test('AngleRow: the mark target is framed and the others dim', () => {
  render(<AngleRow client={{ send: vi.fn() }} status={two} />);
  expect(screen.getByRole('button', { name: 'Angle Cam 2' })).toHaveClass('target');
  expect(screen.getByRole('button', { name: 'Angle Cam 1' })).toHaveClass('dim');
});

test('AngleRow: tally chips sit on their cameras', () => {
  render(<AngleRow client={{ send: vi.fn() }} status={two} />);
  expect(within(screen.getByRole('button', { name: 'Angle Cam 1' })).getByText('PGM')).toBeInTheDocument();
  expect(within(screen.getByRole('button', { name: 'Angle Cam 2' })).getByText('PVW')).toBeInTheDocument();
});

test('AngleRow: clicking a tile sends player.angle for preview', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<AngleRow client={client} status={status} />);
  fireEvent.click(screen.getByRole('button', { name: 'Angle Obs 3' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'player.angle', player: 'preview', inputId: 'cam3' });
  expect(screen.getByText('All cameras at 01:14:02:11')).toBeInTheDocument();
});