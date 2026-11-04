import type { EventStore } from '../api/types';

export function displayOrder(store: EventStore): number[] {
  return [...store.rootOrder, ...store.lists.flatMap((l) => l.order)];
}

export function groupOf(store: EventStore, id: number): string {
  return store.events.find((e) => e.id === id)?.listId ?? '';
}

export function toggleId(ids: number[], id: number): number[] {
  return ids.includes(id) ? ids.filter((x) => x !== id) : [...ids, id];
}

export function rangeIds(order: number[], anchor: number, id: number): number[] {
  const a = order.indexOf(anchor);
  const b = order.indexOf(id);
  if (a < 0 || b < 0) return [id];
  return order.slice(Math.min(a, b), Math.max(a, b) + 1);
}