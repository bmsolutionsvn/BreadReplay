import { act, render, screen } from '@testing-library/react';

import { TitleBar } from './TitleBar';

let shell: ((e: { data: unknown }) => void) | null = null;
function shellStub() {
  const post = vi.fn();
  shell = null;
  (window as unknown as { chrome: unknown }).chrome = {
    webview: {
      postMessage: post,
      addEventListener: (_t: string, fn: (e: { data: unknown }) => void) => (shell = fn),
      removeEventListener: () => (shell = null),
    },
  };
  return post;
}

const posted = (post: ReturnType<typeof vi.fn>, at = 0) => JSON.parse(post.mock.calls.at(at)![0] as string);

afterEach(() => {
  delete (window as unknown as { chrome?: unknown }).chrome;
  vi.restoreAllMocks();
});

test('the caption buttons send window actions to the shell', () => {
  const post = shellStub();
  render(<TitleBar />);
  act(() => screen.getByRole('button', { name: 'Minimise' }).click());
  expect(posted(post, 0)).toEqual({ type: 'window', action: 'minimize' });
  act(() => screen.getByRole('button', { name: 'Maximise' }).click());
  expect(posted(post, 1)).toEqual({ type: 'window', action: 'maximize' });
  act(() => screen.getByRole('button', { name: 'Quit BreadReplay' }).click());
  expect(posted(post, 2)).toEqual({ type: 'window', action: 'close' });
  expect(post).toHaveBeenCalledTimes(3);
});

test('windowState turns the middle button into Restore and back', () => {
  shellStub();
  render(<TitleBar />);
  expect(screen.getByRole('button', { name: 'Maximise' })).toBeInTheDocument();
  act(() => shell?.({ data: { type: 'windowState', maximized: true } }));
  expect(screen.getByRole('button', { name: 'Restore' })).toBeInTheDocument();
  expect(screen.queryByRole('button', { name: 'Maximise' })).toBeNull();
  act(() => shell?.({ data: { type: 'windowState', maximized: false } }));
  expect(screen.getByRole('button', { name: 'Maximise' })).toBeInTheDocument();
});

test('without a shell bridge the bar renders and its buttons do not throw', () => {
  render(<TitleBar />);
  expect(screen.getByText('BreadReplay')).toBeInTheDocument();
  for (const name of ['Minimise', 'Maximise', 'Quit BreadReplay']) {
    expect(() => act(() => screen.getByRole('button', { name }).click())).not.toThrow();
  }
});

test('the bar is the drag region and its buttons are not', () => {
  const { container } = render(<TitleBar />);
  const bar = container.querySelector('header')!;
  expect(bar).toHaveClass('titlebar');
  // app.css: `.titlebar { app-region: drag }` and `.titlebar button { app-region: no-drag }`.
  const buttons = bar.querySelectorAll('button');
  expect(buttons).toHaveLength(3);
  expect(bar.querySelector('.name')).toHaveTextContent('BreadReplay');
});