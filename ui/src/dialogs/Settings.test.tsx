import { fireEvent, render, screen, waitFor } from '@testing-library/react';

import { Settings } from './Settings';

function fake(reply: Record<string, unknown> = { ok: true }) {
  return { send: vi.fn().mockResolvedValue(reply) };
}

const sent = (client: ReturnType<typeof fake>) => client.send.mock.calls.map(([c]) => c);

test('Settings: saving sends settings.set with only changed fields', async () => {
  const client = fake();
  const onClose = vi.fn();
  render(<Settings client={client} onClose={onClose} />);
  fireEvent.click(screen.getByRole('tab', { name: 'Playback' }));
  fireEvent.change(screen.getByLabelText('Last N (s)'), { target: { value: '20' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save' }));
  await waitFor(() => expect(onClose).toHaveBeenCalled());
  expect(sent(client)).toEqual([{ cmd: 'settings.set', lastNSeconds: 20 }]);
});

test('Settings: a recording limit of 0 shows an inline error', () => {
  const client = fake();
  render(<Settings client={client} onClose={() => {}} />);
  fireEvent.click(screen.getByRole('tab', { name: 'Recording' }));
  fireEvent.change(screen.getByLabelText('Recording limit (hours)'), { target: { value: '0' } });
  expect(screen.getByText(/more than 0 hours/)).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Save' }));
  expect(client.send).not.toHaveBeenCalled();
});

test('Settings: Run pre-flight shows minutes free and warnings', async () => {
  const client = fake({ ok: true, canRecord: true, minutesFree: 412.4, writeMBps: 180.2, warnings: ['Disk is slower than needed'], error: null });
  render(<Settings client={client} onClose={() => {}} />);
  fireEvent.click(screen.getByRole('tab', { name: 'Recording' }));
  fireEvent.click(screen.getByRole('button', { name: 'Run pre-flight' }));
  expect(await screen.findByText(/412 min free/)).toBeInTheDocument();
  expect(screen.getByText('Disk is slower than needed')).toBeInTheDocument();
});

test('Settings: a hotkey conflict is shown inline', () => {
  const client = fake();
  render(<Settings client={client} onClose={() => {}} />);
  fireEvent.click(screen.getByRole('tab', { name: 'Hotkeys' }));
  fireEvent.change(screen.getByLabelText('Mark Out key'), { target: { value: 'I' } });
  expect(screen.getByText('Already used by Mark In')).toBeInTheDocument();
});