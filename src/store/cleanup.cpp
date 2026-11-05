#include "store/cleanup.h"

#include <algorithm>
#include <stdexcept>

namespace br {
namespace {
bool inside(std::span<const ProtectedRange> kept, int64_t tsUs) {
  for (const auto& r : kept) {
    if (tsUs >= r.inUs && tsUs <= r.outUs) return true;
  }
  return false;
}

bool isKeyframe(const SegmentHeader& h, const FrameRecord& rec) {
  return h.version < 2 || (rec.flags & FrameRecord::kKeyframe) != 0;
}

std::vector<ProtectedRange> merge(std::vector<ProtectedRange> ranges) {
  std::sort(ranges.begin(), ranges.end(),
            [](const ProtectedRange& a, const ProtectedRange& b) { return a.inUs < b.inUs; });
  std::vector<ProtectedRange> out;
  for (const auto& r : ranges) {
    if (!out.empty() && r.inUs <= out.back().outUs) {
      out.back().outUs = std::max(out.back().outUs, r.outUs);
    } else {
      out.push_back(r);
    }
  }
  return out;
}
}  // namespace

std::vector<ProtectedRange> keptRanges(const std::vector<Event>& events, int64_t marginUs) {
  std::vector<ProtectedRange> ranges;
  ranges.reserve(events.size());
  for (const auto& e : events) {
    ranges.push_back({std::min(e.inUs, e.outUs) - marginUs, std::max(e.inUs, e.outUs) + marginUs});
  }
  return merge(std::move(ranges));
}

std::vector<SegmentPlan> planCleanup(const std::map<std::string, const InputStore*>& stores,
                                     std::span<const ProtectedRange> kept, int64_t protectFromUs) {
  std::vector<SegmentPlan> plans;
  for (const auto& [inputId, store] : stores) {
    if (!store) continue;
    const auto segments = store->segments();
    for (size_t si = 0; si < segments.size(); ++si) {
      if (store->isWriting(si)) continue;
      auto reader = SegmentReader::open(segments[si].base);
      if (!reader) continue;
      reader->refresh();
      const auto& recs = reader->records();
      if (recs.empty()) continue;

      bool recent = false, anyInside = false, allInside = true;
      for (const auto& rec : recs) {
        if (rec.tsUs >= protectFromUs) recent = true;
        const bool in = inside(kept, rec.tsUs);
        anyInside = anyInside || in;
        allInside = allInside && in;
      }
      if (recent) continue;

      SegmentPlan plan;
      plan.inputId = inputId;
      plan.base = segments[si].base;
      if (!anyInside) {
        plan.action = CleanupAction::Delete;
        plan.bytesFreed = segments[si].bytes;
      } else if (allInside) {
        continue;
      } else {
        plan.action = CleanupAction::Rewrite;
        std::vector<ProtectedRange> clipped;
        for (const auto& r : kept) {
          int64_t start = std::max(r.inUs, recs.front().tsUs);
          const int64_t end = std::min(r.outUs, recs.back().tsUs);
          if (start > end) continue;
          // A compressed range begins on the keyframe at or before it, so the decoder can start.
          int64_t keyStart = start;
          for (const auto& rec : recs) {
            if (rec.tsUs > start) break;
            if (isKeyframe(reader->header(), rec)) keyStart = rec.tsUs;
          }
          start = keyStart;
          clipped.push_back({start, end});
        }
        plan.keep = merge(std::move(clipped));
        for (const auto& rec : recs) {
          bool keptFrame = false;
          for (const auto& k : plan.keep) {
            if (rec.tsUs >= k.inUs && rec.tsUs <= k.outUs) {
              keptFrame = true;
              break;
            }
          }
          if (!keptFrame) plan.bytesFreed += rec.videoLen + rec.audioLen + 24 + 40;
        }
      }
      plans.push_back(std::move(plan));
    }
  }
  return plans;
}

int64_t totalFreed(const std::vector<SegmentPlan>& plans) {
  int64_t sum = 0;
  for (const auto& p : plans) sum += p.bytesFreed;
  return sum;
}

std::filesystem::path rewriteSegment(const std::filesystem::path& base,
                                     std::span<const ProtectedRange> keep) {
  auto reader = SegmentReader::open(base);
  if (!reader) throw std::runtime_error("cleanup: cannot open " + base.string());
  reader->refresh();
  const auto& recs = reader->records();
  const auto newBase = std::filesystem::path(base.string() + ".cleanup");
  auto removeNew = [&] {
    std::error_code ec;
    std::filesystem::remove(newBase.string() + ".vmxs", ec);
    std::filesystem::remove(newBase.string() + ".vmxi", ec);
  };
  try {
    SegmentWriter writer(newBase, reader->header());
    for (size_t i = 0; i < recs.size(); ++i) {
      if (!inside(keep, recs[i].tsUs)) continue;
      const auto video = reader->readVideo(i, false);
      const auto audio = reader->readAudio(i);
      writer.append(recs[i].tsUs, video, recs[i].previewLen, audio,
                    isKeyframe(reader->header(), recs[i]), recs[i].srcPtsUs);
    }
    writer.close();
  } catch (...) {
    removeNew();
    throw;
  }
  return newBase;
}

}  // namespace br