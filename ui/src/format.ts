export function rateFps(rate: string): number {
  const [num, den] = rate.split('/');
  return Number(num) / Number(den || 1);
}

export function formatRemaining(seconds: number | null | undefined, fps: number): string {
  if (seconds == null || !Number.isFinite(seconds)) return '--';
  const nominal = Math.max(1, Math.round(fps));
  const total = Math.max(0, Math.round(Math.max(0, seconds) * nominal));
  const mm = Math.floor(total / (nominal * 60));
  const ss = Math.floor(total / nominal) % 60;
  const ff = total % nominal;
  return `-${pad(mm)}:${pad(ss)}:${pad(ff)}`;
}

export function formatClock(us: number): string {
  const d = new Date(us / 1000);
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

export function formatDuration(us: number): string {
  const s = Math.max(0, Math.round(us / 1_000_000));
  return `${Math.floor(s / 60)}:${pad(s % 60)}`;
}

export function formatSize(bytes: number): string {
  const gb = bytes / 1024 ** 3;
  if (gb >= 1) return `${gb.toFixed(1)} GB`;
  return `${Math.round(bytes / 1024 ** 2)} MB`;
}

export function formatDiskLeft(secondsLeft: number | null): string {
  if (secondsLeft == null || !Number.isFinite(secondsLeft)) return '--';
  const s = Math.max(0, Math.floor(secondsLeft));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h > 0 ? `${h} h ${m} min left` : `${m} min left`;
}

function pad(n: number): string {
  return String(n).padStart(2, '0');
}