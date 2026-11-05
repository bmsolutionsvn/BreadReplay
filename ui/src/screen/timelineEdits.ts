import type { AngleCut, Event, InputStatus } from '../api/types';

export type EventEdit = Pick<Event, 'inUs' | 'outUs' | 'angles' | 'speeds' | 'zooms'>;

export type Limits = { minUs: number; maxUs: number; periodUs: number };

const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

function clone(ev: EventEdit): EventEdit {
  return {
    inUs: ev.inUs,
    outUs: ev.outUs,
    angles: ev.angles.map((a) => ({ ...a })),
    speeds: ev.speeds.map((s) => ({ ...s })),
    zooms: ev.zooms.map((z) => ({ ...z, rect: { ...z.rect } })),
  };
}

function moveIn<T extends { tUs: number }>(points: T[], fromUs: number, toUs: number): T[] {
  if (fromUs < toUs) {
    const kept = points.filter((p) => p.tUs > toUs);
    let inEffect: T | undefined;
    for (const p of points) if (p.tUs <= toUs) inEffect = p;
    return inEffect ? [{ ...inEffect, tUs: toUs }, ...kept] : kept;
  }
  if (fromUs > toUs && points.length) {
    return [{ ...points[0], tUs: toUs }, ...points.slice(1)];
  }
  return points.map((p) => ({ ...p }));
}

export function mediaLimits(inputs: InputStatus[], ev: EventEdit, periodUs: number): Limits {
  const first = ev.angles[0]?.inputId;
  const last = ev.angles[ev.angles.length - 1]?.inputId;
  const start = inputs.find((i) => i.id === first)?.coverage.find(([a, b]) => a <= ev.inUs && ev.inUs <= b);
  const end = inputs.find((i) => i.id === last)?.coverage.find(([a, b]) => a <= ev.outUs && ev.outUs <= b);
  return { minUs: start?.[0] ?? ev.inUs, maxUs: end?.[1] ?? ev.outUs, periodUs };
}

export function snapTime(
  tUs: number,
  o: { periodUs: number; targets: number[]; usPerPx: number; snap: boolean },
): number {
  if (o.snap && o.usPerPx > 0) {
    let best: number | undefined;
    for (const target of o.targets) {
      const distance = Math.abs(target - tUs);
      if (distance <= 6 * o.usPerPx && (best === undefined || distance < Math.abs(best - tUs))) best = target;
    }
    if (best !== undefined) return best;
  }
  return Math.round(tUs / o.periodUs) * o.periodUs;
}

export function trimIn(ev: EventEdit, tUs: number, lim: Limits): EventEdit {
  const out = clone(ev);
  out.inUs = clamp(tUs, lim.minUs, ev.outUs - lim.periodUs);
  out.angles = moveIn(out.angles, ev.inUs, out.inUs);
  out.speeds = moveIn(out.speeds, ev.inUs, out.inUs);
  out.zooms = moveIn(out.zooms, ev.inUs, out.inUs);
  return out;
}

export function trimOut(ev: EventEdit, tUs: number, lim: Limits): EventEdit {
  const out = clone(ev);
  out.outUs = clamp(tUs, ev.inUs + lim.periodUs, lim.maxUs);
  out.angles = out.angles.filter((a) => a.tUs < out.outUs);
  out.speeds = out.speeds.filter((s) => s.tUs < out.outUs);
  out.zooms = out.zooms.filter((z) => z.tUs < out.outUs);
  return out;
}

export function moveSwitch(ev: EventEdit, i: number, tUs: number, lim: Limits): EventEdit {
  const out = clone(ev);
  if (i < 1 || i >= out.angles.length) return out;
  const lo = out.angles[i - 1].tUs + lim.periodUs;
  const hi = (out.angles[i + 1]?.tUs ?? out.outUs) - lim.periodUs;
  out.angles[i].tUs = clamp(tUs, lo, hi);
  return out;
}

export function changeCamera(ev: EventEdit, i: number, inputId: string): EventEdit {
  const out = clone(ev);
  if (i < 0 || i >= out.angles.length) return out;
  out.angles[i].inputId = inputId;
  const kept: AngleCut[] = [];
  for (const cut of out.angles) {
    if (!kept.length || kept[kept.length - 1].inputId !== cut.inputId) kept.push(cut);
  }
  out.angles = kept;
  return out;
}

