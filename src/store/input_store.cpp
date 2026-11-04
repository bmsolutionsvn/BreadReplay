#include "store/input_store.h"

#include <algorithm>
#include <ctime>
#include <cstring>
#include <stdexcept>

namespace br {
namespace {
std::tm localTime(int64_t epochUs) {
  const std::time_t s = static_cast<std::time_t>(epochUs / 1'000'000);
  std::tm t{};
  localtime_s(&t, &s);
  return t;
}

int64_t minuteKey(int64_t epochUs) {
  const std::tm t = localTime(epochUs);
  return (static_cast<int64_t>(t.tm_year) << 20) | (static_cast<int64_t>(t.tm_mon) << 16) |
         (static_cast<int64_t>(t.tm_mday) << 11) | (static_cast<int64_t>(t.tm_hour) << 6) | t.tm_min;
}

std::string stamp(int64_t epochUs) {
  const std::tm t = localTime(epochUs);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &t);
  return buf;
}
}  // namespace

InputStore::InputStore(std::filesystem::path dir, SegmentHeader proto)
    : dir_(std::move(dir)), proto_(std::move(proto)) {
  std::filesystem::create_directories(dir_);
  periodUs_ = proto_.rate.fps() > 0 ? static_cast<int64_t>(1'000'000.0 / proto_.rate.fps() + 0.5) : 33'333;

  for (const auto& e : std::filesystem::directory_iterator(dir_)) {
    if (!e.is_regular_file() || e.path().extension() != ".vmxs") continue;
    auto base = e.path();
    base.replace_extension();
    recoverSegment(base);
    auto r = SegmentReader::open(base);
    if (!r) continue;
    Seg s;
    s.base = base;
    s.reader = std::make_unique<SegmentReader>(std::move(*r));
    for (const auto& rec : s.reader->records()) s.times.push_back(rec.tsUs);
    s.bytes = static_cast<int64_t>(std::filesystem::file_size(e.path())) +
              static_cast<int64_t>(std::filesystem::file_size(std::filesystem::path(base.string() + ".vmxi")));
    segs_.push_back(std::move(s));
  }
  std::sort(segs_.begin(), segs_.end(),
            [](const Seg& a, const Seg& b) { return a.times.empty() ? false : (b.times.empty() ? true : a.times.front() < b.times.front()); });
}

void InputStore::append(int64_t tsUs, std::span<const uint8_t> video, uint32_t previewLen,
                        std::span<const float> audio, bool keyframe, int64_t srcPtsUs) {
  std::unique_lock lock(m_);
  const int64_t key = minuteKey(tsUs);
  // Compare the minute with the segment's start, so a segment that crossed a minute mid-GOP still
  // rolls at the first keyframe after it.
  const bool minuteChanged = !segs_.empty() && !segs_.back().times.empty() &&
                             minuteKey(segs_.back().times.front()) != key;
  // A compressed segment rolls at the first keyframe after the minute, not mid-GOP: closing on
  // the minute would throw away every frame until the next keyframe.
  if (minuteChanged && keyframe && !forceNewSegment_ && segs_.back().writer) {
    segs_.back().writer->close();
    segs_.back().writer.reset();
  }
  const bool continuing = !forceNewSegment_ && !segs_.empty() && !segs_.back().times.empty() &&
                          (segs_.back().writer ||
                           (minuteKey(segs_.back().times.back()) == key &&
                            !segs_.back().reader->records().empty()));
  if (!keyframe && !continuing) {
    ++droppedBeforeKeyframe_;
    return;
  }
  if (segs_.empty() || !segs_.back().writer) {
    const bool canAppend = !forceNewSegment_ && !segs_.empty() && !segs_.back().times.empty() &&
                           minuteKey(segs_.back().times.back()) == key && !segs_.back().reader->records().empty();
    if (canAppend) {
      segs_.back().writer = std::make_unique<SegmentWriter>(segs_.back().base, proto_);
    } else {
      forceNewSegment_ = false;
      std::filesystem::path base = dir_ / stamp(tsUs);
      for (int n = 1; std::filesystem::exists(base.string() + ".vmxs"); ++n) {
        base = dir_ / (stamp(tsUs) + "-" + std::to_string(n));
      }
      Seg s;
      s.base = base;
      s.writer = std::make_unique<SegmentWriter>(base, proto_);
      auto r = SegmentReader::open(base);
      s.reader = std::make_unique<SegmentReader>(r ? std::move(*r) : SegmentReader{});
      segs_.push_back(std::move(s));
    }
  }
  Seg& s = segs_.back();
  s.writer->append(tsUs, video, previewLen, audio, keyframe, srcPtsUs);
  s.times.push_back(tsUs);
  s.bytes += static_cast<int64_t>(24) + static_cast<int64_t>(audio.size() * sizeof(float)) +
             static_cast<int64_t>(video.size());
}

