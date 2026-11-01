import { render, screen } from '@testing-library/react';

import type { Status } from '../api/types';
import { StatusBar } from './StatusBar';

test('StatusBar: shows time left and a persistent disk alert', () => {
  const status = {
    recording: true,
    inputs: [{ id: 'cam1', name: 'Obs 1', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] }],
    disk: { freeBytes: 5 * 1024 ** 3, secondsLeft: 600, writeMBps: 120 },
    alerts: [{ id: 'disk-space', text: 'Disk D: 10 min left', since: 1 }],
    players: {},
    events: { version: 1 },
  } as unknown as Status;
  render(<StatusBar status={status} />);
  expect(screen.getByText('10 min left')).toBeInTheDocument();
  expect(screen.getByRole('alert')).toHaveTextContent('Disk D: 10 min left');
  expect(screen.getByText('Obs 1')).toBeInTheDocument();
});