export function moveBlock(ev: EventEdit, i: number, deltaUs: number, lim: Limits): EventEdit {
  const out = clone(ev);
  if (i < 0 || i >= out.angles.length) return out;
  const period = lim.periodUs;
  if (i === 0) {
    if (out.angles.length < 2) return out;
    const lo = ev.inUs + period;
    const hi = (out.angles[2]?.tUs ?? ev.outUs) - period;
    out.angles[1].tUs = clamp(out.angles[1].tUs + deltaUs, lo, hi);
    return out;
  }
  if (i === out.angles.length - 1) {
    const lo = out.angles[i - 1].tUs + period;
    const hi = ev.outUs - period;
    out.angles[i].tUs = clamp(out.angles[i].tUs + deltaUs, lo, hi);
    return out;
  }
  const length = out.angles[i + 1].tUs - out.angles[i].tUs;
  const lo = out.angles[i - 1].tUs + period;
  const hi = (out.angles[i + 2]?.tUs ?? ev.outUs) - period - length;
  const start = clamp(out.angles[i].tUs + deltaUs, lo, hi);
  out.angles[i].tUs = start;
  out.angles[i + 1].tUs = start + length;
  return out;
}

export function split(ev: EventEdit, tUs: number, periodUs: number): EventEdit {
  const out = clone(ev);
  const at = Math.round(tUs / periodUs) * periodUs;
  if (at <= ev.inUs || at >= ev.outUs || out.angles.some((a) => a.tUs === at)) return out;
  let camera = '';
  for (const cut of out.angles) if (cut.tUs <= at) camera = cut.inputId;
  if (!camera) return out;
  out.angles.push({ tUs: at, inputId: camera });
  out.angles.sort((a, b) => a.tUs - b.tUs);
  return out;
}

export function deleteBlock(ev: EventEdit, i: number): EventEdit {
  const out = clone(ev);
  if (i < 0 || i >= out.angles.length || out.angles.length < 2) return out;
  if (i === 0) {
    out.angles[0].inputId = out.angles[1].inputId;
    out.angles.splice(1, 1);
  } else {
    out.angles.splice(i, 1);
  }
  return out;
}

export function slip(ev: EventEdit, deltaUs: number, lim: Limits): EventEdit {
  const out = clone(ev);
  const delta = clamp(deltaUs, lim.minUs - ev.inUs, lim.maxUs - ev.outUs);
  out.inUs += delta;
  out.outUs += delta;
  out.angles = out.angles.map((a) => ({ ...a, tUs: a.tUs + delta }));
  out.speeds = out.speeds.map((s) => ({ ...s, tUs: s.tUs + delta }));
  out.zooms = out.zooms.map((z) => ({ ...z, tUs: z.tUs + delta }));
  return out;
}

export function moveSpeedEdge(ev: EventEdit, i: number, tUs: number, lim: Limits): EventEdit {
  const out = clone(ev);
  if (i <= 0 || i >= out.speeds.length) return out;
  const lo = out.speeds[i - 1].tUs + lim.periodUs;
  const hi = (out.speeds[i + 1]?.tUs ?? out.outUs) - lim.periodUs;
  out.speeds[i].tUs = clamp(tUs, lo, hi);
  return out;
}

export function setSpeed(ev: EventEdit, i: number, speed: number): EventEdit {
  const out = clone(ev);
  if (i < 0 || i >= out.speeds.length) return out;
  out.speeds[i].speed = speed;
  return out;
}

export function moveZoomEdge(ev: EventEdit, i: number, tUs: number, lim: Limits): EventEdit {
  const out = clone(ev);
  if (i <= 0 || i >= out.zooms.length) return out;
  const lo = out.zooms[i - 1].tUs + lim.periodUs;
  const hi = (out.zooms[i + 1]?.tUs ?? out.outUs) - lim.periodUs;
  out.zooms[i].tUs = clamp(tUs, lo, hi);
  return out;
}

export function resetZoom(ev: EventEdit, i: number): EventEdit {
  const out = clone(ev);
  if (i < 0 || i >= out.zooms.length) return out;
  out.zooms[i].rect = { cx: 0.5, cy: 0.5, scale: 1 };
  return out;
}