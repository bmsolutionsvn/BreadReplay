#include "events/edit_history.h"

namespace br {

void EditHistory::record(const EventStore& before) {
  if (limit_ == 0) return;
  if (undo_.size() == limit_) undo_.erase(undo_.begin());
  undo_.push_back(before);
  redo_.clear();
}

bool EditHistory::undo(EventStore& store) {
  if (undo_.empty()) return false;
  redo_.push_back(store);
  store.restoreFrom(undo_.back());
  undo_.pop_back();
  return true;
}

bool EditHistory::redo(EventStore& store) {
  if (redo_.empty()) return false;
  undo_.push_back(store);
  store.restoreFrom(redo_.back());
  redo_.pop_back();
  return true;
}

void EditHistory::clear() {
  undo_.clear();
  redo_.clear();
}

}  // namespace br