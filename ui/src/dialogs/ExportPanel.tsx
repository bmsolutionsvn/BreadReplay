import { useEffect, useRef, useState } from 'react';

import type { Client, Command } from '../api/client';
import type { ExportCodec, ExportQuality } from '../api/types';

export type ExportTarget = { listId?: string; ids?: number[]; id?: number; name: string };
export type ExportChoice = { codec: ExportCodec; quality: ExportQuality; mbps: number };

type Props = {
  client: Pick<Client, 'send'>;
  what: ExportTarget;
  codecs: ExportCodec[];
  last: ExportChoice;
  onClose: () => void;
  onStarted?: (choice: ExportChoice) => void;
};

// A non-modal popover: Escape or a click outside closes it, and the screen's hotkeys keep working.
export function ExportPanel({ client, what, codecs, last, onClose, onStarted }: Props) {
  const { name: defaultName, ...target } = what;
  const [name, setName] = useState(defaultName);
  const [codec, setCodec] = useState<ExportCodec>(codecs.includes(last.codec) ? last.codec : 'h264');
  const [quality, setQuality] = useState<ExportQuality>(last.quality);
  const [mbps, setMbps] = useState(String(last.mbps));
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const box = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    const onDown = (e: MouseEvent) => {
      if (box.current && !box.current.contains(e.target as Node)) onClose();
    };
    window.addEventListener('keydown', onKey);
    window.addEventListener('mousedown', onDown);
    return () => {
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('mousedown', onDown);
    };
  }, [onClose]);

  const mbpsValue = Number(mbps);
  const customBad = quality === 'custom' && !(mbpsValue > 0);

  async function start() {
    if (busy || customBad) return;
    setBusy(true);
    setError('');
    const choice: ExportChoice = { codec, quality, mbps: mbpsValue > 0 ? mbpsValue : last.mbps };
    const cmd: Command = {
      cmd: 'export.start',
      ...target,
      name: name.trim() || defaultName,
      codec,
      quality,
    };
    if (quality === 'custom') cmd.mbps = mbpsValue;
    try {
      const reply = await client.send(cmd);
      if (!reply.ok) {
        setError(reply.error);
        return;
      }
      onStarted?.(choice);
      void client.send({ cmd: 'settings.set', exportCodec: codec, exportQuality: quality, exportMbps: choice.mbps });
      onClose();
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="export-pop" role="dialog" aria-label="Export" ref={box}>
      <label className="field x">
        <span>Name</span>
        <input className="ctl" aria-label="Export name" value={name} onChange={(e) => setName(e.target.value)} />
      </label>
      <label className="field x">
        <span>Codec</span>
        <select className="ctl" aria-label="Codec" value={codec} onChange={(e) => setCodec(e.target.value as ExportCodec)}>
          <option value="h264">H.264</option>
          {codecs.includes('hevc') && <option value="hevc">H.265</option>}
        </select>
      </label>
      <label className="field x">
        <span>Quality</span>
        <select className="ctl" aria-label="Quality" value={quality} onChange={(e) => setQuality(e.target.value as ExportQuality)}>
          <option value="high">High</option>
          <option value="medium">Medium</option>
          <option value="small">Small</option>
          <option value="custom">Custom</option>
        </select>
      </label>
      {quality === 'custom' && (
        <label className="field x">
          <span>Bitrate</span>
          <input
            className="ctl mono"
            aria-label="Bitrate (Mbps)"
            type="number"
            min={0}
            step={0.5}
            value={mbps}
            onChange={(e) => setMbps(e.target.value)}
          />
        </label>
      )}
      {error && (
        <div className="block danger" role="alert">
          {error}
        </div>
      )}
      <div className="row-end">
        <button className="btn" onClick={onClose}>
          Cancel
        </button>
        <button className="btn primary" disabled={busy || customBad} onClick={start}>
          Start
        </button>
      </div>
    </div>
  );
}