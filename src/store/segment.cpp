#include "store/segment.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace br {
namespace {
constexpr int64_t kFlushIntervalUs = 500'000;
constexpr size_t kEssenceHeaderSize = 24;
constexpr char kMagic[4] = {'B', 'R', 'F', '1'};

HANDLE openWrite(const std::filesystem::path& p, bool append) {
  HANDLE h = CreateFileW(p.wstring().c_str(), GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         append ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  // GENERIC_WRITE already contains FILE_WRITE_DATA, which cancels FILE_APPEND_DATA semantics, so
  // an append handle would write at offset 0; put its pointer at the end explicitly.
  if (append && h != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER end{};
    SetFilePointerEx(h, end, nullptr, FILE_END);
  }
  return h;
}

HANDLE openRead(const std::filesystem::path& p) {
  return CreateFileW(p.wstring().c_str(), GENERIC_READ,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, nullptr);
}

void writeAll(HANDLE h, const void* data, size_t n) {
  const auto* p = static_cast<const uint8_t*>(data);
  while (n > 0) {
    DWORD wrote = 0;
    if (!WriteFile(h, p, static_cast<DWORD>(n), &wrote, nullptr) || wrote == 0) {
      throw std::runtime_error("segment write failed");
    }
    p += wrote;
    n -= wrote;
  }
}

bool readAt(HANDLE h, int64_t pos, void* data, size_t n) {
  LARGE_INTEGER li;
  li.QuadPart = pos;
  if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false;
  auto* p = static_cast<uint8_t*>(data);
  while (n > 0) {
    DWORD got = 0;
    if (!ReadFile(h, p, static_cast<DWORD>(n), &got, nullptr) || got == 0) return false;
    p += got;
    n -= got;
  }
  return true;
}

int64_t sizeOf(HANDLE h) {
  LARGE_INTEGER s;
  if (!GetFileSizeEx(h, &s)) return 0;
  return s.QuadPart;
}

std::filesystem::path withExt(const std::filesystem::path& base, const char* ext) {
  return std::filesystem::path(base.string() + ext);
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
  out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + 4);
}
void put16(std::vector<uint8_t>& out, uint16_t v) {
  out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + 2);
}
void put64(std::vector<uint8_t>& out, int64_t v) {
  out.insert(out.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + 8);
}

std::vector<uint8_t> headerBytes(const SegmentHeader& h) {
  std::vector<uint8_t> out;
  put32(out, h.version);
  put32(out, static_cast<uint32_t>(h.inputId.size()));
  out.insert(out.end(), h.inputId.begin(), h.inputId.end());
  put32(out, h.width);
  put32(out, h.height);
  put32(out, static_cast<uint32_t>(h.rate.num));
  put32(out, static_cast<uint32_t>(h.rate.den));
  put16(out, h.audioChannels);
  put32(out, h.sampleRate);
  return out;
}

bool parseHeader(std::span<const uint8_t> b, SegmentHeader& h, int64_t& headerSize) {
  auto need = [&](size_t pos, size_t n) { return pos + n <= b.size(); };
  if (!need(0, 8)) return false;
  h.version = *reinterpret_cast<const uint32_t*>(&b[0]);
  const uint32_t idLen = *reinterpret_cast<const uint32_t*>(&b[4]);
  size_t pos = 8;
  if (!need(pos, idLen)) return false;
  h.inputId.assign(reinterpret_cast<const char*>(&b[pos]), idLen);
  pos += idLen;
  if (!need(pos, 4 + 4 + 4 + 4 + 2 + 4)) return false;
  h.width = *reinterpret_cast<const uint32_t*>(&b[pos]); pos += 4;
  h.height = *reinterpret_cast<const uint32_t*>(&b[pos]); pos += 4;
  h.rate = bc::Rate{static_cast<int>(*reinterpret_cast<const uint32_t*>(&b[pos])),
                    static_cast<int>(*reinterpret_cast<const uint32_t*>(&b[pos + 4]))};
  pos += 8;
  h.audioChannels = *reinterpret_cast<const uint16_t*>(&b[pos]); pos += 2;
  h.sampleRate = *reinterpret_cast<const uint32_t*>(&b[pos]); pos += 4;
  headerSize = static_cast<int64_t>(pos);
  return true;
}
}  // namespace

