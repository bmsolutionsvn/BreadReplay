#pragma once
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "events/event_store.h"
#include "events/show_file.h"
#include "export/export_rules.h"

namespace br {

class InputStore;

struct ExportJob {
  std::string name;
  std::vector<Event> events;  // copies in play order, taken when the job is added
  ExportCodec codec = ExportCodec::H264;
  int64_t bitRate = 0;
  std::filesystem::path path;
};

struct ExportState {
  int id = 0;
  std::string name, state, error, path;
  double progress = 0;
};

// One job at a time on a below-normal thread, each with its own device and decode lane.
class ExportQueue {
 public:
  ExportQueue(std::map<std::string, const InputStore*> stores, ShowSettings settings);
  ~ExportQueue();
  ExportQueue(const ExportQueue&) = delete;
  ExportQueue& operator=(const ExportQueue&) = delete;

  int add(ExportJob job);
  void cancel(int id);
  std::vector<ExportState> states() const;  // queued/running plus the last 5 finished
  void stop();

 private:
  struct Entry {
    ExportJob job;
    ExportState state;
  };

  void run();
  void runJob(const Entry& entry);
  int64_t estimateTicks(EventStore& store, const std::vector<Event>& events) const;
  void setProgress(int id, double progress);
  void finishEntry(int id, const std::string& state, const std::string& error, double progress);
  bool isFinal(const std::string& state) const;

  std::map<std::string, const InputStore*> stores_;
  ShowSettings settings_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::vector<Entry> entries_;
  std::set<std::filesystem::path> reserved_;  // paths handed out but not yet on disk
  int nextId_ = 1;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<int> cancelId_{0};
};

}  // namespace br