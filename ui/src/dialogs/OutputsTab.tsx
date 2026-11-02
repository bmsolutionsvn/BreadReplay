import { useState } from 'react';

import type { Client } from '../api/client';
import type { OutputConfig } from '../api/types';

export const OUTPUT_KINDS: Array<[string, string]> = [
  ['decklink', 'DeckLink card (SDI / HDMI cable)'],
  ['omt', 'OMT (video over the local network)'],
  ['ndi', 'NDI (video over the local network)'],
  ['fullscreen', 'A screen on this PC (fullscreen)'],
];
const kindName = (kind: string) => OUTPUT_KINDS.find(([k]) => k === kind)?.[1].split(' (')[0] ?? kind;

function detailLabel(kind: string): string {
  if (kind === 'fullscreen') return 'Screen number';
  if (kind === 'ndi' || kind === 'omt') return 'Name others will see';
  return 'Card number';
}

function paramsFor(kind: string, detail: string): Record<string, unknown> {
  if (kind === 'ndi' || kind === 'omt') return { name: detail || 'BreadReplay' };
  if (kind === 'fullscreen') return { monitor: Number(detail) || 0 };
  return { device: Number(detail) || 0 };
}

export function OutputsTab({ client, outputs }: { client: Pick<Client, 'send'>; outputs: OutputConfig[] }) {
  const [kind, setKind] = useState('decklink');
  const [detail, setDetail] = useState('');

  function add() {
    client.send({
      cmd: 'output.add',
      id: `out-${Date.now().toString(36)}`,
      kind,
      keyed: false,
      enabled: true,
      params: paramsFor(kind, detail.trim()),
    });
    setDetail('');
  }

  return (
    <div className="tab-outputs">
      <h3>On-air output</h3>
      <p className="note">Where the replays go. The director takes this feed into the main switcher.</p>
      <ul className="cfg-list">
        {outputs.map((o) => (
          <li key={o.id}>
            <label className="inline">
              <input
                type="checkbox"
                aria-label={`Enable ${o.kind} ${o.id}`}
                checked={o.enabled}
                onChange={(e) => client.send({ cmd: 'output.update', ...o, enabled: e.target.checked })}
              />
              <span>{`${kindName(o.kind)} · ${o.id}`}</span>
            </label>
            <button className="btn ghost-danger" aria-label={`Remove ${o.kind} ${o.id}`} onClick={() => client.send({ cmd: 'output.remove', id: o.id })}>
              Remove
            </button>
          </li>
        ))}
        {outputs.length === 0 && <li className="note">No output yet. Add one below so the director can take the replays.</li>}
      </ul>
      <h3>Add an output</h3>
      <label className="field">
        <span>Send to</span>
        <select className="ctl" aria-label="New output kind" value={kind} onChange={(e) => setKind(e.target.value)}>
          {OUTPUT_KINDS.map(([v, l]) => (
            <option key={v} value={v}>
              {l}
            </option>
          ))}
        </select>
      </label>
      <label className="field">
        <span>{detailLabel(kind)}</span>
        <input className="ctl mono" aria-label={detailLabel(kind)} value={detail} onChange={(e) => setDetail(e.target.value)} />
      </label>
      <div className="row-end">
        <button className="btn" onClick={add}>
          Add output
        </button>
      </div>
    </div>
  );
}