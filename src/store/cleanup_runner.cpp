#include "store/cleanup_runner.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>

namespace br {
CleanupRunner::CleanupRunner(std::map<std::string, InputStore*> stores, KeptFn kept,
                             std::function<int64_t()> newestTs, int64_t protectUs)
    : stores_(std::move(stores)),
      kept_(std::move(kept)),
      newestTs_(std::move(newestTs)),
      protectUs_(protectUs) {}

CleanupRunner::~CleanupRunner() { stop(); }

bool CleanupRunner::start() {
  {
    std::lock_guard lock(m_);
    if (status_.state == "running") return false;
  }
  if (thread_.joinable()) thread_.join();
  {
    std::lock_guard lock(m_);
    status_ = CleanupStatus{"running", 0, 0, ""};
  }
  stopping_ = false;
  thread_ = std::thread([this] { run(); });
  return true;
}

CleanupStatus CleanupRunner::status() const {
  std::lock_guard lock(m_);
  return status_;
}

void CleanupRunner::stop() {
  stopping_ = true;
  if (thread_.joinable()) thread_.join();
}

void CleanupRunner::run() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  int64_t freed = 0;
  try {
    const int64_t newest = newestTs_ ? newestTs_() : 0;
    const int64_t protectFrom = newest > 0 ? newest - protectUs_ : 0;
    std::map<std::string, const InputStore*> view;
    for (const auto& [id, store] : stores_) view[id] = store;
    const auto plans = planCleanup(view, kept_(), protectFrom);
    size_t done = 0;
    for (const auto& initial : plans) {
      if (stopping_) break;
      const auto storeIt = stores_.find(initial.inputId);
      if (storeIt == stores_.end() || !storeIt->second) {
        ++done;
        continue;
      }
      InputStore& store = *storeIt->second;
      // The replays may have changed: re-plan this one against the kept ranges as they are now.
      const auto freshPlans = planCleanup({{initial.inputId, &store}}, kept_(), protectFrom);
      const auto fresh = std::find_if(freshPlans.begin(), freshPlans.end(),
                                      [&](const SegmentPlan& p) { return p.base == initial.base; });
      if (fresh == freshPlans.end() ||
          !std::filesystem::exists(initial.base.string() + ".vmxs")) {
        ++done;  // retention or the operator got there first
        continue;
      }
      const int64_t before = fresh->bytesFreed;
      if (fresh->action == CleanupAction::Delete) {
        if (store.removeSegment(initial.base)) freed += before;
      } else if (fresh->action == CleanupAction::Rewrite) {
        const auto newBase = rewriteSegment(initial.base, fresh->keep);
        if (store.replaceSegment(initial.base, newBase)) freed += before;
      }
      ++done;
      std::lock_guard lock(m_);
      status_.progress = plans.empty() ? 1.0 : static_cast<double>(done) / static_cast<double>(plans.size());
      status_.bytesFreed = freed;
    }
    std::lock_guard lock(m_);
    status_.state = "done";
    status_.progress = 1.0;
    status_.bytesFreed = freed;
  } catch (const std::exception& e) {
    std::lock_guard lock(m_);
    status_.state = "failed";
    status_.error = e.what();
    status_.bytesFreed = freed;
  }
}

}  // namespace br