import { useEffect, useRef, useState, type MouseEvent, type WheelEvent } from 'react';

import type { Client } from '../api/client';
import type { Event, Status } from '../api/types';
import { useContextMenu } from '../ContextMenu';
import { formatClock } from '../format';
import { cameraColor, type MarkStamp } from './cameraColors';
import {
  changeCamera,
  deleteBlock,
  mediaLimits,
  moveBlock,
  moveSpeedEdge,
  moveSwitch,
  moveZoomEdge,
  resetZoom,
  setSpeed,
  slip,
  snapTime,
  split,
  trimIn,
  trimOut,
  type EventEdit,
  type Limits,
} from './timelineEdits';

export interface TimeRange {
  fromUs: number;
  toUs: number;
}

// shortcut: the UI has no show-rate field, so edits snap to this grid; read the rate from
// status when the engine exposes it.
const PERIOD_US = 100_000;

const span = (v: TimeRange) => Math.max(1, v.toUs - v.fromUs);
const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

export function timeToX(tUs: number, view: TimeRange, width = 1000): number {
  return ((tUs - view.fromUs) / span(view)) * width;
}

export function xToTime(x: number, view: TimeRange, width = 1000): number {
  return view.fromUs + (x / width) * span(view);
}

type Props = {
  client: Pick<Client, 'send'>;
  events: { currentList: string; events: Event[]; lists: Array<{ id: string; name: string; order: number[] }>; rootOrder: number[] } | null;
  status?: Status | null;
  selectedId: number | null;
  selectedIds?: number[];
  onSelect?: (primary: number | null, ids: number[]) => void;
  selectedBlock?: number | null;
  onSelectBlock?: (i: number | null) => void;
  fitKey?: number;
  pin?: MarkStamp | null;
  width?: number;
};

type Drag =
  | { kind: 'in' | 'out' | 'slip'; startX: number; startT: number; shift: boolean }
  | { kind: 'switch' | 'speed' | 'zoom'; i: number; startX: number; startT: number; shift: boolean }
  | { kind: 'block'; i: number; startX: number; startY: number; startT: number; axis: '' | 'move' | 'camera'; targetInput?: string }
  | { kind: 'view'; startX: number; fromUs: number; toUs: number };

const editOf = (e: Event): EventEdit => ({
  inUs: e.inUs,
  outUs: e.outUs,
  angles: e.angles,
  speeds: e.speeds,
  zooms: e.zooms,
});

function sameEdit(a: EventEdit, b: EventEdit): boolean {
  return JSON.stringify(a) === JSON.stringify(b);
}

