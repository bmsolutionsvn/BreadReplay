#pragma once
#include <cstddef>
#include <vector>

#include "events/event_store.h"

namespace br {

// Bounded, memory-only stack of event-store snapshots. One snapshot per edit.
class EditHistory {
 public:
  explicit EditHistory(size_t limit = 200) : limit_(limit) {}
  void record(const EventStore& before);
  bool undo(EventStore& store);
  bool redo(EventStore& store);
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  void clear();

 private:
  const size_t limit_;
  std::vector<EventStore> undo_;
  std::vector<EventStore> redo_;
};

}  // namespace br