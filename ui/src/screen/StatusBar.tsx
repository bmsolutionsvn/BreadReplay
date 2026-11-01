import type { Status } from '../api/types';
import { formatDiskLeft, formatSize } from '../format';

// System errors persist as a block, never as a toast (BM UX contract).
export function StatusBar({ status }: { status: Status | null }) {
  const disk = status?.disk;
  return (
    <footer className="status" aria-label="Status bar">
      <span>Disk free <b>{disk ? formatSize(disk.freeBytes) : '—'}</b></span>
      <span>{formatDiskLeft(disk?.secondsLeft ?? null)}</span>
      <span>{disk ? `${disk.writeMBps.toFixed(1)} MB/s` : '—'}</span>
      <span className="rec">{status?.recording ? 'Recording' : 'Not recording'}</span>
      <div className="health">
        {(status?.inputs ?? []).map((input) => (
          <span key={input.id} title={`${input.fpsIn.toFixed(2)} fps · ${input.dropped} dropped`}>
            <i className={`dot ${input.state}`} />
            {input.name}
          </span>
        ))}
      </div>
      <div className="alerts">
        {(status?.alerts ?? []).map((a) => (
          <div key={a.id} className="block danger" role="alert">
            {a.text}
          </div>
        ))}
      </div>
    </footer>
  );
}