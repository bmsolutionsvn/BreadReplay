import { act, render, screen, waitFor } from '@testing-library/react';
import { useEffect } from 'react';

import { App } from '../App';
import { Client } from '../api/client';
import type { Status } from '../api/types';
import { FakeWs } from '../test/fakeWs';
import { AngleRow } from './AngleRow';
import { NativeVideo } from './NativeVideo';
import { startNativeVideo } from './nativeVideoRegistry';

function makeClient() {
  FakeWs.all = [];
  const c = new Client('ws://x', (u) => new FakeWs(u) as unknown as WebSocket);
  FakeWs.last.open();
  return c;
}

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

// The page's boxes measured 200x100 CSS px at (10, 20); `.zero` stands in for display:none.
let left = 10;
let top = 20;
const width = 200;
const height = 100;

beforeEach(() => {
  left = 10;
  top = 20;
  // jsdom does not load app.css, so the native-mode transparency pass always warns here.
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  Object.defineProperty(window, 'devicePixelRatio', { configurable: true, value: 1.5 });
  vi.spyOn(Element.prototype, 'getBoundingClientRect').mockImplementation(function (this: Element) {
    const zero = this.parentElement?.classList.contains('zero') ?? false;
    const w = zero ? 0 : width;
    const h = zero ? 0 : height;
    return { x: left, y: top, left, top, width: w, height: h, right: left + w, bottom: top + h, toJSON: () => ({}) } as DOMRect;
  });
});

afterEach(() => {
  delete (window as unknown as { chrome?: unknown }).chrome;
  delete (document as unknown as { visibilityState?: string }).visibilityState;
  location.hash = '';
  Object.defineProperty(window, 'devicePixelRatio', { configurable: true, value: 1 });
  vi.useRealTimers();
  vi.restoreAllMocks();
});

function Harness({ client, native, ids }: { client: Client; native: boolean; ids: string[] }) {
  useEffect(() => startNativeVideo(client), [client]);
  return (
    <div>
      {ids.map((id) => (
        <div key={id} className={id === 'angle:b' ? 'zero' : 'wide'}>
          <NativeVideo id={id} native={native} fallback={<span>fallback {id}</span>} />
        </div>
      ))}
    </div>
  );
}

const sent = (ws: FakeWs = FakeWs.last) => ws.sent.map((s) => JSON.parse(s) as { cmd: string } & Record<string, unknown>);
const surfaceReqs = (ws: FakeWs = FakeWs.last) => sent(ws).filter((m) => m.cmd === 'subscribeSurfaces');
const layers = (post: ReturnType<typeof vi.fn>, at = -1) => JSON.parse(post.mock.calls.at(at)![0] as string);

const baseStatus: Status = {
  recording: true,
  inputs: [],
  disk: { freeBytes: 1, secondsLeft: 60, writeMBps: 10 },
  alerts: [],
  players: {
    preview: { posUs: 0, tc: '01:00:00:00', eventId: null, angle: 'a', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 } },
    output: { posUs: 0, tc: '01:00:00:00', eventId: null, angle: 'a', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 }, remainingS: null },
  },
  events: { version: 1 },
};

test('renders the fallback when native is false', () => {
  const post = shellStub();
  render(<Harness client={makeClient()} native={false} ids={['preview']} />);
  expect(screen.getByText('fallback preview')).toBeInTheDocument();
  expect(surfaceReqs()).toHaveLength(0);
  expect(post).not.toHaveBeenCalled();
});

test('subscribes with device-pixel sizes', async () => {
  shellStub();
  render(<Harness client={makeClient()} native ids={['preview', 'output']} />);
  await waitFor(() => expect(surfaceReqs()).toHaveLength(1));
  expect(surfaceReqs()[0].surfaces).toEqual([
    { id: 'preview', w: 300, h: 150 },
    { id: 'output', w: 300, h: 150 },
  ]);
});

test('relays the reply to the shell', async () => {
  const post = shellStub();
  render(<Harness client={makeClient()} native ids={['preview']} />);
  await waitFor(() => expect(surfaceReqs()).toHaveLength(1));
  act(() => FakeWs.last.receive({ ok: true, pid: 42, surfaces: [{ id: 'preview', handle: '99', w: 300, h: 150 }] }));
  await waitFor(() => expect(post).toHaveBeenCalled());
  expect(layers(post)).toEqual({ type: 'videoLayers', pid: 42, layers: [{ id: 'preview', handle: '99', x: 15, y: 30, w: 300, h: 150 }] });
});

test('a position-only change re-posts layers without a new subscription', async () => {
  const post = shellStub();
  render(<Harness client={makeClient()} native ids={['preview']} />);
  await waitFor(() => expect(surfaceReqs()).toHaveLength(1));
  act(() => FakeWs.last.receive({ ok: true, pid: 42, surfaces: [{ id: 'preview', handle: '99', w: 300, h: 150 }] }));
  await waitFor(() => expect(post).toHaveBeenCalled());
  left = 30;
  act(() => window.dispatchEvent(new Event('resize')));
  await waitFor(() => expect(layers(post).layers[0].x).toBe(45));
  expect(surfaceReqs()).toHaveLength(1);
});