SegmentWriter::SegmentWriter(std::filesystem::path base, SegmentHeader h)
    : base_(std::move(base)), header_(std::move(h)) {
  std::filesystem::create_directories(base_.parent_path());
  const auto idx = withExt(base_, ".vmxi");
  const auto ess = withExt(base_, ".vmxs");
  bool append = std::filesystem::exists(idx) && std::filesystem::exists(ess);

  if (append) {
    HANDLE probe = openRead(idx);
    if (probe != INVALID_HANDLE_VALUE) {
      std::vector<uint8_t> buf(256);
      const int64_t n = sizeOf(probe);
      const size_t take = static_cast<size_t>(std::min<int64_t>(n, 256));
      if (take == 0 || !readAt(probe, 0, buf.data(), take) || !parseHeader({buf.data(), take}, header_, indexHeaderSize_)) {
        append = false;
      }
      CloseHandle(probe);
    } else {
      append = false;
    }
  }

  if (append) {
    essence_ = openWrite(ess, true);
    index_ = openWrite(idx, true);
    if (essence_ == INVALID_HANDLE_VALUE || index_ == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("cannot append to segment " + base_.string());
    }
    essencePos_ = sizeOf(static_cast<HANDLE>(essence_));
    const int64_t idxSize = sizeOf(static_cast<HANDLE>(index_));
    frames_ = idxSize > indexHeaderSize_ ? static_cast<size_t>((idxSize - indexHeaderSize_) / 32) : 0;
    LARGE_INTEGER end;
    end.QuadPart = 0;
    SetFilePointerEx(static_cast<HANDLE>(index_), end, nullptr, FILE_END);
  } else {
    essence_ = openWrite(ess, false);
    index_ = openWrite(idx, false);
    if (essence_ == INVALID_HANDLE_VALUE || index_ == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("cannot create segment " + base_.string());
    }
    const auto hb = headerBytes(header_);
    writeAll(static_cast<HANDLE>(index_), hb.data(), hb.size());
    indexHeaderSize_ = static_cast<int64_t>(hb.size());
    frames_ = 0;
  }
  lastFlushTs_ = INT64_MIN;
}

SegmentWriter::~SegmentWriter() { close(); }

void SegmentWriter::append(int64_t tsUs, std::span<const uint8_t> video, uint32_t previewLen,
                           std::span<const float> audio) {
  if (!essence_) return;
  const uint32_t videoLen = static_cast<uint32_t>(video.size());
  const uint32_t audioLen = static_cast<uint32_t>(audio.size() * sizeof(float));
  if (previewLen > videoLen) previewLen = videoLen;

  FrameRecord rec;
  rec.tsUs = tsUs;
  rec.offset = essencePos_;
  rec.videoLen = videoLen;
  rec.previewLen = previewLen;
  rec.audioLen = audioLen;

  std::vector<uint8_t> head;
  head.reserve(kEssenceHeaderSize);
  head.insert(head.end(), reinterpret_cast<const uint8_t*>(kMagic), reinterpret_cast<const uint8_t*>(kMagic) + 4);
  put64(head, tsUs);
  put32(head, videoLen);
  put32(head, audioLen);
  put32(head, previewLen);

  if (head.size() != kEssenceHeaderSize) throw std::runtime_error("segment header size drifted");
  HANDLE ess = static_cast<HANDLE>(essence_);
  writeAll(ess, head.data(), head.size());
  if (audioLen) writeAll(ess, audio.data(), audioLen);
  if (videoLen) writeAll(ess, video.data(), videoLen);
  essencePos_ += static_cast<int64_t>(kEssenceHeaderSize) + audioLen + videoLen;

  // The record reaches the file at once so a reader sees it; FlushFileBuffers is the crash
  // barrier and only runs every 0.5 s of timestamps.
  writeAll(static_cast<HANDLE>(index_), &rec, sizeof(rec));
  ++frames_;
  if (lastFlushTs_ == INT64_MIN || tsUs - lastFlushTs_ >= kFlushIntervalUs) {
    flushIndex();
    lastFlushTs_ = tsUs;
  }
}

void SegmentWriter::flushIndex() {
  if (index_) FlushFileBuffers(static_cast<HANDLE>(index_));
}

void SegmentWriter::close() {
  if (index_) {
    FlushFileBuffers(static_cast<HANDLE>(index_));
    CloseHandle(static_cast<HANDLE>(index_));
    index_ = nullptr;
  }
  if (essence_) {
    FlushFileBuffers(static_cast<HANDLE>(essence_));
    CloseHandle(static_cast<HANDLE>(essence_));
    essence_ = nullptr;
  }
}

size_t SegmentWriter::frames() const { return frames_; }
int64_t SegmentWriter::bytes() const { return essencePos_; }

std::optional<SegmentReader> SegmentReader::open(const std::filesystem::path& base) {
  SegmentReader r;
  r.base_ = base;
  HANDLE idx = openRead(withExt(base, ".vmxi"));
  if (idx == INVALID_HANDLE_VALUE) return std::nullopt;
  const int64_t n = sizeOf(idx);
  std::vector<uint8_t> buf(static_cast<size_t>(std::min<int64_t>(n, 4096)));
  const size_t take = static_cast<size_t>(std::min<int64_t>(n, static_cast<int64_t>(buf.size())));
  if (take == 0 || !readAt(idx, 0, buf.data(), take) || !parseHeader({buf.data(), take}, r.header_, r.recordsOffset_)) {
    CloseHandle(idx);
    return std::nullopt;
  }
  CloseHandle(idx);
  r.refresh();
  return r;
}

