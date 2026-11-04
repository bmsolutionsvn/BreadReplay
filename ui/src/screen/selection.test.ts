import type { EventStore } from '../api/types';
import { displayOrder, rangeIds } from './selection';

const store = (o: Partial<EventStore>): EventStore => ({
  version: 1,
  nextId: 1,
  nextListId: 1,
  currentList: '',
  preRollUs: 0,
  postRollUs: 0,
  defaultSpeed: 0.5,
  rootOrder: [],
  lists: [],
  events: [],
  ...o,
});

test('displayOrder lists the root, then each group', () => {
  expect(displayOrder(store({ rootOrder: [1], lists: [{ id: 'g1', name: 'Game 1', order: [3, 2] }] }))).toEqual([1, 3, 2]);
});

test('rangeIds selects inclusively in display order either way', () => {
  expect(rangeIds([1, 3, 2, 5], 2, 3)).toEqual([3, 2]);
});