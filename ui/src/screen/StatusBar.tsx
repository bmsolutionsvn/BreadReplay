import { useEffect, useState } from 'react';

import type { Client } from '../api/client';
import type { ExportState, Status } from '../api/types';
import { formatDiskLeft, formatSize, signalHint } from '../format';

// System errors persist as a block, never as a toast (BM UX contract).
export function StatusBar({ status, client }: { status: Status | null; client?: Pick<Client, 'send'> }) {
  const disk = status?.disk;
  const exports = status?.exports ?? [];
  const running = exports.find((e) => e.state === 'running');
  const queued = exports.filter((e) => e.state === 'queued').length;
  const finishedList = exports.filter((e) => e.state === 'done' || e.state === 'failed');
  const finished = finishedList.length ? finishedList[finishedList.length - 1] : undefined;
  const failed = !running && finished?.state === 'failed' ? finished : null;
  const done = finished?.state === 'done' ? finished : null;
  const [recent, setRecent] = useState<ExportState | null>(null);

  useEffect(() => {
    if (!done) return;
    setRecent(done);
    const timer = setTimeout(() => setRecent(null), 10_000);
    return () => clearTimeout(timer);
  }, [done?.id, done?.state]);

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
      <div className="exports">
        {running && (
          <span>
            Exporting {running.name} · {Math.round(running.progress * 100)} %
          </span>
        )}
        {running && client && (
          <button
            className="mini"
            aria-label="Cancel export"
            onClick={() => void client.send({ cmd: 'export.cancel', id: running.id })}
          >
            Cancel
          </button>
        )}
        {queued > 0 && <span>+{queued} queued</span>}
        {failed && <span className="err">Export failed: {failed.error}</span>}
        {!running && !failed && recent && <span>Exported {recent.name}</span>}
      </div>
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