void SegmentReader::refresh() {
  HANDLE idx = openRead(withExt(base_, ".vmxi"));
  if (idx == INVALID_HANDLE_VALUE) return;
  const int64_t n = sizeOf(idx);
  const size_t have = records_.size();
  const int64_t available = n > recordsOffset_ ? (n - recordsOffset_) / 32 : 0;
  if (static_cast<int64_t>(have) < available) {
    const size_t add = static_cast<size_t>(available) - have;
    std::vector<FrameRecord> more(add);
    if (readAt(idx, recordsOffset_ + static_cast<int64_t>(have) * 32, more.data(), add * sizeof(FrameRecord))) {
      records_.insert(records_.end(), more.begin(), more.end());
    }
  }
  CloseHandle(idx);
}

std::optional<size_t> SegmentReader::indexAtOrBefore(int64_t tsUs) const {
  auto it = std::upper_bound(records_.begin(), records_.end(), tsUs,
                             [](int64_t t, const FrameRecord& r) { return t < r.tsUs; });
  if (it == records_.begin()) return std::nullopt;
  return static_cast<size_t>(std::distance(records_.begin(), it) - 1);
}

std::vector<uint8_t> SegmentReader::readVideo(size_t i, bool previewOnly) const {
  if (i >= records_.size()) return {};
  const auto& r = records_[i];
  const size_t len = previewOnly ? r.previewLen : r.videoLen;
  std::vector<uint8_t> out(len);
  HANDLE ess = openRead(withExt(base_, ".vmxs"));
  if (ess == INVALID_HANDLE_VALUE) return {};
  const bool ok = readAt(ess, r.offset + kEssenceHeaderSize + r.audioLen, out.data(), len);
  CloseHandle(ess);
  return ok ? out : std::vector<uint8_t>{};
}

std::vector<float> SegmentReader::readAudio(size_t i) const {
  if (i >= records_.size()) return {};
  const auto& r = records_[i];
  std::vector<float> out(r.audioLen / sizeof(float));
  if (r.audioLen == 0) return out;
  HANDLE ess = openRead(withExt(base_, ".vmxs"));
  if (ess == INVALID_HANDLE_VALUE) return {};
  const bool ok = readAt(ess, r.offset + kEssenceHeaderSize, out.data(), r.audioLen);
  CloseHandle(ess);
  return ok ? out : std::vector<float>{};
}

void recoverSegment(const std::filesystem::path& base) {
  const auto idxPath = withExt(base, ".vmxi");
  const auto essPath = withExt(base, ".vmxs");
  HANDLE idx = openRead(idxPath);
  if (idx == INVALID_HANDLE_VALUE) return;
  SegmentHeader h;
  int64_t headerSize = 0;
  std::vector<uint8_t> head(4096);
  const size_t take = static_cast<size_t>(std::min<int64_t>(sizeOf(idx), 4096));
  if (take == 0 || !readAt(idx, 0, head.data(), take) || !parseHeader({head.data(), take}, h, headerSize)) {
    CloseHandle(idx);
    return;
  }
  const int64_t n = sizeOf(idx);
  const size_t count = n > headerSize ? static_cast<size_t>((n - headerSize) / 32) : 0;
  std::vector<FrameRecord> recs(count);
  if (count && !readAt(idx, headerSize, recs.data(), count * sizeof(FrameRecord))) {
    CloseHandle(idx);
    return;
  }
  CloseHandle(idx);

  HANDLE ess = openRead(essPath);
  if (ess == INVALID_HANDLE_VALUE) return;
  const int64_t essSize = sizeOf(ess);
  int64_t goodEnd = 0;
  size_t keep = 0;
  for (const auto& r : recs) {
    const int64_t end = r.offset + static_cast<int64_t>(kEssenceHeaderSize) + r.audioLen + r.videoLen;
    if (end > essSize || r.offset < goodEnd) break;
    goodEnd = end;
    ++keep;
  }
  if (keep == count) {
    CloseHandle(ess);
    return;  // nothing to drop; leave both files alone
  }
  // Drop the incomplete tail from both files, so the next writer appends after a whole frame.
  LARGE_INTEGER pos;
  pos.QuadPart = goodEnd;
  SetFilePointerEx(ess, pos, nullptr, FILE_BEGIN);
  SetEndOfFile(ess);
  FlushFileBuffers(ess);
  CloseHandle(ess);

  HANDLE w = openWrite(idxPath, true);
  if (w != INVALID_HANDLE_VALUE) {
    pos.QuadPart = static_cast<int64_t>(headerSize) + static_cast<int64_t>(keep) * 32;
    SetFilePointerEx(w, pos, nullptr, FILE_BEGIN);
    SetEndOfFile(w);
    FlushFileBuffers(w);
    CloseHandle(w);
  }
}

}  // namespace br