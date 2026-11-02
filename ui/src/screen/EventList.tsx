import type { Client } from '../api/client';
import type { Event, EventStore } from '../api/types';
import { useContextMenu } from '../ContextMenu';
import { formatClock, formatDuration } from '../format';
import { keyLabel } from '../hotkeys';

type Props = {
  client: Pick<Client, 'send'>;
  events: EventStore | null;
  selectedId: number | null;
  onSelect: (id: number) => void;
  hotkeys?: Record<string, string>;
};

export function EventList({ client, events, selectedId, onSelect, hotkeys = {} }: Props) {
  const menu = useContextMenu();
  const list = events?.lists.find((l) => l.id === events.currentList);
  const order = list?.order?.length ? list.order : (events?.events ?? []).filter((e) => e.listId === events?.currentList).map((e) => e.id);
  const rows = order.map((id) => events?.events.find((e) => e.id === id)).filter((e): e is Event => !!e);

  const cue = (e: Event) => {
    onSelect(e.id);
    return client.send({ cmd: 'player.cue', player: 'preview', eventId: e.id });
  };

  return (
    <aside className="events" aria-label="Event list">
      <h5>{`Events · ${list?.name ?? 'No list'}`}</h5>
      <div className="ev-list">
        {rows.map((e) => {
          const label = `${e.id} · ${formatClock(e.inUs)} · ${formatDuration(e.outUs - e.inUs)}`;
          return (
            <button
              key={e.id}
              className={`ev ${selectedId === e.id ? 'sel' : ''}`}
              aria-label={label}
              onClick={() => void cue(e)}
              onContextMenu={(m) =>
                menu(m, [
                  { label: 'Cue in Preview', onSelect: () => void cue(e) },
                  {
                    label: 'Cue and take to air',
                    hint: keyLabel('take', hotkeys),
                    onSelect: async () => {
                      await cue(e);
                      await client.send({ cmd: 'take' });
                    },
                  },
                  { label: 'Clear angle cuts', onSelect: () => void client.send({ cmd: 'event.clearCuts', id: e.id }) },
                  'separator',
                  { label: `Delete event ${e.id}`, danger: true, onSelect: () => void client.send({ cmd: 'event.delete', id: e.id }) },
                ])
              }
            >
              <span>{label}</span>
              {selectedId === e.id && <em>cued</em>}
            </button>
          );
        })}
        {rows.length === 0 && (
          <p className="note">
            No events yet. Press <kbd>{keyLabel('mark.lastN', hotkeys)}</kbd> after a play to save the last seconds.
          </p>
        )}
      </div>
      <button
        className="btn go"
        disabled={!list || rows.length === 0}
        title={rows.length === 0 ? 'Mark some events first' : undefined}
        onClick={() => list && client.send({ cmd: 'highlight.play', listId: list.id })}
      >
        Highlight ({list?.name ?? '—'})
      </button>
    </aside>
  );
}
