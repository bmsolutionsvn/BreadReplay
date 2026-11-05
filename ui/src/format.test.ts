import { formatClockTenths, formatDiskLeft, formatDuration, formatRemaining, formatSize, formatBytes, rateFps } from './format';

const local = (h: number, m: number, s: number) => new Date(1970, 0, 1, h, m, s).getTime() * 1000;

test('format: a mark stamp reads to a tenth', () => {
  expect(formatClockTenths(local(1, 12, 29) + 400_000)).toBe('01:12:29.4');
});

test('format: remaining 7.4 s at 59.94 prints -00:07:24', () => {
  expect(formatRemaining(7.4, 59.94)).toBe('-00:07:24');
});

test('format: infinite remaining prints --', () => {
  expect(formatRemaining(Infinity, 59.94)).toBe('--');
  expect(formatRemaining(null, 59.94)).toBe('--');
});

test('format: the rate string gives its fps', () => {
  expect(rateFps('60000/1001')).toBeCloseTo(59.94, 2);
  expect(rateFps('50/1')).toBe(50);
});

test('format: durations and disk time read for an operator', () => {
  expect(formatDuration(12_340_000)).toBe('0:12');
  expect(formatDuration(72_000_000)).toBe('1:12');
  expect(formatDiskLeft(600)).toBe('10 min left');
  expect(formatDiskLeft(7200)).toBe('2 h 0 min left');
  expect(formatDiskLeft(null)).toBe('--');
  expect(formatSize(1536 * 1024 * 1024)).toBe('1.5 GB');
});

test('format: cleanup sizes read from bytes to GB', () => {
  expect(formatBytes(0)).toBe('0 B');
  expect(formatBytes(524288)).toBe('512 KB');
  expect(formatBytes(13207024435)).toBe('12.3 GB');
});