import { fireEvent, render, screen } from '@testing-library/react';

import { ControlsBar } from './ControlsBar';

test('ControlsBar: Take sends take', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<ControlsBar client={client} lastN={12} />);
  fireEvent.click(screen.getByRole('button', { name: 'Take ▶' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'take' });
  fireEvent.click(screen.getByRole('button', { name: 'Last 12 s' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'mark.lastN', n: 12 });
});