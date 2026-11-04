import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';

import type { Client, Command } from './api/client';
import { ContextMenuHost } from './ContextMenu';
import { hotkeyMap, keyToCommand } from './hotkeys';
import { Settings, defaultSettings } from './dialogs/Settings';
import { AngleRow } from './screen/AngleRow';
import { cameraColor, cameraIndex, type MarkStamp } from './screen/cameraColors';
import { ControlsBar } from './screen/ControlsBar';
import { EventList } from './screen/EventList';
import { Monitors } from './screen/Monitors';
import { nativeVideoRequested, startNativeVideo } from './screen/nativeVideoRegistry';
import { groupOf } from './screen/selection';
import { SpeedLever } from './screen/SpeedLever';
import { StatusBar } from './screen/StatusBar';
import { Timeline } from './screen/Timeline';
import { deleteBlock, split } from './screen/timelineEdits';
import { TitleBar } from './screen/TitleBar';
import { setEvents, setStatus, useEvents, useStatus } from './state/store';

const FPS = 60_000 / 1001;

// Everything below the engine connection; tests render this with a fake client.
export function App({ client }: { client: Client }) {
  const status = useStatus();
  const events = useEvents();
  const native = nativeVideoRequested() && status?.nativeVideo === true;
  const haveStatus = status !== null;
  const [selected, setSelected] = useState<number | null>(null);
  const [selectedIds, setSelectedIds] = useState<number[]>([]);
  const [selectedBlock, setSelectedBlock] = useState<number | null>(null);
  const [fitKey, setFitKey] = useState(0);
  const [stamp, setStamp] = useState<MarkStamp | null>(null);
  const [lastMark, setLastMark] = useState<MarkStamp | null>(null);
  const stampTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const [settings, setSettings] = useState(false);
  const [zoomMode, setZoomMode] = useState(false);
  const [savedHotkeys, setSavedHotkeys] = useState<Record<string, string>>({});
  const hotkeys = status?.hotkeys ?? savedHotkeys;

  useEffect(() => () => clearTimeout(stampTimer.current), []);

  // Every Mark (buttons, keys, Preview menu) sets the stamp from the engine's reply.
  const sendMark = useCallback((cmd: Command) => {
    void client.send(cmd).then((res) => {
      if (!res.ok) return;
      const mark: MarkStamp = {
        edge: res.edge === 'in' ? 'in' : 'out',
        inputId: String(res.inputId ?? ''),
        tUs: Number(res.tUs ?? 0),
      };
      setStamp(mark);
      setLastMark(mark);
      clearTimeout(stampTimer.current);
      stampTimer.current = setTimeout(() => setStamp(null), 1500);
    });
  }, [client]);

  useEffect(() => {
    const offStatus = client.onStatus(setStatus);
    const offEvents = client.onEvents(setEvents);
    return () => {
      offStatus();
      offEvents();
    };
  }, [client]);

  useLayoutEffect(() => {
    document.documentElement.classList.toggle('native-video', native);
    return () => document.documentElement.classList.remove('native-video');
  }, [native]);
  useEffect(() => (native ? startNativeVideo(client) : undefined), [client, native]);
  // Fallback mode (no flag, or the engine says why native video is off) asks for H.264/raw
  // pictures once per connection; the client resends on every reconnect.
  useEffect(() => {
    if (native || !haveStatus) return;
    void client.wantPreviews();
  }, [client, native, haveStatus]);

  // A newly marked event becomes the selected one (the operator just made it); otherwise keep the
  // selection while it exists.
  const newestSeen = useRef(0);
  useEffect(() => {
    if (!events?.events.length) return;
    const newest = Math.max(...events.events.map((e) => e.id));
    const isNew = newestSeen.current > 0 && newest > newestSeen.current;
    newestSeen.current = Math.max(newestSeen.current, newest);
    if (isNew) {
      setSelected(newest);
      setSelectedIds([newest]);
      return;
    }
    if (selected != null && events.events.some((e) => e.id === selected)) return;
    const last = events.events[events.events.length - 1].id;
    setSelected(last);
    setSelectedIds([last]);
  }, [events, selected]);

  const event = events?.events.find((e) => e.id === selected) ?? null;
  const pos = status?.players.preview.posUs ?? 0;
  const periodUs = Math.round(1_000_000 / FPS);

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
      if (cmd.cmd === 'ui.zoomMode') {
        setZoomMode((z) => !z);
      } else if (cmd.cmd === 'ui.selectAll') {
        if (!events) return;
        const group = selected != null ? groupOf(events, selected) : events.currentList;
        const ids = (group ? events.lists.find((l) => l.id === group)?.order : events.rootOrder) ?? [];
        setSelectedIds(ids);
        if (selected == null || !ids.includes(selected)) setSelected(ids[0] ?? null);
      } else if (cmd.cmd === 'ui.split') {
        if (!event) return;
        const next = split(event, pos, periodUs);
        void client.send({ cmd: 'event.update', id: event.id, ...next });
      } else if (cmd.cmd === 'ui.fit') {
        setFitKey((k) => k + 1);
      } else if (cmd.cmd === 'ui.delete') {
        if (!event) return;
        if (selectedBlock != null) {
          void client.send({ cmd: 'event.update', id: event.id, ...deleteBlock(event, selectedBlock) });
        } else if (selectedIds.length) {
          void client.send({ cmd: 'event.delete', ids: selectedIds });
        }
      } else if (cmd.cmd.startsWith('edit.')) {
        void client.send(cmd);
      } else if (cmd.cmd.startsWith('mark.')) {
        sendMark(cmd);
      } else {
        void client.send(cmd);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [client, status, selected, settings, hotkeys, events, sendMark, event, pos, periodUs, selectedBlock, selectedIds]);

  useEffect(() => setSelectedBlock(null), [selected]);

  const lastN = defaultSettings.lastNSeconds;
  const previewIndex = cameraIndex(status, status?.players.preview.angle);
  const target = previewIndex >= 0 ? { num: previewIndex + 1, color: cameraColor(previewIndex) } : undefined;

  return (
    <ContextMenuHost>
    <div className="app">
      <TitleBar />
      <header className="top">
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
          <Monitors status={status} fps={FPS} client={client} events={events} hotkeys={hotkeys} lastN={lastN} native={native} stamp={stamp} onMark={sendMark} />
          <AngleRow status={status} client={client} hotkeys={hotkeys} native={native} />
          <Timeline
            client={client}
            events={events}
            selectedId={selected}
            selectedIds={selectedIds}
            onSelect={(primary, ids) => {
              setSelected(primary);
              setSelectedIds(ids);
            }}
            selectedBlock={selectedBlock}
            onSelectBlock={setSelectedBlock}
            fitKey={fitKey}
            status={status}
            pin={lastMark}
          />
          <ControlsBar client={client} lastN={lastN} hotkeys={hotkeys} target={target} onMark={sendMark} />
        </div>
        <SpeedLever status={status} client={client} hotkeys={hotkeys} />
        <EventList
          client={client}
          events={events}
          selectedId={selected}
          selectedIds={selectedIds}
          onSelect={(primary, ids) => {
            setSelected(primary);
            setSelectedIds(ids);
          }}
          hotkeys={hotkeys}
        />
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