std::optional<FrameRef> InputStore::find(int64_t tsUs) const {
  std::shared_lock lock(m_);
  for (size_t si = segs_.size(); si-- > 0;) {
    const auto& times = segs_[si].times;
    if (times.empty()) continue;
    auto it = std::upper_bound(times.begin(), times.end(), tsUs);
    if (it == times.begin()) continue;
    const size_t idx = static_cast<size_t>(std::distance(times.begin(), it) - 1);
    return FrameRef{si, idx, times[idx]};
  }
  return std::nullopt;
}

std::optional<FrameRef> InputStore::frameAt(size_t segment, size_t index) const {
  std::shared_lock lock(m_);
  if (segment >= segs_.size() || index >= segs_[segment].times.size()) return std::nullopt;
  return FrameRef{segment, index, segs_[segment].times[index]};
}

size_t InputStore::frameCountIn(size_t segment) const {
  std::shared_lock lock(m_);
  return segment < segs_.size() ? segs_[segment].times.size() : 0;
}

bool InputStore::isWriting(size_t segment) const {
  std::shared_lock lock(m_);
  return segment < segs_.size() && segs_[segment].writer != nullptr;
}

int64_t InputStore::srcPtsOf(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return ref.tsUs;
  auto* r = const_cast<SegmentReader*>(segs_[ref.segment].reader.get());
  if (ref.index >= r->records().size()) r->refresh();  // no file open once the record is loaded
  const auto& recs = r->records();
  return ref.index < recs.size() ? recs[ref.index].srcPtsUs : ref.tsUs;
}

size_t InputStore::nextKeyframeAfter(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return ref.index + 1;
  auto* r = const_cast<SegmentReader*>(segs_[ref.segment].reader.get());
  r->refresh();
  const auto& recs = r->records();
  for (size_t i = ref.index + 1; i < recs.size(); ++i) {
    if (recs[i].flags & FrameRecord::kKeyframe) return i;
  }
  return recs.size();
}

bool InputStore::isGapAt(int64_t tsUs) const {
  const auto ref = find(tsUs);
  if (!ref) return true;
  return tsUs - ref->tsUs > gapUs();
}

std::vector<uint8_t> InputStore::readVideo(const FrameRef& ref, bool previewOnly) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return {};
  auto* r = const_cast<SegmentReader*>(segs_[ref.segment].reader.get());
  r->refresh();
  return r->readVideo(ref.index, previewOnly);
}

std::pair<uint32_t, uint32_t> InputStore::frameSize(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return {0, 0};
  const auto& h = segs_[ref.segment].reader->header();
  return {h.width, h.height};
}

std::vector<float> InputStore::readAudio(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return {};
  auto* r = const_cast<SegmentReader*>(segs_[ref.segment].reader.get());
  r->refresh();
  return r->readAudio(ref.index);
}

std::vector<SegmentInfo> InputStore::segments() const {
  std::shared_lock lock(m_);
  std::vector<SegmentInfo> out;
  out.reserve(segs_.size());
  for (const auto& s : segs_) {
    SegmentInfo info;
    info.base = s.base;
    info.firstTs = s.times.empty() ? 0 : s.times.front();
    info.lastTs = s.times.empty() ? 0 : s.times.back();
    info.bytes = s.bytes;
    out.push_back(info);
  }
  return out;
}