// Whole-recording bar over a multicam timeline: one track per camera, blocks on the camera
// that plays, and the replay window across them (spec §4).
export function Timeline({
  client,
  events,
  status,
  selectedId,
  selectedIds = [],
  onSelect,
  selectedBlock = null,
  onSelectBlock,
  fitKey = 0,
  pin = null,
  width = 1000,
}: Props) {
  const menu = useContextMenu();
  const event = events?.events.find((e) => e.id === selectedId) ?? null;
  const ev: EventEdit | null = event ? editOf(event) : null;
  const recordedTo = Math.max(0, ...(status?.inputs ?? []).map((i) => i.coverage[i.coverage.length - 1]?.[1] ?? 0));
  const game = gameRange(event, events?.events ?? [], status);
  const lane = useRef<HTMLDivElement>(null);
  const [measured, setMeasured] = useState(0);
  const [view, setView] = useState<TimeRange>(() => (event ? { fromUs: event.inUs, toUs: event.outUs } : liveView(status)));
  const [preview, setPreview] = useState<EventEdit | null>(null);
  const [drag, setDrag] = useState<Drag | null>(null);
  const [orderDrag, setOrderDrag] = useState<number | null>(null);
  const shown = preview ?? ev;
  const w = measured || width;

  useEffect(() => {
    const el = lane.current;
    if (!el) return;
    setMeasured(el.clientWidth);
    if (typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(() => setMeasured(el.clientWidth));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  useEffect(() => {
    setPreview(null);
    setView(event ? { fromUs: event.inUs, toUs: event.outUs } : liveView(status));
    // A different replay starts from its own window; server edits to this one keep the view.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedId]);

  useEffect(() => {
    if (!fitKey || !event) return;
    const pad = Math.round((event.outUs - event.inUs) * 0.05);
    setView({ fromUs: event.inUs - pad, toUs: event.outUs + pad });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [fitKey]);

  const pct = (tUs: number) => `${((clamp(tUs, view.fromUs, view.toUs) - view.fromUs) / span(view)) * 100}%`;
  const widthPct = (a: number, b: number) =>
    `${(Math.max(0, Math.min(b, view.toUs) - Math.max(a, view.fromUs)) / span(view)) * 100}%`;
  const inView = (tUs: number | null | undefined): tUs is number => tUs != null && tUs >= view.fromUs && tUs <= view.toUs;
  const previewStatus = status?.players.preview;
  const playhead = previewStatus?.posUs;
  const pgm = status?.players.output.eventId != null ? status.players.output.posUs : null;

  const lim = (e: EventEdit): Limits => mediaLimits(status?.inputs ?? [], e, PERIOD_US);
  const targets = (e: EventEdit): number[] => [playhead ?? 0, pgm ?? 0, e.inUs, e.outUs, ...e.angles.map((a) => a.tUs)];
  const timeAt = (clientX: number) => xToTime(clientX - (lane.current?.getBoundingClientRect().left ?? 0), view, w);

  function snapped(e: EventEdit, tUs: number, shift: boolean): number {
    return snapTime(tUs, { periodUs: PERIOD_US, targets: targets(e), usPerPx: span(view) / w, snap: !shift });
  }

  function send(next: EventEdit) {
    if (!event || !ev || sameEdit(next, ev)) return;
    void client.send({
      cmd: 'event.update',
      id: event.id,
      inUs: next.inUs,
      outUs: next.outUs,
      angles: next.angles,
      speeds: next.speeds,
      zooms: next.zooms,
    });
  }

  function inputAt(e: MouseEvent): string | undefined {
    return (e.target as HTMLElement).closest<HTMLElement>('[data-input]')?.dataset.input;
  }

  function down(d: Drag) {
    setDrag(d);
  }

  function move(e: MouseEvent) {
    if (!drag) return;
    const dx = e.clientX - drag.startX;
    if (drag.kind === 'view') {
      const delta = (dx / w) * span({ fromUs: drag.fromUs, toUs: drag.toUs });
      setView({ fromUs: drag.fromUs - delta, toUs: drag.toUs - delta });
      return;
    }
    if (!ev) return;
    const shift = e.shiftKey;
    if (drag.kind === 'block') {
      const dy = e.clientY - drag.startY;
      if (!drag.axis && Math.max(Math.abs(dx), Math.abs(dy)) > 4) {
        const axis = Math.abs(dy) > Math.abs(dx) ? 'camera' : 'move';
        setDrag({ ...drag, axis });
        if (axis === 'camera') {
          const target = inputAt(e);
          if (target) setPreview(changeCamera(ev, drag.i, target));
        }
      } else if (drag.axis === 'camera') {
        const target = inputAt(e);
        if (target) {
          setDrag({ ...drag, targetInput: target });
          setPreview(changeCamera(ev, drag.i, target));
        }
      } else if (drag.axis === 'move') {
        setPreview(moveBlock(ev, drag.i, (dx / w) * span(view), lim(ev)));
      }
      return;
    }
    const at = timeAt(e.clientX);
    if (drag.kind === 'in') setPreview(trimIn(ev, snapped(ev, at, shift), lim(ev)));
    else if (drag.kind === 'out') setPreview(trimOut(ev, snapped(ev, at, shift), lim(ev)));
    else if (drag.kind === 'slip') setPreview(slip(ev, snapped(ev, ev.inUs + ((e.clientX - drag.startX) / w) * span(view), shift) - ev.inUs, lim(ev)));
    else if (drag.kind === 'switch') setPreview(moveSwitch(ev, drag.i, snapped(ev, drag.startT + (dx / w) * span(view), shift), lim(ev)));
    else if (drag.kind === 'speed') setPreview(moveSpeedEdge(ev, drag.i, snapped(ev, drag.startT + (dx / w) * span(view), shift), lim(ev)));
    else if (drag.kind === 'zoom') setPreview(moveZoomEdge(ev, drag.i, snapped(ev, drag.startT + (dx / w) * span(view), shift), lim(ev)));
  }

  function up(e: MouseEvent) {
    if (drag && ev) {
      const dx = e.clientX - drag.startX;
      const shift = e.shiftKey;
      let next = ev;
      if (drag.kind === 'in') next = trimIn(ev, snapped(ev, timeAt(e.clientX), shift), lim(ev));
      else if (drag.kind === 'out') next = trimOut(ev, snapped(ev, timeAt(e.clientX), shift), lim(ev));
      else if (drag.kind === 'slip') next = slip(ev, snapped(ev, ev.inUs + (dx / w) * span(view), shift) - ev.inUs, lim(ev));
      else if (drag.kind === 'switch') next = moveSwitch(ev, drag.i, snapped(ev, drag.startT + (dx / w) * span(view), shift), lim(ev));
      else if (drag.kind === 'speed') next = moveSpeedEdge(ev, drag.i, snapped(ev, drag.startT + (dx / w) * span(view), shift), lim(ev));
      else if (drag.kind === 'zoom') next = moveZoomEdge(ev, drag.i, snapped(ev, drag.startT + (dx / w) * span(view), shift), lim(ev));
      else if (drag.kind === 'block') {
        const dy = e.clientY - drag.startY;
        const axis = drag.axis || (Math.max(Math.abs(dx), Math.abs(dy)) > 4 ? (Math.abs(dy) > Math.abs(dx) ? 'camera' : 'move') : '');
        if (axis === 'camera') {
          const target = drag.targetInput ?? inputAt(e);
          if (target) next = changeCamera(ev, drag.i, target);
        } else if (axis === 'move') {
          next = moveBlock(ev, drag.i, snapped(ev, drag.startT + (dx / w) * span(view), shift) - drag.startT, lim(ev));
        }
      }
      send(next);
    }
    setPreview(null);
    setDrag(null);
  }

  function wheel(e: WheelEvent) {
    e.preventDefault();
    if (e.ctrlKey) {
      const at = timeAt(e.clientX);
      const wanted = clamp(span(view) * (e.deltaY < 0 ? 0.8 : 1.25), 1_000_000, Math.max(1_000_000, span(game)));
      const from = at - (timeAt(e.clientX) - view.fromUs) * (wanted / span(view));
      setView({ fromUs: from, toUs: from + wanted });
    } else {
      const delta = (e.deltaY > 0 ? 1 : -1) * span(view) * 0.1;
      setView({ fromUs: view.fromUs + delta, toUs: view.toUs + delta });
    }
  }

  const ordered = (events?.rootOrder ?? []).concat(...(events?.lists ?? []).map((l) => l.order));
  const selectedEvents = ordered
    .filter((id) => selectedIds.includes(id))
    .map((id) => events?.events.find((e) => e.id === id))
    .filter((e): e is Event => e != null);

  return (
    <section
      className="tl"
      aria-label="Timeline"
      data-from={Math.round(view.fromUs)}
      data-to={Math.round(view.toUs)}
      onMouseMove={move}
      onMouseUp={up}
      onMouseLeave={() => {
        setDrag(null);
        setPreview(null);
      }}
      onWheel={wheel}
    >
      <div className="tl-row tl-top">
        <span className="tl-lab" />
        <div className="tl-actions">
          <button aria-label="Undo" disabled={!status?.edit?.canUndo} onClick={() => void client.send({ cmd: 'edit.undo' })}>
            ↶
          </button>
          <button aria-label="Redo" disabled={!status?.edit?.canRedo} onClick={() => void client.send({ cmd: 'edit.redo' })}>
            ↷
          </button>
        </div>
      </div>
      <div className="tl-row game">
        <span className="tl-lab" title="The whole recording. Yellow ticks are saved replays; the box is the part shown below.">Whole recording</span>
        <div className="tl-lane">
          {(events?.events ?? []).map((e) => (
            <span key={e.id} className="mk" style={{ left: pct(e.inUs) }} />
          ))}
          <span
            className="win"
            style={{ left: `${((view.fromUs - game.fromUs) / span(game)) * 100}%`, width: `${(span(view) / span(game)) * 100}%` }}
            onMouseDown={(e) => {
              e.preventDefault();
              down({ kind: 'view', startX: e.clientX, fromUs: view.fromUs, toUs: view.toUs });
            }}
          />
        </div>
      </div>

      <div className="tl-row ruler-row">
        <span className="tl-lab" />
        <div className="ruler">
          {[0, 0.25, 0.5, 0.75, 1].map((f) => (
            <span key={f}>{formatClock(view.fromUs + span(view) * f)}</span>
          ))}
        </div>
      </div>

      {selectedEvents.length > 1 && (
        <div className="tl-row order-row">
          <span className="tl-lab">Play order</span>
          <div className="tl-lane order-lane">
            {selectedEvents.map((e) => (
              <span
                key={e.id}
                data-testid={`order-${e.id}`}
                className={`order${orderDrag === e.id ? ' dragging' : ''}`}
                onMouseDown={() => setOrderDrag(e.id)}
                onMouseUp={() => {
                  if (orderDrag != null && orderDrag !== e.id) {
                    const moving = events?.events.find((x) => x.id === orderDrag);
                    void client.send({ cmd: 'event.move', ids: [orderDrag], listId: e.listId, beforeId: e.id });
                    if (moving) onSelect?.(orderDrag, selectedIds);
                  }
                  setOrderDrag(null);
                }}
                onClick={() => onSelect?.(e.id, selectedIds)}
              >
                {e.name || `Replay ${e.id}`}
              </span>
            ))}
          </div>
        </div>
      )}

      <div className="tl-tracks">
        <div className="tl-labels">
          {(status?.inputs ?? []).map((input, index) => (
            <span className="tl-lab cams-lab" key={input.id} title={`${input.name}: faint = recorded, red = signal was missing`}>
              <b className="num" style={{ background: cameraColor(index) }}>{index + 1}</b> {input.name}
            </span>
          ))}
        </div>
        <div className="tl-lanes" ref={lane}>
          {(status?.inputs ?? []).map((input, index) => {
            const colour = cameraColor(index);
            const target = previewStatus?.angle === input.id;
            return (
              <div
                className={`tl-lane lane${target ? ' armed' : ''}`}
                key={input.id}
                data-testid={`track-${input.id}`}
                data-input={input.id}
                style={target ? { boxShadow: `inset 0 0 0 2px ${colour}` } : undefined}
              >
                <span
                  className="lane-hit"
                  onContextMenu={(e) => {
                    e.preventDefault();
                    const r = e.currentTarget.getBoundingClientRect();
                    const t = Math.round(xToTime(e.clientX - r.left, view, r.width || w));
                    menu(e, [
                      { label: `Show ${formatClock(t)} in Preview`, onSelect: () => void client.send({ cmd: 'player.cueAt', player: 'preview', t, angle: input.id }) },
                      { label: 'Back to live (what is happening now)', onSelect: () => void client.send({ cmd: 'player.live', player: 'preview' }) },
                    ]);
                  }}
                />
                {input.coverage.map(([from, to], i) => (
                  <span key={`c${i}`} className="rec" style={{ left: pct(from), width: widthPct(from, to) }} />
                ))}
                {gaps(input.coverage, { fromUs: view.fromUs, toUs: Math.min(view.toUs, recordedTo) }).map(([from, to], i) => (
                  <span key={`g${i}`} className="gap" style={{ left: pct(from), width: widthPct(from, to) }} />
                ))}
                {shown?.angles.map((a, i) =>
                  a.inputId === input.id ? (
                    <span
                      key={i}
                      data-testid={`block-${i}`}
                      className={`seg${selectedBlock === i ? ' sel' : ''}`}
                      style={{ left: pct(a.tUs), width: widthPct(a.tUs, shown.angles[i + 1]?.tUs ?? shown.outUs), background: colour }}
                      onMouseDown={(e) => {
                        e.preventDefault();
                        onSelectBlock?.(i);
                        const startT = i === 0 ? (shown.angles[1]?.tUs ?? shown.outUs) : a.tUs;
                        down({ kind: 'block', i, startX: e.clientX, startY: e.clientY, startT, axis: '' });
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        if (!event || !ev) return;
                        const at = playhead ?? ev.inUs;
                        menu(e, [
                          { label: 'Split at playhead', onSelect: () => send(split(ev, at)) },
                          { label: 'Delete block', onSelect: () => send(deleteBlock(ev, i)) },
                        ]);
                      }}
                    >
                      {inputName(status, a.inputId)}
                    </span>
                  ) : null,
                )}
                {shown?.angles.map((a, i) =>
                  i > 0 && a.inputId === input.id ? (
                    <span
                      key={`s${i}`}
                      data-testid={`switch-${i}`}
                      className="switch"
                      style={{ left: pct(a.tUs) }}
                      onMouseDown={(e) => {
                        e.preventDefault();
                        down({ kind: 'switch', i, startX: e.clientX, startT: a.tUs, shift: e.shiftKey });
                      }}
                    />
                  ) : null,
                )}
                {pin && pin.inputId === input.id && <span className="pin" style={{ left: pct(pin.tUs), background: colour }} />}
              </div>
            );
          })}

          {shown && (status?.inputs ?? []).length > 0 && (
            <span className="inout" style={{ left: pct(shown.inUs), width: widthPct(shown.inUs, shown.outUs) }}>
              <span
                data-testid="slip-bar"
                className="slip"
                onMouseDown={(e) => {
                  e.preventDefault();
                  down({ kind: 'slip', startX: e.clientX, startT: shown.inUs, shift: e.shiftKey });
                }}
              />
              <span
                data-testid="handle-in"
                className="handle in"
                onMouseDown={(e) => {
                  e.preventDefault();
                  down({ kind: 'in', startX: e.clientX, startT: shown.inUs, shift: e.shiftKey });
                }}
              />
              <span
                data-testid="handle-out"
                className="handle out"
                onMouseDown={(e) => {
                  e.preventDefault();
                  down({ kind: 'out', startX: e.clientX, startT: shown.outUs, shift: e.shiftKey });
                }}
              />
            </span>
          )}

          {inView(playhead) && <span className="ph pvw" data-testid="ph-pvw" style={{ left: pct(playhead) }} />}
          {inView(pgm) && <span className="ph pgm" data-testid="ph-pgm" style={{ left: pct(pgm) }} />}
        </div>
      </div>

      {shown && (
        <div className="tl-row">
          <span className="tl-lab" title="Replay speed along the replay. Striped parts are slow motion.">Speed</span>
          <div className="tl-lane speedlane">
            {shown.speeds.map((s, i) => (
              <span
                key={i}
                className={`spd${s.speed < 0.99 ? ' slow' : ''}`}
                style={{ left: pct(s.tUs), width: widthPct(s.tUs, shown.speeds[i + 1]?.tUs ?? shown.outUs) }}
                onContextMenu={(e) => {
                  e.preventDefault();
                  menu(e, [1, 0.75, 0.5, 0.25].map((speed) => ({
                    label: `${Math.round(speed * 100)}%`,
                    onSelect: () => send(setSpeed(ev!, i, speed)),
                  })));
                }}
              >
                {Math.round(s.speed * 100)}%
                {i > 0 && (
                  <span
                    data-testid={`speed-edge-${i}`}
                    className="edge"
                    onMouseDown={(e) => {
                      e.preventDefault();
                      down({ kind: 'speed', i, startX: e.clientX, startT: s.tUs, shift: e.shiftKey });
                    }}
                  />
                )}
              </span>
            ))}
          </div>
        </div>
      )}

      <div className="tl-row">
        <span className="tl-lab" title="Zoom along the replay. Drag an edge; right-click a segment to reset it.">Zoom</span>
        <div className="tl-lane speedlane">
          {shown?.zooms.map((z, i) => (
            <span
              key={i}
              className="spd"
              style={{ left: pct(z.tUs), width: widthPct(z.tUs, shown.zooms[i + 1]?.tUs ?? shown.outUs) }}
              onContextMenu={(e) => {
                e.preventDefault();
                menu(e, [{ label: 'Reset zoom', onSelect: () => send(resetZoom(ev!, i)) }]);
              }}
            >
              {z.rect.scale !== 1 ? `×${z.rect.scale.toFixed(1)}` : ''}
              {i > 0 && (
                <span
                  data-testid={`zoom-edge-${i}`}
                  className="edge"
                  onMouseDown={(e) => {
                    e.preventDefault();
                    down({ kind: 'zoom', i, startX: e.clientX, startT: z.tUs, shift: e.shiftKey });
                  }}
                />
              )}
            </span>
          ))}
        </div>
      </div>
    </section>
  );
}

function inputName(status: Status | null | undefined, id: string): string {
  return status?.inputs.find((i) => i.id === id)?.name || id;
}

function liveView(status?: Status | null): TimeRange {
  const pos = status?.players.preview.posUs ?? 0;
  return { fromUs: pos - 25_000_000, toUs: pos + 5_000_000 };
}

function gameRange(event: Event | null, all: Event[], status?: Status | null): TimeRange {
  const times = all.flatMap((e) => [e.inUs, e.outUs]);
  if (event) times.push(event.inUs, event.outUs);
  for (const input of status?.inputs ?? []) for (const [a, b] of input.coverage) times.push(a, b);
  const newest = status?.players.preview.posUs ?? 0;
  if (times.length === 0) return { fromUs: Math.max(0, newest - 30_000_000), toUs: newest || 30_000_000 };
  return { fromUs: Math.min(...times), toUs: Math.max(...times, newest) };
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
