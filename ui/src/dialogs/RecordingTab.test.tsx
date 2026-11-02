import { fireEvent, render, screen } from '@testing-library/react';

import { defaultSettings } from './Settings';
import { RecordingTab } from './RecordingTab';

test('RecordingTab: pre-flight shows the measured disk', async () => {
  const client = {
    send: vi.fn().mockResolvedValue({ ok: true, canRecord: true, minutesFree: 412.4, writeMBps: 180.2, warnings: [], error: null }),
  };
  render(<RecordingTab client={client} value={defaultSettings} onChange={() => {}} limitHours="" onLimitHours={() => {}} limitError="" />);
  fireEvent.click(screen.getByRole('button', { name: 'Check the drive' }));
  expect(await screen.findByText(/room for about 412 minutes/)).toBeInTheDocument();
  expect(client.send).toHaveBeenCalledWith({ cmd: 'record.preflight' });
});