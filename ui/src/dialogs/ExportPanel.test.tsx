import { fireEvent, render, screen, waitFor } from '@testing-library/react';

import { ExportPanel } from './ExportPanel';

const last = { codec: 'h264' as const, quality: 'high' as const, mbps: 10 };

test('H.265 is hidden when no encoder offers it', () => {
  render(
    <ExportPanel
      client={{ send: vi.fn().mockResolvedValue({ ok: true }) }}
      what={{ listId: 'g1', name: 'Game 1' }}
      codecs={['h264']}
      last={last}
      onClose={() => {}}
    />,
  );
  expect(screen.queryByRole('option', { name: 'H.265' })).toBeNull();
  expect(screen.getByRole('option', { name: 'H.264' })).toBeInTheDocument();
});

test('Custom needs a bitrate above zero', () => {
  render(
    <ExportPanel
      client={{ send: vi.fn().mockResolvedValue({ ok: true }) }}
      what={{ listId: 'g1', name: 'Game 1' }}
      codecs={['h264']}
      last={last}
      onClose={() => {}}
    />,
  );
  fireEvent.change(screen.getByLabelText('Quality'), { target: { value: 'custom' } });
  const bitrate = screen.getByLabelText('Bitrate (Mbps)');
  fireEvent.change(bitrate, { target: { value: '0' } });
  expect(screen.getByRole('button', { name: 'Start' })).toBeDisabled();
  fireEvent.change(bitrate, { target: { value: '5' } });
  expect(screen.getByRole('button', { name: 'Start' })).toBeEnabled();
});

test('Start sends the export and remembers the choice', async () => {
  const client = { send: vi.fn().mockResolvedValue({ ok: true }) };
  const onClose = vi.fn();
  render(
    <ExportPanel
      client={client}
      what={{ listId: 'g1', name: 'Game 1' }}
      codecs={['h264', 'hevc']}
      last={last}
      onClose={onClose}
    />,
  );
  fireEvent.change(screen.getByLabelText('Codec'), { target: { value: 'hevc' } });
  fireEvent.change(screen.getByLabelText('Quality'), { target: { value: 'small' } });
  fireEvent.click(screen.getByRole('button', { name: 'Start' }));
  await waitFor(() => expect(onClose).toHaveBeenCalled());
  expect(client.send).toHaveBeenCalledWith({ cmd: 'export.start', listId: 'g1', name: 'Game 1', codec: 'hevc', quality: 'small' });
  expect(client.send).toHaveBeenCalledWith({ cmd: 'settings.set', exportCodec: 'hevc', exportQuality: 'small', exportMbps: 10 });
});

test('opens on the last codec and quality', () => {
  render(
    <ExportPanel
      client={{ send: vi.fn().mockResolvedValue({ ok: true }) }}
      what={{ listId: 'g1', name: 'Game 1' }}
      codecs={['h264', 'hevc']}
      last={{ codec: 'hevc', quality: 'small', mbps: 6 }}
      onClose={() => {}}
    />,
  );
  expect(screen.getByLabelText('Codec')).toHaveValue('hevc');
  expect(screen.getByLabelText('Quality')).toHaveValue('small');
});