import { act, render, screen } from '@testing-library/react';

import type { Status } from '../api/types';
import { resetStore, setStatus, useStatus } from './store';

function Alerts() {
  const status = useStatus();
  return (
    <ul>
      {(status?.alerts ?? []).map((a) => (
        <li key={a.id} role="alert">
          {a.text}
        </li>
      ))}
    </ul>
  );
}

const base: Status = {
  recording: true,
  inputs: [],
  disk: { freeBytes: 0, secondsLeft: null, writeMBps: 0 },
  alerts: [],
  players: {
    preview: { posUs: 0, tc: '01:00:00:00', eventId: null, angle: 'a', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 } },
    output: { posUs: 0, tc: '01:00:00:00', eventId: null, angle: 'a', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 }, remainingS: null },
  },
  events: { version: 1 },
};

beforeEach(() => resetStore());

test('an alert stays until a status without it arrives', () => {
  render(<Alerts />);
  act(() => setStatus({ ...base, alerts: [{ id: 'disk-space', text: 'Disk D: 12 min left', since: 1 }] }));
  expect(screen.getByRole('alert')).toHaveTextContent('Disk D: 12 min left');
  act(() => setStatus({ ...base, recording: true, alerts: [{ id: 'disk-space', text: 'Disk D: 12 min left', since: 1 }] }));
  expect(screen.getByRole('alert')).toBeInTheDocument();
  act(() => setStatus({ ...base, alerts: [] }));
  expect(screen.queryByRole('alert')).toBeNull();
});

test('useStatus returns the latest status', () => {
  render(<Alerts />);
  act(() => setStatus(base));
  expect(screen.queryByRole('alert')).toBeNull();
});