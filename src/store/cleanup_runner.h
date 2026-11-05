#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "store/cleanup.h"
#include "store/input_store.h"

namespace br {

struct CleanupStatus {
  std::string state = "idle";  // idle, running, done, failed
  double progress = 0;
  int64_t bytesFreed = 0;
  std::string error;
};

// Plans once, then deletes or rewrites one segment at a time on a below-normal thread.
class CleanupRunner {
 public:
  using KeptFn = std::function<std::vector<ProtectedRange>()>;
  CleanupRunner(std::map<std::string, InputStore*> stores, KeptFn kept,
                std::function<int64_t()> newestTs, int64_t protectUs = kCleanupProtectUs);
  ~CleanupRunner();
  CleanupRunner(const CleanupRunner&) = delete;
  CleanupRunner& operator=(const CleanupRunner&) = delete;

  bool start();  // false while a run is already going
  CleanupStatus status() const;
  size_t storeCount() const;  // for tests: how many stores this runner holds
  void stop();

 private:
  void run();

  std::map<std::string, InputStore*> stores_;
  KeptFn kept_;
  std::function<int64_t()> newestTs_;
  int64_t protectUs_;
  mutable std::mutex m_;
  CleanupStatus status_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
};

}  // namespace br