test('a 0x0 box is left out and visibility hidden sends an empty subscription', async () => {
  shellStub();
  render(<Harness client={makeClient()} native ids={['preview', 'angle:b']} />);
  await waitFor(() => expect(surfaceReqs()).toHaveLength(1));
  expect(surfaceReqs()[0].surfaces).toEqual([{ id: 'preview', w: 300, h: 150 }]);
  act(() => FakeWs.last.receive({ ok: true, pid: 42, surfaces: [{ id: 'preview', handle: '99', w: 300, h: 150 }] }));
  Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
  act(() => document.dispatchEvent(new Event('visibilitychange')));
  await waitFor(() => expect(surfaceReqs()).toHaveLength(2));
  expect(surfaceReqs()[1].surfaces).toEqual([]);
});

test('videoLayerFailed shows the no-picture placeholder for that id', async () => {
  shellStub();
  const { container } = render(<Harness client={makeClient()} native ids={['preview']} />);
  await waitFor(() => expect(container.querySelector('.nv-box')).not.toBeNull());
  act(() => shell?.({ data: { type: 'videoLayerFailed', id: 'preview' } }));
  expect(container.querySelector('.nv-lost')).not.toBeNull();
  expect(screen.queryByText(/fallback/)).toBeNull();
});

test('hiding sends an empty subscription without waiting for a frame', async () => {
  shellStub();
  render(<Harness client={makeClient()} native ids={['preview']} />);
  await waitFor(() => expect(surfaceReqs()).toHaveLength(1));
  act(() => FakeWs.last.receive({ ok: true, pid: 42, surfaces: [{ id: 'preview', handle: '99', w: 300, h: 150 }] }));
  // A hidden WebView2 page does not run animation frames.
  vi.spyOn(window, 'requestAnimationFrame').mockReturnValue(1);
  Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
  act(() => document.dispatchEvent(new Event('visibilitychange')));
  expect(surfaceReqs()).toHaveLength(2);
  expect(surfaceReqs()[1].surfaces).toEqual([]);
});

function angleStatus(ids: string[]): Status {
  return {
    ...baseStatus,
    inputs: ids.map((id) => ({ id, name: id, state: 'live', fpsIn: 60, dropped: 0, queued: 0, coverage: [] })),
  };
}

function AngleHarness({ client, ids }: { client: Client; ids: string[] }) {
  useEffect(() => startNativeVideo(client), [client]);
  return <AngleRow status={angleStatus(ids)} client={client} native />;
}

test('the angle list changing resubscribes with the new ids', async () => {
  shellStub();
  const client = makeClient();
  const { rerender } = render(<AngleHarness client={client} ids={['a', 'b']} />);
  await waitFor(() => expect(surfaceReqs()).toHaveLength(1));
  expect(surfaceReqs()[0].surfaces).toEqual([
    { id: 'angle:a', w: 300, h: 150 },
    { id: 'angle:b', w: 300, h: 150 },
  ]);
  act(() =>
    FakeWs.last.receive({
      ok: true,
      pid: 42,
      surfaces: [
        { id: 'angle:a', handle: '1', w: 300, h: 150 },
        { id: 'angle:b', handle: '2', w: 300, h: 150 },
      ],
    }),
  );
  rerender(<AngleHarness client={client} ids={['a', 'b', 'c']} />);
  await waitFor(() => expect(surfaceReqs()).toHaveLength(2));
  expect((surfaceReqs()[1].surfaces as Array<{ id: string }>).map((s) => s.id)).toEqual(['angle:a', 'angle:b', 'angle:c']);
});

test('fallback mode sends wantPreviews on open and on reconnect', () => {
  vi.useFakeTimers();
  FakeWs.all = [];
  const c = new Client('ws://x', (u) => new FakeWs(u) as unknown as WebSocket);
  void c.wantPreviews().catch(() => {});
  const first = FakeWs.last;
  first.open();
  expect(sent(first).map((m) => m.cmd)).toEqual(['wantPreviews']);
  first.close();
  vi.advanceTimersByTime(2000);
  const second = FakeWs.last;
  second.open();
  expect(sent(second).map((m) => m.cmd)).toEqual(['wantPreviews']);
  c.close();
});

test('native mode never sends wantPreviews', async () => {
  location.hash = '#nativeVideo=1';
  const c = makeClient();
  render(<App client={c} />);
  act(() => FakeWs.last.receive({ ...baseStatus, type: 'status', nativeVideo: true }));
  await waitFor(() => expect(document.documentElement.classList.contains('native-video')).toBe(true));
  expect(sent().map((m) => m.cmd)).not.toContain('wantPreviews');
  c.close();
});