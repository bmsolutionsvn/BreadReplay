import { fireEvent, render, screen } from '@testing-library/react';

import { OutputsTab } from './OutputsTab';

test('OutputsTab adds an output with the params its kind needs', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<OutputsTab client={client} outputs={[]} />);
  fireEvent.change(screen.getByLabelText('Card number'), { target: { value: '1' } });
  fireEvent.click(screen.getByRole('button', { name: 'Add output' }));
  expect(client.send).toHaveBeenCalledWith({
    cmd: 'output.add',
    id: expect.any(String),
    kind: 'decklink',
    keyed: false,
    enabled: true,
    params: { device: 1 },
  });
});