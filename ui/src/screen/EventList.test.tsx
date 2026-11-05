import { fireEvent, render, screen } from '@testing-library/react';
import { useState } from 'react';

import type { EventStore } from '../api/types';
import { ContextMenuHost } from '../ContextMenu';
import { EventList } from './EventList';

const local = (h: number, m: number, s: number) => new Date(1970, 0, 1, h, m, s).getTime() * 1000;

const ev = (id: number, listId = '') => ({
  id,
  listId,
  name: '',
  tags: [],
  angleNotes: {},
  inUs: local(1, 5, 10 + id),
  outUs: local(1, 5, 22 + id),
  angles: [],
  speeds: [],
  zooms: [],
});

const store = (o: Partial<EventStore> = {}): EventStore => ({
  version: 1,
  nextId: 10,
  nextListId: 3,
  currentList: '',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  rootOrder: [],
  lists: [],
  events: [],
  ...o,
});

const client = () => ({ send: vi.fn().mockResolvedValue({ ok: true }) });

function Harness({ s, client: c, onSelect }: { s: EventStore; client: ReturnType<typeof client>; onSelect?: (p: number | null, ids: number[]) => void }) {
  const [selected, setSelected] = useState<number | null>(s.events[0]?.id ?? null);
  const [ids, setIds] = useState<number[]>(s.events.length ? [s.events[0].id] : []);
  return (
    <EventList
      client={c}
      events={s}
      selectedId={selected}
      selectedIds={ids}
      onSelect={(p, next) => {
        setSelected(p);
        setIds(next);
        onSelect?.(p, next);
      }}
    />
  );
}

test('no group shows one plain list', () => {
  const c = client();
  const s = store({ rootOrder: [1, 3], events: [ev(1), ev(3)] });
  const { container } = render(<EventList client={c} events={s} selectedId={null} selectedIds={[]} onSelect={() => {}} />);
  expect(screen.getByText('Replays')).toBeInTheDocument();
  expect(screen.queryByText('No group')).toBeNull();
  expect([...container.querySelectorAll('.ev')].map((el) => el.textContent)).toEqual([
    '1 · 01:05:11 · 0:12',
    '3 · 01:05:13 · 0:12',
  ]);
});

test('clicking a group targets it; clicking the target again targets the root', () => {
  const c = client();
  const s = store({ lists: [{ id: 'g1', name: 'Game 1', order: [2] }], events: [ev(2, 'g1')] });
  const { rerender } = render(<EventList client={c} events={s} selectedId={null} selectedIds={[]} onSelect={() => {}} />);
  fireEvent.click(screen.getByTestId('group-g1'));
  expect(c.send).toHaveBeenCalledWith({ cmd: 'list.select', id: 'g1' });
  expect(screen.getByTestId('group-g1')).not.toHaveTextContent('◎');
  rerender(<EventList client={c} events={{ ...s, currentList: 'g1' }} selectedId={null} selectedIds={[]} onSelect={() => {}} />);
  expect(screen.getByTestId('group-g1')).toHaveTextContent('◎');
  fireEvent.click(screen.getByTestId('group-g1'));
  expect(c.send).toHaveBeenLastCalledWith({ cmd: 'list.select', id: '' });
});

test('+ Group creates a group', () => {
  const c = client();
  render(<EventList client={c} events={store()} selectedId={null} selectedIds={[]} onSelect={() => {}} />);
  fireEvent.click(screen.getByRole('button', { name: '+ Group' }));
  expect(c.send).toHaveBeenCalledWith({ cmd: 'list.new' });
});

test('double-click renames inline; Escape cancels', () => {
  const c = client();
  const s = store({ lists: [{ id: 'g1', name: 'Game 1', order: [] }] });
  render(<EventList client={c} events={s} selectedId={null} selectedIds={[]} onSelect={() => {}} />);
  fireEvent.doubleClick(screen.getByTestId('group-name-g1'));
  const input = screen.getByRole('textbox');
  fireEvent.change(input, { target: { value: 'Final' } });
  fireEvent.keyDown(input, { key: 'Enter' });
  expect(c.send).toHaveBeenCalledWith({ cmd: 'list.rename', id: 'g1', name: 'Final' });

  c.send.mockClear();
  fireEvent.doubleClick(screen.getByTestId('group-name-g1'));
  const second = screen.getByRole('textbox');
  fireEvent.change(second, { target: { value: 'Nope' } });
  fireEvent.keyDown(second, { key: 'Escape' });
  expect(c.send).not.toHaveBeenCalledWith(expect.objectContaining({ cmd: 'list.rename' }));
});

