import { fireEvent, render, screen, within } from '@testing-library/react';

import type { Status } from '../api/types';
import { StatusBar } from './StatusBar';

test('StatusBar: shows time left and a persistent disk alert', () => {
  const status = {
    recording: true,
    inputs: [{ id: 'cam1', name: 'Obs 1', state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] }],
    disk: { freeBytes: 5 * 1024 ** 3, secondsLeft: 600, writeMBps: 120 },
    alerts: [{ id: 'disk-space', text: 'Disk D: 10 min left', since: 1 }],
    players: {},
    events: { version: 1 },
  } as unknown as Status;
  render(<StatusBar status={status} />);
  expect(screen.getByText('room for 10 min more')).toBeInTheDocument();
  expect(screen.getByRole('alert')).toHaveTextContent('Disk D: 10 min left');
  expect(screen.getByText('Obs 1')).toBeInTheDocument();
});

const baseStatus = (exports: NonNullable<Status['exports']>) =>
  ({
    recording: true,
    inputs: [],
    disk: { freeBytes: 5 * 1024 ** 3, secondsLeft: 600, writeMBps: 120 },
    alerts: [],
    players: {},
    events: { version: 1 },
    exports,
  }) as unknown as Status;

test('StatusBar: a running export shows progress and cancels', () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  render(
    <StatusBar
      status={baseStatus([{ id: 7, name: 'Game 1', state: 'running', progress: 0.42, error: '', path: '' }])}
      client={client}
    />,
  );
  expect(screen.getByText('Exporting Game 1 · 42 %')).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Cancel export' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'export.cancel', id: 7 });
});

test('StatusBar: a failed export says why', () => {
  render(
    <StatusBar
      status={baseStatus([{ id: 8, name: 'Game 1', state: 'failed', progress: 0, error: 'the disk is full', path: '' }])}
    />,
  );
  expect(screen.getByText('Export failed: the disk is full')).toBeInTheDocument();
});
test('StatusBar: Clean up asks the engine first and confirms before running', async () => {
  const client = { send: vi.fn() };
  client.send.mockResolvedValueOnce({ ok: true, bytesFreed: 13207024435, segments: 4 });
  client.send.mockResolvedValueOnce({ ok: true });
  render(<StatusBar status={baseStatus([])} client={client} />);

  fireEvent.click(screen.getByRole('button', { name: 'Clean up' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'cleanup.plan' });
  expect(await screen.findByText('Free 12.3 GB?')).toBeInTheDocument();

  fireEvent.click(within(screen.getByRole('dialog')).getByRole('button', { name: 'Clean up' }));
  expect(client.send).toHaveBeenCalledWith({ cmd: 'cleanup.run' });
});

test('StatusBar: nothing to free opens no dialog', async () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true, bytesFreed: 0, segments: 0 }) };
  render(<StatusBar status={baseStatus([])} client={client} />);

  fireEvent.click(screen.getByRole('button', { name: 'Clean up' }));
  expect(await screen.findByText('Nothing to free')).toBeInTheDocument();
  expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
});

test('StatusBar: a running cleanup shows progress', () => {
  const status = {
    ...baseStatus([]),
    cleanup: { state: 'running', progress: 0.4, bytesFreed: 0, error: '' },
  } as unknown as Status;
  render(<StatusBar status={status} client={{ send: vi.fn() }} />);
  expect(screen.getByText('Cleaning up · 40 %')).toBeInTheDocument();
  expect(screen.getByRole('button', { name: 'Clean up' })).toBeDisabled();
});