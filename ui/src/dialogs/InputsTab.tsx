import { useState } from 'react';

import type { Client } from '../api/client';
import type { InputStatus } from '../api/types';

// "uvc" is the engine factory's kind; the operator sees "Capture".
export const INPUT_KINDS: Array<[string, string]> = [
  ['srt', 'SRT'],
  ['ndi', 'NDI'],
  ['omt', 'OMT'],
  ['decklink', 'DeckLink'],
  ['aja', 'AJA (experimental, not verified on hardware)'],
  ['uvc', 'Capture'],
];

function addressLabel(kind: string): string {
  if (kind === 'srt') return 'URL';
  if (kind === 'ndi' || kind === 'omt') return 'Source';
  return 'Device';
}

function paramsFor(kind: string, address: string, channel: string): Record<string, unknown> {
  if (kind === 'srt') return { url: address };
  if (kind === 'ndi' || kind === 'omt') return { source: address };
  if (kind === 'uvc') return { device: address };
  if (kind === 'aja') return { device: Number(address) || 0, channel: Number(channel) || 0 };
  return { device: Number(address) || 0 };
}

export function InputsTab({ client, inputs }: { client: Pick<Client, 'send'>; inputs: InputStatus[] }) {
  const [name, setName] = useState('');
  const [kind, setKind] = useState('srt');
  const [address, setAddress] = useState('');
  const [channel, setChannel] = useState('0');

  function add() {
    if (!name.trim()) return;
    client.send({
      cmd: 'input.add',
      id: `in-${Date.now().toString(36)}`,
      name: name.trim(),
      kind,
      params: paramsFor(kind, address.trim(), channel),
    });
    setName('');
    setAddress('');
  }

  return (
    <div className="tab-inputs">
      <h3>Inputs</h3>
      <ul className="cfg-list">
        {inputs.map((i) => (
          <li key={i.id}>
            <span>{i.name}</span>
            <small className={`st ${i.state}`}>{i.state}</small>
            <button className="btn ghost-danger" aria-label={`Remove ${i.name}`} onClick={() => client.send({ cmd: 'input.remove', id: i.id })}>
              Remove
            </button>
          </li>
        ))}
        {inputs.length === 0 && <li className="note">No inputs configured.</li>}
      </ul>
      <h3>Add an input</h3>
      <label className="field">
        <span>Name</span>
        <input className="ctl" aria-label="New input name" value={name} onChange={(e) => setName(e.target.value)} />
      </label>
      <label className="field">
        <span>Kind</span>
        <select className="ctl" aria-label="New input kind" value={kind} onChange={(e) => setKind(e.target.value)}>
          {INPUT_KINDS.map(([v, l]) => (
            <option key={v} value={v}>
              {l}
            </option>
          ))}
        </select>
      </label>
      <label className="field">
        <span>{addressLabel(kind)}</span>
        <input className="ctl mono" aria-label={addressLabel(kind)} value={address} onChange={(e) => setAddress(e.target.value)} />
      </label>
      {kind === 'aja' && (
        <label className="field">
          <span>Channel</span>
          <input className="ctl mono" aria-label="Channel" type="number" min={0} value={channel} onChange={(e) => setChannel(e.target.value)} />
        </label>
      )}
      <div className="row-end">
        <button className="btn" onClick={add}>
          Add input
        </button>
      </div>
    </div>
  );
}