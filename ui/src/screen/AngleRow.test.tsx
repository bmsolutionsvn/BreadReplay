import { fireEvent, render, screen } from '@testing-library/react';

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

test('AngleRow: clicking a tile sends player.angle for preview', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<AngleRow client={client} status={status} />);
  fireEvent.click(screen.getByRole('button', { name: 'Angle Obs 3' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'player.angle', player: 'preview', inputId: 'cam3' });
  expect(screen.getByText('All cameras at 01:14:02:11')).toBeInTheDocument();
});