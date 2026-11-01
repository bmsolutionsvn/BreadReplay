import { fireEvent, render, screen } from '@testing-library/react';

import { defaultHotkeyKeys, HotkeysTab } from './HotkeysTab';

test('HotkeysTab: a duplicate key is refused inline', () => {
  const onChange = vi.fn();
  const onConflict = vi.fn();
  render(<HotkeysTab value={defaultHotkeyKeys} onChange={onChange} onConflict={onConflict} />);
  fireEvent.change(screen.getByLabelText('Mark Out key'), { target: { value: 'i' } });
  expect(screen.getByText('Already used by Mark In')).toBeInTheDocument();
  expect(onConflict).toHaveBeenCalledWith('Already used by Mark In');
});