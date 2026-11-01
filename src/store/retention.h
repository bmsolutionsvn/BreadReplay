#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "store/input_store.h"

namespace br {

struct RetentionLimit {
  std::optional<double> hours;
  std::optional<int64_t> bytes;
};

struct ProtectedRange {
  int64_t inUs = 0, outUs = 0;
};

struct StoredSegment {
  std::string inputId;
  SegmentInfo info;
  bool open = false;
};

// Oldest first; never the open segment, never one overlapping a protected range. Deletes until
// the limit holds and freeBytes >= minFreeBytes.
std::vector<StoredSegment> segmentsToDelete(std::vector<StoredSegment> all, const RetentionLimit&,
                                            int64_t freeBytes, int64_t minFreeBytes,
                                            std::span<const ProtectedRange> keep);

class WriteRateMeter {
 public:
  void add(int64_t bytes, int64_t nowUs);
  double bytesPerSec(int64_t nowUs) const;  // 5 s window

 private:
  struct Sample {
    int64_t tsUs;
    int64_t bytes;
  };
  std::vector<Sample> samples_;
};

std::optional<double> secondsLeft(int64_t freeBytes, double writeBytesPerSec);  // nullopt when rate == 0

struct Preflight {
  bool canRecord = false;
  double minutesFree = 0;
  double writeMBps = 0;
  std::vector<std::string> warnings;
  std::optional<std::string> error;
};

// <=3 s / <=256 MB test write, then deleted.
Preflight preflight(const std::filesystem::path& folder, int inputs, double mbpsPerInput);

}  // namespace br