import { useEffect, useState } from 'react';

import type { Client } from './api/client';
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
  const [hotkeys, setHotkeys] = useState<Record<string, string>>({});

  useEffect(() => {
    const offStatus = client.onStatus(setStatus);
    const offEvents = client.onEvents(setEvents);
    return () => {
      offStatus();
      offEvents();
    };
  }, [client]);

  useEffect(() => {
    if (!events?.events.length) return;
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
  const anchor = event?.inUs ?? status?.players.preview.posUs ?? 0;
  const view = { fromUs: anchor - 5_000_000, toUs: anchor + 25_000_000 };

  return (
    <div className="app">
      <header className="top">
        <b>BreadReplay</b>
        <span className={`pill ${status?.recording ? 'live' : 'offline'}`}>
          <i />
          {status?.recording ? 'Recording' : 'Stopped'}
        </span>
        {zoomMode && <span className="tag-note">Zoom mode</span>}
        <div className="right">
          <button className="btn" onClick={() => client.send({ cmd: status?.recording ? 'record.stop' : 'record.start' })}>
            {status?.recording ? 'Stop recording' : 'Start recording'}
          </button>
          <button className="btn" onClick={() => setSettings(true)}>
            Settings
          </button>
        </div>
      </header>

      <div className="main">
        <div className="stage">
          <Monitors status={status} fps={FPS} client={client} />
          <AngleRow status={status} client={client} />
          <Timeline client={client} events={events} selectedId={selected} status={status} view={view} />
          <ControlsBar client={client} lastN={defaultSettings.lastNSeconds} />
        </div>
        <SpeedLever status={status} client={client} />
        <EventList client={client} events={events} selectedId={selected} onSelect={setSelected} />
      </div>

      <StatusBar status={status} />
      {settings && (
        <Settings
          client={client}
          settings={{ ...defaultSettings, hotkeys }}
          status={status}
          onClose={() => setSettings(false)}
          onHotkeys={setHotkeys}
        />
      )}
    </div>
  );
}