bool InputStore::removeSegment(const std::filesystem::path& base) {
  std::unique_lock lock(m_);
  for (auto it = segs_.begin(); it != segs_.end(); ++it) {
    if (it->base != base) continue;
    if (it->writer) return false;  // the open segment keeps recording
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(base.string() + ".vmxs"), ec);
    std::filesystem::remove(std::filesystem::path(base.string() + ".vmxi"), ec);
    segs_.erase(it);
    return true;
  }
  return false;
}

void InputStore::changeVideoFormat(uint32_t w, uint32_t h, bc::Rate rate, StoredCodec codec,
                                   StoredColour colour) {
  std::unique_lock lock(m_);
  if (!segs_.empty() && segs_.back().writer) {
    segs_.back().writer->close();
    segs_.back().writer.reset();
  }
  proto_.width = w;
  proto_.height = h;
  proto_.rate = rate;
  proto_.codec = codec;
  proto_.colour = colour;
  forceNewSegment_ = true;
  periodUs_ = rate.fps() > 0 ? static_cast<int64_t>(1'000'000.0 / rate.fps() + 0.5) : 33'333;
}

std::vector<std::pair<int64_t, int64_t>> InputStore::coverage() const {
  std::shared_lock lock(m_);
  std::vector<std::pair<int64_t, int64_t>> out;
  for (const auto& s : segs_) {
    for (int64_t t : s.times) {
      if (out.empty() || t - out.back().second > gapUs()) {
        out.emplace_back(t, t);
      } else {
        out.back().second = t;
      }
    }
  }
  return out;
}

int64_t InputStore::newestTs() const {
  std::shared_lock lock(m_);
  if (segs_.empty() || segs_.back().times.empty()) return 0;
  return segs_.back().times.back();
}

int64_t InputStore::bytesOnDisk() const {
  std::shared_lock lock(m_);
  int64_t sum = 0;
  for (const auto& s : segs_) sum += s.bytes;
  return sum;
}

size_t InputStore::frameCount() const {
  std::shared_lock lock(m_);
  size_t n = 0;
  for (const auto& s : segs_) n += s.times.size();
  return n;
}

size_t InputStore::droppedBeforeKeyframe() const {
  std::shared_lock lock(m_);
  return droppedBeforeKeyframe_;
}

StoredCodec InputStore::codecOf(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return StoredCodec::Vmx;
  return segs_[ref.segment].reader->header().codec;
}

StoredColour InputStore::colourOf(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return StoredColour::Unknown;
  return segs_[ref.segment].reader->header().colour;
}

bool InputStore::isKeyframe(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (ref.segment >= segs_.size() || !segs_[ref.segment].reader) return false;
  auto* r = const_cast<SegmentReader*>(segs_[ref.segment].reader.get());
  r->refresh();
  const auto& recs = r->records();
  if (ref.index >= recs.size()) return false;
  if (r->header().version < 2) return true;  // v1 had no flag: every frame was a picture
  return (recs[ref.index].flags & FrameRecord::kKeyframe) != 0;
}

std::optional<FrameRef> InputStore::keyframeAtOrBefore(const FrameRef& ref) const {
  std::shared_lock lock(m_);
  if (segs_.empty()) return std::nullopt;
  const size_t start = std::min(ref.segment, segs_.size() - 1);
  for (size_t si = start + 1; si-- > 0;) {
    auto& s = segs_[si];
    if (s.times.empty() || !s.reader) continue;
    auto* r = const_cast<SegmentReader*>(s.reader.get());
    r->refresh();
    const auto& recs = r->records();
    const bool allKey = r->header().version < 2;
    auto it = std::upper_bound(s.times.begin(), s.times.end(), ref.tsUs);
    for (size_t n = static_cast<size_t>(std::distance(s.times.begin(), it)); n-- > 0;) {
      if (n >= recs.size()) continue;  // record not in the file yet
      if (allKey || (recs[n].flags & FrameRecord::kKeyframe)) return FrameRef{si, n, s.times[n]};
    }
  }
  return std::nullopt;
}

}  // namespace br