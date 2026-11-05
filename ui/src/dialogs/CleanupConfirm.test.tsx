import { fireEvent, render, screen } from '@testing-library/react';

import { CleanupConfirm } from './CleanupConfirm';

test('CleanupConfirm: shows what it frees and confirms', () => {
  const onConfirm = vi.fn();
  const onCancel = vi.fn();
  render(<CleanupConfirm bytes={13207024435} onConfirm={onConfirm} onCancel={onCancel} />);

  expect(screen.getByText('Free 12.3 GB?')).toBeInTheDocument();
  expect(screen.getByText('This cannot be undone.')).toBeInTheDocument();

  fireEvent.click(screen.getByRole('button', { name: 'Clean up' }));
  expect(onConfirm).toHaveBeenCalled();

  fireEvent.keyDown(window, { key: 'Escape' });
  expect(onCancel).toHaveBeenCalled();
});