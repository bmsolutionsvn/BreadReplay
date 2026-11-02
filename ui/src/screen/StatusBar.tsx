import type { Status } from '../api/types';
import { formatDiskLeft, formatSize, signalHint } from '../format';

// System errors persist as a block, never as a toast (BM UX contract).
export function StatusBar({ status }: { status: Status | null }) {
  const disk = status?.disk;
  return (
    <footer className="status" aria-label="Status bar">
      <span title="Free space on the recording drive.">
        Disk <b>{disk ? formatSize(disk.freeBytes) : '—'}</b> free
      </span>
      <span title="How much longer the cameras can record before the drive is full.">
        {disk?.secondsLeft != null ? `room for ${formatDiskLeft(disk.secondsLeft).replace(' left', '')} more` : 'room left: —'}
      </span>
      <span title="How fast the recording is being written to the drive.">{disk ? `writing ${disk.writeMBps.toFixed(1)} MB/s` : '—'}</span>
      <span className="rec">{status?.recording ? 'Recording' : 'Not recording'}</span>
      <div className="health">
        {(status?.inputs ?? []).map((input) => (
          <span key={input.id} title={`${input.name} ${signalHint(input.state)} · ${input.fpsIn.toFixed(2)} frames/s · ${input.dropped} frames lost`}>
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