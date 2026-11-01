import type { Client } from '../api/client';
import type { Event, EventStore } from '../api/types';
import { formatClock, formatDuration } from '../format';

type Props = {
  client: Pick<Client, 'send'>;
  events: EventStore | null;
  selectedId: number | null;
  onSelect: (id: number) => void;
};

export function EventList({ client, events, selectedId, onSelect }: Props) {
  const list = events?.lists.find((l) => l.id === events.currentList);
  const order = list?.order?.length ? list.order : (events?.events ?? []).filter((e) => e.listId === events?.currentList).map((e) => e.id);
  const rows = order.map((id) => events?.events.find((e) => e.id === id)).filter((e): e is Event => !!e);
  return (
    <aside className="events" aria-label="Event list">
      <h5>{`Events · ${list?.name ?? 'No list'}`}</h5>
      <div className="ev-list">
        {rows.map((e) => {
          const label = `${e.id} · ${formatClock(e.inUs)} · ${formatDuration(e.outUs - e.inUs)}`;
          return (
            <button key={e.id} className={`ev ${selectedId === e.id ? 'sel' : ''}`} aria-label={label} onClick={() => {
                onSelect(e.id);
                client.send({ cmd: 'player.cue', player: 'preview', eventId: e.id });
              }}>
              <span>{label}</span>
              {selectedId === e.id && <em>cued</em>}
            </button>
          );
        })}
        {rows.length === 0 && <p className="note">No events in this list yet. Space makes one.</p>}
      </div>
      <button className="btn go" disabled={!list} onClick={() => list && client.send({ cmd: 'highlight.play', listId: list.id })}>
        Highlight ({list?.name ?? '—'})
      </button>
    </aside>
  );
}