import { useEffect, useRef, useState } from 'react';

import type { Client } from './api/client';
import { ContextMenuHost } from './ContextMenu';
import { hotkeyMap, keyToCommand } from './hotkeys';
import { Settings, defaultSettings } from './dialogs/Settings';
import { AngleRow } from './screen/AngleRow';
import { ControlsBar } from './screen/ControlsBar';
import { EventList } from './screen/EventList';
import { Monitors } from './screen/Monitors';
import { SpeedLever } from './screen/SpeedLever';
import { StatusBar } from './screen/StatusBar';
import { Timeline } from './screen/Timeline';
import { setEvents, setStatus, useEvents, useStatus } from './state/store';

const FPS = 60_000 / 1001;

// Everything below the engine connection; tests render this with a fake client.
export function App({ client }: { client: Client }) {
  const status = useStatus();
  const events = useEvents();
  const [selected, setSelected] = useState<number | null>(null);
  const [settings, setSettings] = useState(false);
  const [zoomMode, setZoomMode] = useState(false);
  const [savedHotkeys, setSavedHotkeys] = useState<Record<string, string>>({});
  const hotkeys = status?.hotkeys ?? savedHotkeys;

  useEffect(() => {
    const offStatus = client.onStatus(setStatus);
    const offEvents = client.onEvents(setEvents);
    return () => {
      offStatus();
      offEvents();
    };
  }, [client]);

  // A newly marked event becomes the selected one (the operator just made it); otherwise keep the
  // selection while it exists.
  const newestSeen = useRef(0);
  useEffect(() => {
    if (!events?.events.length) return;
    const newest = Math.max(...events.events.map((e) => e.id));
    const isNew = newestSeen.current > 0 && newest > newestSeen.current;
    newestSeen.current = Math.max(newestSeen.current, newest);
    if (isNew) return setSelected(newest);
    if (selected != null && events.events.some((e) => e.id === selected)) return;
    setSelected(events.events[events.events.length - 1].id);
  }, [events, selected]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (settings) return;
      const cmd = keyToCommand(e, hotkeyMap(hotkeys), {
        inputs: status?.inputs,
        previewLocked: status?.players.preview.locked,
        previewPlaying: status?.players.preview.playing,
        selectedEventId: selected,
        fps: FPS,
      });
      if (!cmd) return;
      e.preventDefault();
      if (cmd.cmd === 'ui.zoomMode') setZoomMode((z) => !z);
      else void client.send(cmd);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [client, status, selected, settings, hotkeys]);

  const event = events?.events.find((e) => e.id === selected) ?? null;
  const pos = status?.players.preview.posUs ?? 0;
  // An event: its In with a little lead, wide enough for its Out. No event: the last 25 s up to
  // Preview, so the live edge sits near the right instead of 25 s of empty future.
  const view = event
    ? { fromUs: event.inUs - 5_000_000, toUs: Math.max(event.inUs + 25_000_000, event.outUs + 3_000_000) }
    : { fromUs: pos - 25_000_000, toUs: pos + 5_000_000 };
  const lastN = defaultSettings.lastNSeconds;

  return (
    <ContextMenuHost>
    <div className="app">
      <header className="top">
        <b>BreadReplay</b>
        <span className={`pill ${status?.recording ? 'live' : 'offline'}`}>
          <i />
          {status?.recording ? 'Recording' : 'Not recording'}
        </span>
        {zoomMode && <span className="tag-note">Zoom mode: drag on Preview to zoom in</span>}
        <div className="right">
          <button
            className="btn"
            title={status?.recording ? 'Stops saving the cameras to disk. You cannot make replays while it is stopped.' : 'Starts saving every camera to disk so you can make replays.'}
            onClick={() => client.send({ cmd: status?.recording ? 'record.stop' : 'record.start' })}
          >
            {status?.recording ? 'Stop recording' : 'Start recording'}
          </button>
          <button className="btn" title="Cameras, output, recording and keyboard shortcuts." onClick={() => setSettings(true)}>
            Settings
          </button>
        </div>
      </header>

      <div className="main">
        <div className="stage">
          <Monitors status={status} fps={FPS} client={client} events={events} hotkeys={hotkeys} lastN={lastN} />
          <AngleRow status={status} client={client} hotkeys={hotkeys} />
          <Timeline client={client} events={events} selectedId={selected} status={status} view={view} />
          <ControlsBar client={client} lastN={lastN} hotkeys={hotkeys} />
        </div>
        <SpeedLever status={status} client={client} hotkeys={hotkeys} />
        <EventList client={client} events={events} selectedId={selected} onSelect={setSelected} hotkeys={hotkeys} />
      </div>

      <StatusBar status={status} />
      {settings && (
        <Settings
          client={client}
          settings={{ ...defaultSettings, hotkeys }}
          status={status}
          onClose={() => setSettings(false)}
          onHotkeys={setSavedHotkeys}
        />
      )}
    </div>
    </ContextMenuHost>
  );
}