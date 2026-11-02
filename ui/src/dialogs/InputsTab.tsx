import { useState } from 'react';

import type { Client } from '../api/client';
import type { InputStatus } from '../api/types';

// "uvc" is the engine factory's kind; the operator sees "Capture".
export const INPUT_KINDS: Array<[string, string]> = [
  ['srt', 'SRT stream (over the network)'],
  ['ndi', 'NDI (video over the local network)'],
  ['omt', 'OMT (video over the local network)'],
  ['decklink', 'DeckLink card (SDI / HDMI cable)'],
  ['aja', 'AJA card (experimental, not tested on hardware)'],
  ['uvc', 'USB camera or capture device'],
];

function addressLabel(kind: string): string {
  if (kind === 'srt') return 'Stream address';
  if (kind === 'ndi' || kind === 'omt') return 'Source name';
  if (kind === 'uvc') return 'Device name';
  return 'Card number';
}

function addressHint(kind: string): string {
  if (kind === 'srt') return 'Starts with srt://, for example srt://192.168.1.20:9000';
  if (kind === 'ndi' || kind === 'omt') return 'The name the sender shows, for example OBSERVER-PC (Game)';
  if (kind === 'uvc') return 'As Windows lists it, for example Cam Link 4K';
  return '0 for the first card, 1 for the second';
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
      <h3>Cameras</h3>
      <p className="note">Every camera here is recorded the whole time, so you can replay any of them.</p>
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
        {inputs.length === 0 && <li className="note">No cameras yet. Add one below.</li>}
      </ul>
      <h3>Add a camera</h3>
      <label className="field">
        <span>Name</span>
        <input className="ctl" aria-label="New input name" placeholder="Observer 1" value={name} onChange={(e) => setName(e.target.value)} />
      </label>
      <label className="field">
        <span>Connection</span>
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
        <span className="hint">{addressHint(kind)}</span>
      </label>
      {kind === 'aja' && (
        <label className="field">
          <span>Card input number</span>
          <input className="ctl mono" aria-label="Channel" type="number" min={0} value={channel} onChange={(e) => setChannel(e.target.value)} />
        </label>
      )}
      <div className="row-end">
        <button className="btn" onClick={add}>
          Add camera
        </button>
      </div>
    </div>
  );
}