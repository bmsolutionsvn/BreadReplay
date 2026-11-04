import { CAMERA_COLORS, TALLY } from './cameraColors';

function rgb(hex: string): [number, number, number] {
  const n = parseInt(hex.slice(1), 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

function hueOf(hex: string): number {
  const [r, g, b] = rgb(hex);
  const max = Math.max(r, g, b);
  const d = max - Math.min(r, g, b);
  if (d === 0) return 0;
  let h = 0;
  if (max === r) h = ((g - b) / d) % 6;
  else if (max === g) h = (b - r) / d + 2;
  else h = (r - g) / d + 4;
  return ((h * 60) % 360 + 360) % 360;
}

function luminance(hex: string): number {
  const [r, g, b] = rgb(hex).map((v) => (v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4));
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

function contrast(a: string, b: string): number {
  const x = luminance(a);
  const y = luminance(b);
  return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
}

test('camera colours stay off the tally hues and read on the track', () => {
  for (const c of CAMERA_COLORS) {
    for (const t of [TALLY.pgm, TALLY.pvw]) {
      const d = Math.abs(hueOf(c) - hueOf(t)) % 360;
      expect(Math.min(d, 360 - d)).toBeGreaterThanOrEqual(25);
    }
    expect(contrast(c, '#151a20')).toBeGreaterThanOrEqual(3);
    expect(contrast(c, '#0b0e12')).toBeGreaterThanOrEqual(4.5);
  }
  expect(contrast('#ffffff', TALLY.pgmInk)).toBeGreaterThanOrEqual(4.5);
  expect(contrast('#ffffff', TALLY.pvwInk)).toBeGreaterThanOrEqual(4.5);
});