test('Delete group asks inline, then deletes', () => {
  const c = client();
  const s = store({ lists: [{ id: 'g1', name: 'Game 1', order: [] }] });
  render(
    <ContextMenuHost>
      <EventList client={c} events={s} selectedId={null} selectedIds={[]} onSelect={() => {}} />
    </ContextMenuHost>,
  );
  fireEvent.contextMenu(screen.getByTestId('group-g1'), { clientX: 10, clientY: 10 });
  fireEvent.click(screen.getByRole('menuitem', { name: 'Delete group' }));
  expect(c.send).not.toHaveBeenCalledWith(expect.objectContaining({ cmd: 'list.delete' }));
  expect(screen.getByTestId('group-g1')).toHaveTextContent('Delete Game 1?');
  fireEvent.click(screen.getByRole('button', { name: 'Delete' }));
  expect(c.send).toHaveBeenCalledWith({ cmd: 'list.delete', id: 'g1' });
});

test('Ctrl+click and Shift+click build a selection; Play selection plays it in display order', () => {
  const c = client();
  const onSelect = vi.fn();
  const s = store({
    rootOrder: [1],
    lists: [{ id: 'g1', name: 'Game 1', order: [2, 3] }],
    events: [ev(1), ev(2, 'g1'), ev(3, 'g1')],
  });
  render(<Harness s={s} client={c} onSelect={onSelect} />);
  fireEvent.click(screen.getByRole('button', { name: /^3 · / }), { ctrlKey: true });
  expect(onSelect).toHaveBeenCalledWith(1, [1, 3]);
  expect(c.send).not.toHaveBeenCalledWith(expect.objectContaining({ cmd: 'player.cue' }));
  fireEvent.click(screen.getByRole('button', { name: /^2 · / }), { shiftKey: true });
  expect(onSelect).toHaveBeenLastCalledWith(1, [1, 2]);
  expect(screen.getByRole('button', { name: 'Play selection (2)' })).toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Play selection (2)' }));
  expect(c.send).toHaveBeenCalledWith({ cmd: 'highlight.play', ids: [1, 2] });
});

test("dropping a selected row on another group's row moves the selection", () => {
  const c = client();
  const s = store({
    rootOrder: [1],
    lists: [{ id: 'g1', name: 'Game 1', order: [2, 3] }],
    events: [ev(1), ev(2, 'g1'), ev(3, 'g1')],
  });
  render(<Harness s={s} client={c} />);
  fireEvent.click(screen.getByRole('button', { name: /^3 · / }), { ctrlKey: true });
  const dataTransfer = { setData: vi.fn(), getData: vi.fn(), effectAllowed: '', dropEffect: '' } as unknown as DataTransfer;
  fireEvent.dragStart(screen.getByRole('button', { name: /^1 · / }), { dataTransfer });
  fireEvent.drop(screen.getByRole('button', { name: /^2 · / }), { dataTransfer });
  expect(c.send).toHaveBeenCalledWith({ cmd: 'event.move', ids: [1, 3], listId: 'g1', beforeId: 2 });
});

test("a group's Export opens the panel for that group", () => {
  const c = client();
  const onExport = vi.fn();
  const s = store({ lists: [{ id: 'g1', name: 'Game 1', order: [2] }], events: [ev(2, 'g1')] });
  render(<EventList client={c} events={s} selectedId={null} selectedIds={[]} onSelect={() => {}} onExport={onExport} />);
  fireEvent.click(screen.getByRole('button', { name: 'Export Game 1' }));
  expect(onExport).toHaveBeenCalledWith({ listId: 'g1', name: 'Game 1' });
  expect(c.send).not.toHaveBeenCalledWith(expect.objectContaining({ cmd: 'list.select' }));
});