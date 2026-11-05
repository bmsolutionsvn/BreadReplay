#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "events/event_store.h"
#include "store/input_store.h"
#include "store/retention.h"

namespace br {

constexpr int64_t kCleanupMarginUs = 3'000'000;
constexpr int64_t kCleanupProtectUs = 300'000'000;

enum class CleanupAction { Keep, Delete, Rewrite };

struct SegmentPlan {
  std::string inputId;
  std::filesystem::path base;
  CleanupAction action = CleanupAction::Keep;
  std::vector<ProtectedRange> keep;  // Rewrite only: clipped to the segment, starts on keyframes
  int64_t bytesFreed = 0;
};

// Per event [min(in,out) - margin, max(in,out) + margin], sorted, overlaps and touching merged.
std::vector<ProtectedRange> keptRanges(const std::vector<Event>& events, int64_t marginUs);

// Closed segments only. A segment with a frame at or after protectFromUs is untouched; one with no
// frame inside kept is deleted; one wholly inside is left alone; anything else is rewritten.
std::vector<SegmentPlan> planCleanup(const std::map<std::string, const InputStore*>& stores,
                                     std::span<const ProtectedRange> kept, int64_t protectFromUs);

// Copies the frames inside keep into <base>.cleanup with the segment's own header. Throws on an I/O
// error after deleting what it wrote; the caller swaps it in with InputStore::replaceSegment.
std::filesystem::path rewriteSegment(const std::filesystem::path& base,
                                     std::span<const ProtectedRange> keep);

int64_t totalFreed(const std::vector<SegmentPlan>& plans);

}  // namespace br