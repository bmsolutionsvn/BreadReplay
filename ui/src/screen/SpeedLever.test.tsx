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