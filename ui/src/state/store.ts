import { useSyncExternalStore } from 'react';

import type { EventStore, Status } from '../api/types';

// The engine pushes status at 10 Hz; both hooks read the latest pushed value, so an alert
// (part of the status) stays on screen until a status without it arrives.
let status: Status | null = null;
let events: EventStore | null = null;
const listeners = new Set<() => void>();

function emit() {
  for (const fn of listeners) fn();
}

function subscribe(fn: () => void) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

export function setStatus(s: Status) {
  status = s;
  emit();
}

export function setEvents(e: EventStore) {
  events = e;
  emit();
}

export function resetStore() {
  status = null;
  events = null;
  emit();
}

export function useStatus(): Status | null {
  return useSyncExternalStore(subscribe, () => status);
}

export function useEvents(): EventStore | null {
  return useSyncExternalStore(subscribe, () => events);
}