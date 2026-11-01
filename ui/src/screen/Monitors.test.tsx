import { render, screen } from '@testing-library/react';

import type { Status } from '../api/types';
import { Monitors } from './Monitors';

const status: Status = {
  recording: true,
  inputs: [{ id: 'cam1', name: 'Obs 1', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] }],
  disk: { freeBytes: 0, secondsLeft: null, writeMBps: 0 },
  alerts: [],
  players: {
    preview: { posUs: 0, tc: '01:14:02:11', eventId: 4, angle: 'cam1', playing: false, speed: 0.5, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 } },
    output: { posUs: 0, tc: '01:12:33:04', eventId: 3, angle: 'cam1', playing: true, speed: 0.5, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 }, remainingS: 7.4 },
  },
  events: { version: 1 },
};

test('Monitors: Output shows the remaining countdown and a red frame', () => {
  render(<Monitors status={status} fps={59.94} />);
  expect(screen.getByTestId('remaining')).toHaveTextContent('-00:07:24');
  expect(screen.getByLabelText('Output monitor')).toHaveClass('onair');
  expect(screen.getByLabelText('Preview monitor')).toHaveTextContent('Obs 1');
});