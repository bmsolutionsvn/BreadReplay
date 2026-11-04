import { fireEvent, render, screen } from '@testing-library/react';

import { ControlsBar } from './ControlsBar';

test('ControlsBar: Take sends take', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<ControlsBar client={client} lastN={12} />);
  fireEvent.click(screen.getByRole('button', { name: /^Play on air/ }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'take' });
  fireEvent.click(screen.getByRole('button', { name: /^Save last 12 s/ }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'mark.lastN', n: 12 });
});
test('ControlsBar: Mark buttons carry the target camera number', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<ControlsBar client={client} lastN={12} target={{ num: 2, color: '#e4a94b' }} />);
  expect(screen.getByRole('button', { name: /^Mark start/ })).toHaveTextContent('2');
  expect(screen.getByRole('button', { name: /^Mark end/ })).toHaveTextContent('2');
});

test('ControlsBar: buttons show their keys, remapped ones included', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(<ControlsBar client={client} lastN={12} hotkeys={{ 'mark.in': 'Q' }} />);
  expect(screen.getByRole('button', { name: /^Mark start/ })).toHaveTextContent('Mark start Q');
  expect(screen.getByRole('button', { name: /^Play on air/ })).toHaveTextContent('Enter');
});
