import { fireEvent, render, screen } from '@testing-library/react';

import type { InputStatus } from '../api/types';
import { InputsTab } from './InputsTab';

const input: InputStatus = { id: 'cam1', name: 'Obs 1', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] };

test('InputsTab lists the configured inputs and adds one', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<InputsTab client={client} inputs={[input]} />);
  expect(screen.getByText('Obs 1')).toBeInTheDocument();
  fireEvent.change(screen.getByLabelText('New input name'), { target: { value: 'Obs 2' } });
  fireEvent.change(screen.getByLabelText('Stream address'), { target: { value: 'srt://10.0.0.2:9000' } });
  fireEvent.click(screen.getByRole('button', { name: 'Add camera' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'input.add', id: expect.any(String), name: 'Obs 2', kind: 'srt', params: { url: 'srt://10.0.0.2:9000' } });
  fireEvent.click(screen.getByRole('button', { name: 'Remove Obs 1' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'input.remove', id: 'cam1' });
});