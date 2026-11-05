#include <doctest/doctest.h>

#include "events/edit_history.h"
#include "events/event_store.h"

namespace {
constexpr int64_t kS = 1'000'000;
}

TEST_CASE("History: undo and redo restore the store") {
  br::EventStore s;
  br::EditHistory h;
  h.record(s);
  s.lastN(100 * kS, 1 * kS, "cam1");
  const auto v = s.version();
  CHECK(h.undo(s));
  CHECK(s.events().empty());
  CHECK(s.version() > v);
  CHECK(h.redo(s));
  CHECK(s.events().size() == 1);
  CHECK_FALSE(h.redo(s));
}

TEST_CASE("History: a mark after an undo gets a new id") {
  br::EventStore s;
  br::EditHistory h;
  h.record(s);
  const int first = s.lastN(100 * kS, 1 * kS, "cam1").id;
  CHECK(h.undo(s));
  const int again = s.lastN(200 * kS, 1 * kS, "cam1").id;
  CHECK(first == 1);
  CHECK(again == 2);
  CHECK(s.find(first) == nullptr);
}

TEST_CASE("History: keeps at most 200 steps and a new edit clears redo") {
  br::EventStore s;
  br::EditHistory h;
  for (int i = 0; i < 205; ++i) { h.record(s); s.lastN((100 + i) * kS, 1 * kS, "c"); }
  int n = 0;
  while (h.undo(s)) ++n;
  CHECK(n == 200);
  CHECK(s.events().size() == 5);
  h.redo(s);
  h.record(s);
  CHECK_FALSE(h.canRedo());
}