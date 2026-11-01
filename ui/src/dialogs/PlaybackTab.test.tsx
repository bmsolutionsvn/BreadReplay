import { fireEvent, render, screen } from '@testing-library/react';

import { PlaybackTab } from './PlaybackTab';
import { defaultSettings } from './Settings';

test('PlaybackTab: a speed choice is reported as a fraction', () => {
  const onChange = vi.fn();
  render(<PlaybackTab value={defaultSettings} onChange={onChange} />);
  fireEvent.change(screen.getByLabelText('Default speed'), { target: { value: '0.75' } });
  expect(onChange).toHaveBeenCalledWith({ defaultSpeed: 0.75 });
});