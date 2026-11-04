import type { Status } from '../api/types';

// Cams 1–4 are the approved mockup's; 5–8 keep the same rule (no hue near the tally colours).
export const CAMERA_COLORS = ['#5aa0ff', '#e4a94b', '#c084fc', '#f472b6', '#22d3ee', '#facc15', '#fb923c', '#cbd5e1'] as const;

export const TALLY = { pgm: '#e5484d', pgmInk: '#b3261e', pvw: '#30a46c', pvwInk: '#17694a' } as const;

export type MarkStamp = { edge: 'in' | 'out'; inputId: string; tUs: number };

export function cameraColor(index: number): string {
  const n = CAMERA_COLORS.length;
  return CAMERA_COLORS[((index % n) + n) % n];
}

export function cameraIndex(status: Status | null | undefined, inputId: string | undefined): number {
  return status?.inputs.findIndex((i) => i.id === inputId) ?? -1;
}