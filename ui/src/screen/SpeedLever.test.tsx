import { fireEvent, render, screen } from '@testing-library/react';

import type { Status } from '../api/types';
import { SpeedLever } from './SpeedLever';

function fake() {
  return { send: vi.fn().mockResolvedValue({ ok: true }) };
}

test('SpeedLever: moving the lever sends player.lever', () => {
  const client = fake();
  const status = { players: { preview: { speed: 0.5, locked: false } } } as unknown as Status;
  render(<SpeedLever client={client} status={status} />);
  expect(screen.getByText('50%')).toBeInTheDocument();
  fireEvent.change(screen.getByLabelText('Speed lever'), { target: { value: '0.75' } });
  expect(client.send).toHaveBeenCalledWith({ cmd: 'player.lever', player: 'preview', speed: 0.75 });
});
test('SpeedLever: 0–100 % only, and a locked lever cannot be dragged', () => {
  const client = fake();
  const status = { players: { preview: { speed: 1, locked: true } } } as unknown as Status;
  render(<SpeedLever client={client} status={status} />);
  const lever = screen.getByLabelText('Speed lever');
  expect(lever).toHaveAttribute('max', '1');
  expect(lever).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: '50' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'player.preset', player: 'preview', speed: 0.5 });
});
