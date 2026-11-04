#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/rate.h"

namespace br {

enum class StoredCodec : uint8_t { Vmx, H264, Hevc };

struct SegmentHeader {
  uint32_t version = 2;
  StoredCodec codec = StoredCodec::Vmx;
  std::string inputId;
  uint32_t width = 0, height = 0;
  bc::Rate rate;
  uint16_t audioChannels = 2;
  uint32_t sampleRate = 48000;  // audio: float32 interleaved
};

// One fixed-size index record: 32 bytes packed.
struct FrameRecord {
  int64_t tsUs = 0;
  int64_t offset = 0;  // start of the frame's 24-byte essence header
  uint32_t videoLen = 0, previewLen = 0, audioLen = 0;
  uint32_t flags = 0;  // bit 0 = keyframe
  static constexpr uint32_t kKeyframe = 1;
};
static_assert(sizeof(FrameRecord) == 32);

class SegmentWriter {
 public:
  SegmentWriter(std::filesystem::path base, SegmentHeader h);  // creates base.vmxs + base.vmxi
  ~SegmentWriter();
  SegmentWriter(const SegmentWriter&) = delete;
  SegmentWriter& operator=(const SegmentWriter&) = delete;
  void append(int64_t tsUs, std::span<const uint8_t> video, uint32_t previewLen,
              std::span<const float> audio, bool keyframe = true);
  void close();
  size_t frames() const;
  int64_t bytes() const;
  const std::filesystem::path& base() const { return base_; }

 private:
  void flushIndex();

  std::filesystem::path base_;
  SegmentHeader header_;
  void* essence_ = nullptr;  // HANDLE
  void* index_ = nullptr;    // HANDLE
  int64_t essencePos_ = 0;
  int64_t indexHeaderSize_ = 0;
  int64_t lastFlushTs_ = INT64_MIN;
  size_t frames_ = 0;
  std::vector<FrameRecord> pending_;
};

class SegmentReader {
 public:
  static std::optional<SegmentReader> open(const std::filesystem::path& base);
  void refresh();  // picks up newly flushed index records
  const SegmentHeader& header() const { return header_; }
  std::span<const FrameRecord> records() const { return records_; }
  std::optional<size_t> indexAtOrBefore(int64_t tsUs) const;
  std::vector<uint8_t> readVideo(size_t i, bool previewOnly) const;
  std::vector<float> readAudio(size_t i) const;
  const std::filesystem::path& base() const { return base_; }

 private:
  std::filesystem::path base_;
  SegmentHeader header_;
  int64_t recordsOffset_ = 0;
  std::vector<FrameRecord> records_;
};

// Drops index records that point past the end of the essence file.
void recoverSegment(const std::filesystem::path& base);

}  // namespace br