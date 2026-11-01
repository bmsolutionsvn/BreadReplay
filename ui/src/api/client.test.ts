import { FakeWs } from '../test/fakeWs';
import { connect } from './client';

function make() {
  FakeWs.all = [];
  const c = connect(47820, 'abcd', (u) => new FakeWs(u) as unknown as WebSocket);
  FakeWs.last.open();
  return c;
}

const status = {
  type: 'status',
  recording: true,
  inputs: [],
  disk: { freeBytes: 1, secondsLeft: 60, writeMBps: 10 },
  alerts: [{ id: 'disk-space', text: 'Disk D: 12 min left', since: 1 }],
  players: {
    preview: { posUs: 0, tc: '01:00:00:00', eventId: null, angle: 'a', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 } },
    output: { posUs: 0, tc: '01:00:00:00', eventId: null, angle: 'a', playing: false, speed: 1, locked: true, zoom: { cx: 0.5, cy: 0.5, scale: 1 }, remainingS: null },
  },
  events: { version: 1 },
};

test('connect builds the local URL with the token', () => {
  FakeWs.all = [];
  connect(47820, 'a b', (u) => new FakeWs(u) as unknown as WebSocket);
  expect(FakeWs.last.url).toBe('ws://127.0.0.1:47820/?token=a%20b');
});

test('a command is sent as JSON and its reply resolves', async () => {
  const c = make();
  const p = c.send({ cmd: 'take' });
  expect(FakeWs.last.lastRequest()).toEqual({ cmd: 'take' });
  FakeWs.last.receive({ ok: true });
  await expect(p).resolves.toEqual({ ok: true });
});

test('an error reply resolves with ok false and the message', async () => {
  const c = make();
  const p = c.send({ cmd: 'player.cue', eventId: 9 });
  FakeWs.last.receive({ ok: false, error: 'no such event' });
  await expect(p).resolves.toEqual({ ok: false, error: 'no such event' });
});

test('status and events messages reach their listeners', () => {
  const c = make();
  const got: string[] = [];
  c.onStatus((s) => got.push(`recording:${s.recording}`));
  c.onEvents((e) => got.push(`events:${e.currentList}`));
  FakeWs.last.receive(status);
  FakeWs.last.receive({ type: 'events', version: 1, events: { version: 1, nextId: 1, nextListId: 2, currentList: 'g1', preRollUs: 0, postRollUs: 0, defaultSpeed: 0.5, lists: [], events: [] } });
  expect(got).toEqual(['recording:true', 'events:g1']);
});

test('binary preview frames reach the listener for their id', () => {
  const c = make();
  const got: Array<[string, number]> = [];
  c.onPreview('output', (f) => got.push([f.id, f.ptsUs]));
  const id = new TextEncoder().encode('output');
  const bytes = [1, 1, id.length, 0, ...id, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2];
  FakeWs.last.receiveBinary(bytes);
  expect(got).toEqual([['output', 0]]);
});

test('angle-row frames are parsed from BRAP messages', () => {
  const c = make();
  const got: Array<[number, number, number, number]> = [];
  c.onAngleRow((f) => got.push([f.index, f.width, f.height, f.bgra.length]));
  const brap = [0x42, 0x52, 0x41, 0x50, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8];
  FakeWs.last.receiveBinary(brap);
  expect(got).toEqual([[2, 2, 1, 8]]);
});