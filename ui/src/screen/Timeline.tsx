import { useEffect, useRef, useState, type MouseEvent } from 'react';

import type { Client } from '../api/client';
import type { Event, Status } from '../api/types';
import { useContextMenu } from '../ContextMenu';
import { formatClock } from '../format';
import { cameraColor, cameraIndex, type MarkStamp } from './cameraColors';

export interface TimeRange {
  fromUs: number;
  toUs: number;
}

const span = (v: TimeRange) => Math.max(1, v.toUs - v.fromUs);

export function timeToX(tUs: number, view: TimeRange, width = 1000): number {
  return ((tUs - view.fromUs) / span(view)) * width;
}

export function xToTime(x: number, view: TimeRange, width = 1000): number {
  return view.fromUs + (x / width) * span(view);
}

type Props = {
  client: Pick<Client, 'send'>;
  events: { currentList: string; events: Event[]; lists: Array<{ id: string; name: string; order: number[] }> } | null;
  selectedId: number | null;
  status?: Status | null;
  view?: TimeRange;
  width?: number;
  pin?: MarkStamp | null;
};

type Drag = { index: number; startX: number; startT: number; tUs: number };

// Whole-game bar over a zoomed (≈30 s) track with the event's angle/speed blocks and one
// recording lane per input (spec §7).
export function Timeline({ client, events, selectedId, status, view, width = 1000, pin = null }: Props) {
  const menu = useContextMenu();
  const event = events?.events.find((e) => e.id === selectedId) ?? null;
  const range = view ?? gameRange(event, events?.events ?? [], status);
  // Nothing is recorded past the newest frame: lanes stop there instead of showing a red gap.
  const recordedTo = Math.max(0, ...(status?.inputs ?? []).map((i) => i.coverage[i.coverage.length - 1]?.[1] ?? 0));
  const track = useRef<HTMLDivElement>(null);
  const [measured, setMeasured] = useState(0);
  const [drag, setDrag] = useState<Drag | null>(null);
  const w = measured || width;

  useEffect(() => {
    const el = track.current;
    if (!el) return;
    setMeasured(el.clientWidth);
    if (typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(() => setMeasured(el.clientWidth));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const pct = (tUs: number) => `${((tUs - range.fromUs) / span(range)) * 100}%`;
  const widthPct = (a: number, b: number) => `${((Math.min(b, range.toUs) - Math.max(a, range.fromUs)) / span(range)) * 100}%`;

  const angles = event?.angles ?? [];
  const at = (i: number) => (drag?.index === i ? drag.tUs : angles[i].tUs);
  const blockEnd = (i: number) => (i + 1 < angles.length ? at(i + 1) : (event?.outUs ?? range.toUs));

  function move(e: MouseEvent) {
    if (!drag || !event) return;
    const prev = drag.index > 0 ? angles[drag.index - 1].tUs : event.inUs;
    const next = drag.index + 1 < angles.length ? angles[drag.index + 1].tUs : event.outUs;
    const t = Math.max(prev + 1, Math.min(next - 1, drag.startT + ((e.clientX - drag.startX) / w) * span(range)));
    setDrag({ ...drag, tUs: t });
  }

  function up() {
    if (drag && event && drag.tUs !== drag.startT) {
      client.send({ cmd: 'event.update', id: event.id, angles: angles.map((a, i) => (i === drag.index ? { ...a, tUs: drag.tUs } : a)) });
    }
    setDrag(null);
  }

  const game = gameRange(null, events?.events ?? [], status);
  const speeds = event?.speeds ?? [];
  const preview = status?.players.preview;
  const playhead = preview?.posUs;
  const pgm = status?.players.output?.eventId != null ? status.players.output.posUs : null;
  const inView = (tUs: number | null | undefined): tUs is number => tUs != null && tUs >= range.fromUs && tUs <= range.toUs;

  return (
    <section className="tl" aria-label="Timeline">
      <div className="tl-row game">
        <span className="tl-lab" title="The whole recording. Yellow ticks are saved replays; the box is the part shown below.">Whole recording</span>
        <div className="tl-lane">
          {(events?.events ?? []).map((e) => (
            <span key={e.id} className="mk" style={{ left: pct(e.inUs) }} />
          ))}
          <span className="win" style={{ left: `${((range.fromUs - game.fromUs) / span(game)) * 100}%`, width: `${(span(range) / span(game)) * 100}%` }} />
        </div>
      </div>

      <div className="tl-row ruler-row">
        <span className="tl-lab" />
        <div className="ruler">
          {[0, 0.25, 0.5, 0.75, 1].map((f) => (
            <span key={f}>{formatClock(range.fromUs + span(range) * f)}</span>
          ))}
        </div>
      </div>

      <div className="tl-row" ref={track} data-testid="timeline-track" onMouseMove={move} onMouseUp={up} onMouseLeave={up}>
        <span className="tl-lab" title="The selected replay: coloured blocks show which camera plays when. Drag a block edge to move a camera switch.">{event ? `Replay ${event.id}` : 'No replay'}</span>
        <div
          className="tl-lane evlane"
          onContextMenu={(e) => {
            const r = e.currentTarget.getBoundingClientRect();
            const t = Math.round(xToTime(e.clientX - r.left, range, r.width));
            menu(e, [
              { label: `Show ${formatClock(t)} in Preview`, onSelect: () => void client.send({ cmd: 'player.cueAt', player: 'preview', t }) },
              { label: 'Back to live (what is happening now)', onSelect: () => void client.send({ cmd: 'player.live', player: 'preview' }) },
            ]);
          }}
        >
          {event && (
            <>
              <span className="inout" style={{ left: pct(event.inUs), width: widthPct(event.inUs, event.outUs) }} />
              {angles.map((a, i) => (
                <span
                  key={i}
                  className={`seg${speeds[i] && speeds[i].speed < 0.99 ? ' slow' : ''}`}
                  data-testid={`angle-block-${i}`}
                  style={{ left: pct(at(i)), width: widthPct(at(i), blockEnd(i)), background: cameraColor(cameraIndex(status, a.inputId)) }}
                  onMouseDown={(e) => {
                    e.preventDefault();
                    setDrag({ index: i, startX: e.clientX, startT: angles[i].tUs, tUs: angles[i].tUs });
                  }}
                >
                  {inputName(status, a.inputId)}
                </span>
              ))}
              {inView(playhead) && <span className="ph pvw" style={{ left: pct(playhead) }} />}
              {inView(pgm) && <span className="ph pgm" style={{ left: pct(pgm) }} />}
            </>
          )}
        </div>
      </div>

      {event && speeds.length > 0 && (
        <div className="tl-row">
          <span className="tl-lab" title="Replay speed along the replay. Striped parts are slow motion.">Speed</span>
          <div className="tl-lane speedlane">
            {speeds.map((s, i) => (
              <span key={i} className={`spd${s.speed < 0.99 ? ' slow' : ''}`} style={{ left: pct(s.tUs), width: widthPct(s.tUs, speeds[i + 1]?.tUs ?? event.outUs) }}>
                {Math.round(s.speed * 100)}%
              </span>
            ))}
          </div>
        </div>
      )}

      {(status?.inputs ?? []).map((input, index) => {
        const colour = cameraColor(index);
        const target = preview?.angle === input.id;
        return (
        <div className="tl-row cams" key={input.id}>
          <span className="tl-lab" title={`${input.name}: green = recorded, red = signal was missing`}>
            {input.name}
          </span>
          <div className={`tl-lane lane${target ? ' armed' : ''}`} style={target ? { boxShadow: `inset 0 0 0 2px ${colour}` } : undefined}>
            {input.coverage.map(([from, to], i) => (
              <span key={`c${i}`} className="rec" style={{ left: pct(from), width: widthPct(from, to) }} />
            ))}
            {gaps(input.coverage, { fromUs: range.fromUs, toUs: Math.min(range.toUs, recordedTo) }).map(([from, to], i) => (
              <span key={`g${i}`} className="gap" style={{ left: pct(from), width: widthPct(from, to) }} />
            ))}
            {pin && pin.inputId === input.id && (
              <span className="pin" style={{ left: pct(pin.tUs), background: colour }} />
            )}
            {inView(playhead) && <span className="ph pvw" style={{ left: pct(playhead) }} />}
            {inView(pgm) && <span className="ph pgm" style={{ left: pct(pgm) }} />}
          </div>
        </div>
        );
      })}
    </section>
  );
}

function inputName(status: Status | null | undefined, id: string): string {
  return status?.inputs.find((i) => i.id === id)?.name || id;
}

function gameRange(event: Event | null, all: Event[], status?: Status | null): TimeRange {
  const times = all.flatMap((e) => [e.inUs, e.outUs]);
  if (event) times.push(event.inUs, event.outUs);
  const newest = status?.players.preview.posUs ?? 0;
  if (times.length === 0) return { fromUs: Math.max(0, newest - 30_000_000), toUs: newest || 30_000_000 };
  return { fromUs: Math.min(...times), toUs: Math.max(...times, event?.outUs ?? 0) };
}

function gaps(coverage: Array<[number, number]>, view: TimeRange): Array<[number, number]> {
  const ranges = coverage
    .map(([a, b]) => [Math.max(a, view.fromUs), Math.min(b, view.toUs)] as [number, number])
    .filter(([a, b]) => b > a)
    .sort((x, y) => x[0] - y[0]);
  const out: Array<[number, number]> = [];
  let cursor = view.fromUs;
  for (const [a, b] of ranges) {
    if (a > cursor) out.push([cursor, a]);
    cursor = Math.max(cursor, b);
  }
  if (cursor < view.toUs) out.push([cursor, view.toUs]);
  return out;
}