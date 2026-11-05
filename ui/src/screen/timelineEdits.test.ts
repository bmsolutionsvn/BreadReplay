import type { InputStatus } from '../api/types';
import {
  changeCamera,
  deleteBlock,
  mediaLimits,
  moveBlock,
  moveSpeedEdge,
  moveSwitch,
  moveZoomEdge,
  resetZoom,
  setSpeed,
  slip,
  snapTime,
  split,
  trimIn,
  trimOut,
  type EventEdit,
  type Limits,
} from './timelineEdits';

const ev: EventEdit = {
  inUs: 0,
  outUs: 10_000_000,
  angles: [
    { tUs: 0, inputId: 'c1' },
    { tUs: 4_000_000, inputId: 'c2' },
    { tUs: 7_000_000, inputId: 'c3' },
  ],
  speeds: [{ tUs: 0, speed: 1 }],
  zooms: [],
};

const lim: Limits = { minUs: -2_000_000, maxUs: 12_000_000, periodUs: 100_000 };

const input = (id: string, coverage: Array<[number, number]>): InputStatus => ({
  id,
  name: id,
  state: 'live',
  fpsIn: 60,
  dropped: 0,
  queued: 0,
  coverage,
});

test('mediaLimits: first block at In and last block at Out bound the media', () => {
  const limits = mediaLimits([input('c1', [[-1_000_000, 20_000_000]]), input('c3', [[0, 11_000_000]])], ev, 100_000);
  expect(limits).toEqual({ minUs: -1_000_000, maxUs: 11_000_000, periodUs: 100_000 });
});

test('snapTime: targets within 6 px win, otherwise the nearest frame', () => {
  expect(snapTime(4_030_000, { periodUs: 100_000, targets: [4_000_000], usPerPx: 10_000, snap: true })).toBe(4_000_000);
  expect(snapTime(4_030_000, { periodUs: 100_000, targets: [4_000_000], usPerPx: 10_000, snap: false })).toBe(4_000_000);
  expect(snapTime(4_080_000, { periodUs: 100_000, targets: [4_000_000], usPerPx: 10_000, snap: true })).toBe(4_100_000);
});

test('trimIn: moving In in drops earlier cuts and keeps the one in effect', () => {
  const out = trimIn(ev, 5_000_000, lim);
  expect(out.inUs).toBe(5_000_000);
  expect(out.angles).toEqual([
    { tUs: 5_000_000, inputId: 'c2' },
    { tUs: 7_000_000, inputId: 'c3' },
  ]);
  expect(out.speeds).toEqual([{ tUs: 5_000_000, speed: 1 }]);
});

test('trimIn: clamps to the media and trimOut keeps one period', () => {
  expect(trimIn(ev, -5_000_000, lim).inUs).toBe(-2_000_000);
  expect(trimOut(ev, -1, lim).outUs).toBe(100_000);
});

test('moveSwitch: clamps one period before the next cut', () => {
  expect(moveSwitch(ev, 1, 8_000_000, lim).angles[1].tUs).toBe(6_900_000);
});

test('changeCamera: a block matching the one before it drops its cut', () => {
  expect(changeCamera(ev, 2, 'c2').angles).toEqual([
    { tUs: 0, inputId: 'c1' },
    { tUs: 4_000_000, inputId: 'c2' },
  ]);
});

test('moveBlock: both cuts move and the clamp keeps the block length', () => {
  const moved = moveBlock(ev, 1, 500_000, lim);
  expect(moved.angles[1].tUs).toBe(4_500_000);
  expect(moved.angles[2].tUs).toBe(7_500_000);
  const clamped = moveBlock(ev, 1, 9_000_000, lim);
  expect(clamped.angles[2].tUs).toBe(9_900_000);
  expect(clamped.angles[1].tUs).toBe(6_900_000);
});

test('split: adds a cut at the frame and ignores an existing one', () => {
  expect(split(ev, 5_049_999, 100_000).angles).toEqual([
    { tUs: 0, inputId: 'c1' },
    { tUs: 4_000_000, inputId: 'c2' },
    { tUs: 5_000_000, inputId: 'c2' },
    { tUs: 7_000_000, inputId: 'c3' },
  ]);
  expect(split(ev, 4_000_000, 100_000).angles).toEqual(ev.angles);
});

test('deleteBlock: removes a cut, or gives block 0 the next camera', () => {
  expect(deleteBlock(ev, 1).angles).toEqual([
    { tUs: 0, inputId: 'c1' },
    { tUs: 7_000_000, inputId: 'c3' },
  ]);
  expect(deleteBlock(ev, 0).angles).toEqual([
    { tUs: 0, inputId: 'c2' },
    { tUs: 7_000_000, inputId: 'c3' },
  ]);
});

test('slip: moves the window and every point, clamped by the media', () => {
  const out = slip(ev, 3_000_000, lim);
  expect(out.inUs).toBe(2_000_000);
  expect(out.outUs).toBe(12_000_000);
  expect(out.angles.map((a) => a.tUs)).toEqual([2_000_000, 6_000_000, 9_000_000]);
  expect(out.speeds[0].tUs).toBe(2_000_000);
});

test('speed and zoom edges clamp like switches; values and zoom reset', () => {
  const speedy: EventEdit = { ...ev, speeds: [{ tUs: 0, speed: 1 }, { tUs: 4_000_000, speed: 0.5 }] };
  expect(moveSpeedEdge(speedy, 1, 8_000_000, lim).speeds[1].tUs).toBe(8_000_000);
  expect(setSpeed(ev, 0, 0.25).speeds[0].speed).toBe(0.25);

  const zoomed: EventEdit = {
    ...ev,
    zooms: [
      { tUs: 0, rect: { cx: 0.5, cy: 0.5, scale: 1 } },
      { tUs: 3_000_000, rect: { cx: 0.2, cy: 0.3, scale: 2 } },
    ],
  };
  expect(moveZoomEdge(zoomed, 1, 8_000_000, lim).zooms[1].tUs).toBe(8_000_000);
  expect(resetZoom(zoomed, 1).zooms[1].rect).toEqual({ cx: 0.5, cy: 0.5, scale: 1 });
});

