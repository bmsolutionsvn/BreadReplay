import { Fragment, useEffect, useState, type MouseEvent as ReactMouseEvent } from 'react';

import type { Client } from '../api/client';
import type { Event, EventList as Group, EventStore } from '../api/types';
import { useContextMenu } from '../ContextMenu';
import type { ExportTarget } from '../dialogs/ExportPanel';
import { formatClock, formatDuration } from '../format';
import { keyLabel } from '../hotkeys';
import { displayOrder, groupOf, rangeIds, toggleId } from './selection';

type Props = {
  client: Pick<Client, 'send'>;
  events: EventStore | null;
  selectedId: number | null;
  selectedIds: number[];
  onSelect: (primary: number | null, ids: number[]) => void;
  hotkeys?: Record<string, string>;
  onExport?: (what: ExportTarget) => void;
};

// Root rows first, then a header and its rows per group. Click loads a replay; Ctrl/Shift build
// a selection; dragging moves replays between groups (spec §6, §7).
export function EventList({ client, events, selectedId, selectedIds, onSelect, hotkeys = {}, onExport }: Props) {
  const menu = useContextMenu();
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
  const [editing, setEditing] = useState<string | null>(null);
  const [draft, setDraft] = useState('');
  const [confirmDelete, setConfirmDelete] = useState<string | null>(null);
  const [dragId, setDragId] = useState<number | null>(null);

  useEffect(() => {
    if (confirmDelete === null) return;
    const t = setTimeout(() => setConfirmDelete(null), 4000);
    return () => clearTimeout(t);
  }, [confirmDelete]);

  const order = events ? displayOrder(events) : [];
  const eventOf = (id: number) => events?.events.find((e) => e.id === id);
  const existing = (ids: number[]) => ids.filter((id) => eventOf(id));
  const rowsOf = (ids: number[]) => existing(ids).map((id) => eventOf(id)!);
  const selected = selectedIds.length ? selectedIds : selectedId != null ? [selectedId] : [];
  const inOrder = (ids: number[]) => order.filter((id) => ids.includes(id));
  const moveIds = (id: number) => inOrder(selected.includes(id) ? selected : [id]);

  const click = (e: ReactMouseEvent, id: number) => {
    if (e.ctrlKey || e.metaKey) {
      onSelect(selectedId, inOrder(toggleId(selected, id)));
    } else if (e.shiftKey) {
      onSelect(selectedId, inOrder(rangeIds(order, selectedId ?? id, id)));
    } else {
      onSelect(id, [id]);
      void client.send({ cmd: 'player.cue', player: 'preview', eventId: id });
    }
  };

  const startRename = (g: Group) => {
    setEditing(g.id);
    setDraft(g.name);
  };
  const commitRename = () => {
    if (editing !== null) {
      const name = draft.trim();
      if (name) void client.send({ cmd: 'list.rename', id: editing, name });
    }
    setEditing(null);
  };

  const row = (e: Event) => (
    <button
      key={e.id}
      className={`ev${selectedId === e.id ? ' sel' : ''}${selectedId !== e.id && selectedIds.includes(e.id) ? ' multi' : ''}`}
      aria-label={`${e.id} · ${formatClock(e.inUs)} · ${formatDuration(e.outUs - e.inUs)}`}
      title="Click to load this replay in Preview. Right-click for more."
      draggable
      onClick={(ev) => click(ev, e.id)}
      onDragStart={(ev) => {
        setDragId(e.id);
        ev.dataTransfer.setData('text/plain', String(e.id));
      }}
      onDragOver={(ev) => ev.preventDefault()}
      onDrop={(ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        if (dragId !== null) void client.send({ cmd: 'event.move', ids: moveIds(dragId), listId: groupOf(events!, e.id), beforeId: e.id });
        setDragId(null);
      }}
      onContextMenu={(m) =>
        menu(m, [
          { label: 'Load in Preview', onSelect: () => void client.send({ cmd: 'player.cue', player: 'preview', eventId: e.id }) },
          {
            label: 'Play on air now',
            hint: keyLabel('take', hotkeys),
            onSelect: async () => {
              await client.send({ cmd: 'player.cue', player: 'preview', eventId: e.id });
              await client.send({ cmd: 'take' });
            },
          },
          { label: 'Remove camera switches', onSelect: () => void client.send({ cmd: 'event.clearCuts', id: e.id }) },
          ...(onExport ? [{ label: 'Export…', onSelect: () => onExport({ id: e.id, name: e.name || 'Replay' }) }] : []),
          'separator',
          { label: `Delete replay ${e.id}`, danger: true, onSelect: () => void client.send({ cmd: 'event.delete', id: e.id }) },
        ])
      }
    >
      <span>{`${e.id} · ${formatClock(e.inUs)} · ${formatDuration(e.outUs - e.inUs)}`}</span>
      {selectedId === e.id && <em>in Preview</em>}
    </button>
  );

  const header = (g: { id: string; name: string; order: number[] }) => {
    const target = events?.currentList === g.id;
    const isRoot = g.id === '';
    if (confirmDelete === g.id) {
      return (
        <div className="grp-h" data-testid={`group-${isRoot ? 'root' : g.id}`} key={`h-${g.id}`}>
          <span className="grp-name">{`Delete ${g.name}?`}</span>
          <button
            className="mini danger"
            onClick={(ev) => {
              ev.stopPropagation();
              void client.send({ cmd: 'list.delete', id: g.id });
              setConfirmDelete(null);
            }}
          >
            Delete
          </button>
          <button
            className="mini"
            onClick={(ev) => {
              ev.stopPropagation();
              setConfirmDelete(null);
            }}
          >
            Keep
          </button>
        </div>
      );
    }
    return (
      <div
        className={`grp-h${target ? ' target' : ''}`}
        data-testid={`group-${isRoot ? 'root' : g.id}`}
        key={`h-${g.id}`}
        onClick={() => void client.send({ cmd: 'list.select', id: target ? '' : g.id })}
        onDragOver={(ev) => ev.preventDefault()}
        onDrop={(ev) => {
          ev.preventDefault();
          if (dragId !== null) void client.send({ cmd: 'event.move', ids: moveIds(dragId), listId: g.id });
          setDragId(null);
        }}
        onContextMenu={(m) =>
          isRoot
            ? undefined
            : menu(m, [
                { label: 'Rename group', onSelect: () => startRename(g) },
                { label: 'Delete group', danger: true, onSelect: () => setConfirmDelete(g.id) },
              ])
        }
      >
        {isRoot ? (
          <span className="chev" />
        ) : (
          <button
            className="chev"
            aria-label={collapsed.has(g.id) ? `Expand ${g.name}` : `Collapse ${g.name}`}
            onClick={(ev) => {
              ev.stopPropagation();
              setCollapsed((old) => {
                const next = new Set(old);
                if (next.has(g.id)) next.delete(g.id);
                else next.add(g.id);
                return next;
              });
            }}
          >
            {collapsed.has(g.id) ? '▸' : '▾'}
          </button>
        )}
        {editing === g.id ? (
          <input
            className="grp-edit mono"
            autoFocus
            value={draft}
            onClick={(ev) => ev.stopPropagation()}
            onChange={(ev) => setDraft(ev.target.value)}
            onKeyDown={(ev) => {
              if (ev.key === 'Enter') commitRename();
              else if (ev.key === 'Escape') setEditing(null);
              else return;
              ev.stopPropagation();
            }}
            onBlur={() => setEditing(null)}
          />
        ) : (
          <span
            className="grp-name"
            data-testid={`group-name-${isRoot ? 'root' : g.id}`}
            onDoubleClick={(ev) => {
              ev.stopPropagation();
              if (!isRoot) startRename(g);
            }}
          >
            {g.name}
          </span>
        )}
        <span className="grp-count">{g.order.length}</span>
        {target && (
          <span className="aim" title="New replays go into this group">
            ◎
          </span>
        )}
        <button
          className="mini play"
          title={`Play ${g.name} top to bottom`}
          onClick={(ev) => {
            ev.stopPropagation();
            void client.send({ cmd: 'highlight.play', listId: g.id });
          }}
        >
          Play ▸
        </button>
        {onExport && (
          <button
            className="mini play"
            aria-label={`Export ${isRoot ? 'Replays' : g.name}`}
            title={`Export ${isRoot ? 'Replays' : g.name} as an MP4`}
            onClick={(ev) => {
              ev.stopPropagation();
              onExport({ listId: g.id, name: isRoot ? 'Replays' : g.name });
            }}
          >
            Export
          </button>
        )}
      </div>
    );
  };

  const groups = events?.lists ?? [];
  const root = events?.rootOrder ?? [];
  const anyEvents = (events?.events.length ?? 0) > 0;
  const primaryGroup = selectedId != null && events ? groupOf(events, selectedId) : (events?.currentList ?? '');
  const groupOrder = primaryGroup ? (events?.lists.find((l) => l.id === primaryGroup)?.order ?? []) : root;
  const playingSelection = selected.length >= 2;
  const groupName = events?.lists.find((l) => l.id === primaryGroup)?.name;
  const label = playingSelection ? `Play selection (${selected.length})` : primaryGroup ? `Play group (${groupName})` : 'Play all';
  const canPlay = playingSelection ? inOrder(selected).length > 0 : existing(groupOrder).length > 0;

  return (
    <aside className="events" aria-label="Event list">
      <div className="ev-head">
        <h5>Replays</h5>
        <button className="mini" title="Make a group for the next game" onClick={() => void client.send({ cmd: 'list.new' })}>
          + Group
        </button>
      </div>
      <div className="ev-list">
        {groups.length === 0 ? (
          root.map((id) => eventOf(id)).filter((e): e is Event => !!e).map(row)
        ) : (
          <>
            {header({ id: '', name: 'No group', order: root })}
            {rowsOf(root).map(row)}
            {groups.map((g) => (
              <Fragment key={g.id}>
                {header(g)}
                {!collapsed.has(g.id) && rowsOf(g.order).map(row)}
              </Fragment>
            ))}
          </>
        )}
        {!anyEvents && (
          <p className="note">
            No replays yet. Press <kbd>{keyLabel('mark.lastN', hotkeys)}</kbd> right after a good play to save the last seconds as a replay.
          </p>
        )}
      </div>
      <div className="ev-actions">
        <button
          className="btn go"
          disabled={!canPlay}
          title={playingSelection ? 'Plays the selected replays one after another, in list order.' : 'Plays every replay in this group one after another, like an end-of-game highlight reel.'}
          onClick={() =>
            playingSelection
              ? void client.send({ cmd: 'highlight.play', ids: inOrder(selected) })
              : void client.send({ cmd: 'highlight.play', listId: primaryGroup })
          }
        >
          {label}
        </button>
        {playingSelection && onExport && (
          <button className="btn" onClick={() => onExport({ ids: inOrder(selected), name: 'Selection' })}>
            Export selection
          </button>
        )}
      </div>
    </aside>
  );
}