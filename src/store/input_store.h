#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <vector>

#include "store/segment.h"

namespace br {

struct FrameRef {
  size_t segment = 0;
  size_t index = 0;
  int64_t tsUs = 0;
};

struct SegmentInfo {
  std::filesystem::path base;
  int64_t firstTs = 0, lastTs = 0;
  int64_t bytes = 0;
};

class InputStore {
 public:
  InputStore(std::filesystem::path dir, SegmentHeader proto);  // loads + recovers existing segments
  void append(int64_t tsUs, std::span<const uint8_t> video, uint32_t previewLen,
              std::span<const float> audio);
  std::optional<FrameRef> find(int64_t tsUs) const;  // at or before, else nullopt
  bool isGapAt(int64_t tsUs) const;                  // no frame within 2 periods before tsUs
  std::vector<uint8_t> readVideo(const FrameRef&, bool previewOnly) const;
  // The size the frame was recorded at (its segment's header); {0, 0} for a stale ref.
  std::pair<uint32_t, uint32_t> frameSize(const FrameRef&) const;
  std::vector<float> readAudio(const FrameRef&) const;
  std::vector<SegmentInfo> segments() const;
  bool removeSegment(const std::filesystem::path& base);  // refuses the open (writing) segment
  // A recorder's frame size changed: the open segment is closed, the next append starts a new
  // one with this header.
  void changeVideoFormat(uint32_t w, uint32_t h, bc::Rate rate);
  std::vector<std::pair<int64_t, int64_t>> coverage() const;  // ranges split at gaps > 2 periods
  int64_t newestTs() const;
  int64_t bytesOnDisk() const;
  size_t frameCount() const;
  const SegmentHeader& header() const { return proto_; }
  const std::filesystem::path& dir() const { return dir_; }

 private:
  struct Seg {
    std::filesystem::path base;
    std::unique_ptr<SegmentWriter> writer;  // set only for the segment being written
    std::unique_ptr<SegmentReader> reader;
    std::vector<int64_t> times;
    int64_t bytes = 0;
  };

  mutable std::shared_mutex m_;
  std::filesystem::path dir_;
  SegmentHeader proto_;
  std::vector<Seg> segs_;
  int64_t periodUs_ = 0;
  // A gap is more than two show frames and at least 100 ms: periodUs_ is the show's rate, so a
  // 30 fps observer in a 59.94 show would otherwise "drop" every other frame.
  // ponytail: fixed floor; track each input's measured cadence if 10-20 fps sources appear.
  int64_t gapUs() const { return std::max<int64_t>(2 * periodUs_, 100'000); }
  bool forceNewSegment_ = false;
};

}  // namespace br