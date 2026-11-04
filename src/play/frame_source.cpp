#include "play/frame_source.h"

#include <deque>

namespace br {
namespace {
constexpr size_t kCacheBytesTotal = 1ull << 30;  // 1 GB of NV12 across every input
}  // namespace

FrameSource::FrameSource(std::map<std::string, const InputStore*> stores, bc::GpuDevice* gpu,
                         bc::TexturePool* pool, int threads, size_t cacheFrames)
    : stores_(std::move(stores)), gpu_(gpu), pool_(pool), cacheFrames_(cacheFrames ? cacheFrames : 1) {
  (void)threads;  // shortcut: decode runs on the caller's thread, one frame per tick; a worker
                  // pool is only needed once two players decode big frames at once.
  if (gpu_ && pool_) conv_ = std::make_unique<bc::Converter>(*gpu_, *pool_);
}

void FrameSource::ensureDecoder(const std::string& id, size_t lane, uint32_t w, uint32_t h) {
  auto& d = decoders_[{id, lane}];
  if (!d.decoder || d.w != w || d.h != h) {
    d.decoder = std::make_unique<VmxDecoder>(w, h);
    d.w = w;
    d.h = h;
  }
}

void FrameSource::ensureHx(Dec& d, StoredCodec codec, uint32_t w, uint32_t h) {
  if (d.hx && d.codec == codec && d.w == w && d.h == h) return;
  d.hx = std::make_unique<bc::GpuDecoder>(*gpu_, *pool_,
                                          codec == StoredCodec::Hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
  d.codec = codec;
  d.w = w;
  d.h = h;
  d.fedIndex = SIZE_MAX;
  d.primed = false;
}

void FrameSource::touch(const Key& key) {
  for (auto it = lru_.begin(); it != lru_.end(); ++it) {
    if (it->input == key.input && it->tsUs == key.tsUs) {
      lru_.splice(lru_.begin(), lru_, it);
      return;
    }
  }
  lru_.push_front(key);
}

size_t FrameSource::bytesOf(const DecodedFrame& f) {
  const size_t pixels = static_cast<size_t>(f.w) * f.h;
  if (f.picture.tex) {
    if (f.picture.layout == bc::PixelLayout::P010) return pixels * 3;
    if (f.picture.layout == bc::PixelLayout::BGRA8) return pixels * 4;
    return pixels * 3 / 2;
  }
  return f.uyvy.size();
}

void FrameSource::evict() {
  std::map<std::string, size_t> counts;
  for (const auto& [key, frame] : cache_) ++counts[key.input];
  while (!lru_.empty()) {
    std::string over;
    for (const auto& [input, n] : counts) {
      if (n > cacheFrames_) {
        over = input;
        break;
      }
    }
    if (over.empty() && cacheBytes_ <= kCacheBytesTotal) break;
    // Evict this input's least recently used frame (or the global LRU for the byte budget).
    auto victim = lru_.end();
    for (auto it = lru_.end(); it != lru_.begin();) {
      --it;
      if (over.empty() || it->input == over) {
        victim = it;
        break;
      }
    }
    if (victim == lru_.end()) break;
    const Key old = *victim;
    lru_.erase(victim);
    auto it = cache_.find(old);
    if (it == cache_.end()) continue;
    cacheBytes_ -= bytesOf(*it->second);
    cache_.erase(it);
    if (const auto n = counts.find(old.input); n != counts.end() && n->second > 0) --n->second;
  }
}

std::shared_ptr<const DecodedFrame> FrameSource::get(const std::string& inputId, int64_t tsUs,
                                                     size_t lane) {
  std::lock_guard lock(m_);
  const auto sit = stores_.find(inputId);
  if (sit == stores_.end() || !sit->second) return nullptr;
  const InputStore& store = *sit->second;

  const auto ref = store.find(tsUs);
  if (!ref) return nullptr;
  const Key key{inputId, ref->tsUs};
  if (auto it = cache_.find(key); it != cache_.end()) {
    if (!it->second->provisional) {
      touch(key);
      return it->second;
    }
    // A frame cached while its GOP was open may be decode order: let the compressed path re-check it.
  }
  if (store.codecOf(*ref) != StoredCodec::Vmx) {
    // An unsupported profile or a failed allocation holds the last picture, never kills the tick.
    try {
      return getCompressed(inputId, store, *ref, lane);
    } catch (const std::exception&) {
      return nullptr;
    }
  }

  const auto& header = store.header();
  // Decode at the size this frame was recorded at: an input can change size mid-show.
  const auto [w, h] = store.frameSize(*ref);
  if (!w || !h) return nullptr;
  ensureDecoder(inputId, lane, w, h);
  const auto bytes = store.readVideo(*ref, false);
  if (bytes.empty()) return nullptr;  // the writer has not reached this frame yet

  auto frame = std::make_shared<DecodedFrame>();
  frame->tsUs = ref->tsUs;
  frame->w = w;
  frame->h = h;
  frame->uyvy.resize(static_cast<size_t>(w) * h * 2);
  decoders_[{inputId, lane}].decoder->decodeUYVY(bytes, frame->uyvy.data(), static_cast<int>(w * 2));
  frame->audio = store.readAudio(*ref);
  frame->channels = header.audioChannels;
  frame->sampleRate = header.sampleRate;
  frame->inGap = store.isGapAt(tsUs);
  ++diskReads_;

  cacheBytes_ += bytesOf(*frame);
  cache_[key] = frame;
  touch(key);
  evict();
  return frame;
}

void FrameSource::cachePicture(const std::string& id, const InputStore& store, bc::VideoFrameIn pic,
                               uint32_t w, uint32_t h, int64_t slotTs, bool provisional) {
  if (!pic.tex) return;
  ++decodes_;
  const int64_t ts = slotTs;
  const Key key{id, ts};
  if (cache_.count(key)) return;

  auto frame = std::make_shared<DecodedFrame>();
  frame->tsUs = ts;
  frame->w = w;
  frame->h = h;
  frame->provisional = provisional;
  frame->picture = std::move(pic);
  if (const auto ref = store.find(ts)) {
    frame->audio = store.readAudio(*ref);
    frame->inGap = store.isGapAt(ts);
  }
  frame->channels = store.header().audioChannels;
  frame->sampleRate = store.header().sampleRate;
  ++diskReads_;

  cacheBytes_ += bytesOf(*frame);
  cache_[key] = frame;
  touch(key);
  evict();
}

std::shared_ptr<const DecodedFrame> FrameSource::getCompressed(const std::string& id, const InputStore& store,
                                                               const FrameRef& ref, size_t lane) {
  if (!gpu_ || !pool_ || !conv_) return nullptr;  // no device: a compressed frame cannot be shown
  const auto [w, h] = store.frameSize(ref);
  if (!w || !h) return nullptr;

  auto& d = decoders_[{id, lane}];
  ensureHx(d, store.codecOf(ref), w, h);
  const auto k = store.keyframeAtOrBefore(ref);
  if (!k || k->segment != ref.segment) return nullptr;
  const size_t seg = ref.segment;
  const Key want{id, ref.tsUs};
  const size_t count = store.frameCountIn(seg);
  const size_t gopEnd = std::min(count, store.nextKeyframeAfter(*k));
  // The last GOP of the segment being written can still gain AUs, so its display order is not final.
  const bool gopFinal = gopEnd < count || !store.isWriting(seg);
  if (gopFinal) {
    if (provisional_.erase(std::make_tuple(id, seg, k->index))) {
      // Slots filled while the GOP was open may hold decode-order pictures: drop them and re-feed.
      const auto first = store.frameAt(seg, k->index);
      const auto last = store.frameAt(seg, gopEnd - 1);
      if (first && last) {
        auto it = cache_.lower_bound(Key{id, first->tsUs});
        while (it != cache_.end() && it->first.input == id && it->first.tsUs <= last->tsUs) {
          cacheBytes_ -= bytesOf(*it->second);
          for (auto l = lru_.begin(); l != lru_.end(); ++l) {
            if (l->input == id && l->tsUs == it->first.tsUs) {
              lru_.erase(l);
              break;
            }
          }
          it = cache_.erase(it);
        }
      }
      d.hx->reset();
      d.primed = false;
    }
  } else {
    provisional_.insert(std::make_tuple(id, seg, k->index));
    if (auto it = cache_.find(want); it != cache_.end()) return it->second;  // already served this slot
  }

  const auto feed = [&](size_t start) {
    // Within one GOP the r-th picture in display order takes the r-th arrival slot.
    std::map<int64_t, std::deque<int64_t>> slots;
    {
      std::vector<std::pair<int64_t, size_t>> order;  // (source pts, decode index)
      order.reserve(gopEnd > k->index ? gopEnd - k->index : 0);
      for (size_t i = k->index; i < gopEnd; ++i) {
        const auto fr = store.frameAt(seg, i);
        if (!fr) break;
        order.emplace_back(store.srcPtsOf(*fr), i);
      }
      // An open GOP or a source that gives no ordering (all pts equal) keeps decode order.
      if (gopFinal && order.size() > 1 && order.front().first != order.back().first) {
        std::sort(order.begin(), order.end());
      }
      for (size_t r = 0; r < order.size(); ++r) {
        const auto slot = store.frameAt(seg, k->index + r);
        if (!slot) break;
        slots[order[r].first].push_back(slot->tsUs);
      }
    }
    // A repeated source pts (a broken source) takes the slots in decode order, one per picture.
    const auto takeSlot = [&slots](int64_t pts) -> int64_t {
      const auto it = slots.find(pts);
      if (it == slots.end() || it->second.empty()) return INT64_MIN;
      const int64_t ts = it->second.front();
      it->second.pop_front();
      return ts;
    };

    size_t i = start;
    for (; i < count; ++i) {
      const auto fr = store.frameAt(seg, i);
      if (!fr) break;
      const auto au = store.readVideo(*fr, false);
      if (au.empty()) break;
      d.hx->decode(au, store.srcPtsOf(*fr), [&](bc::VideoFrameIn pic) {
        const int64_t slot = takeSlot(pic.ptsUs);
        if (slot != INT64_MIN) cachePicture(id, store, std::move(pic), w, h, slot, !gopFinal);
      });
      d.primed = true;
      d.seg = seg;
      d.fedIndex = i;
      if (i < ref.index) continue;
      if (cache_.count(want)) return true;
      // A B-frame's picture comes out after the references that follow it; feed on until it does.
      if (i > ref.index + 16) break;
    }
    if (i == count) {
      // The last pictures of the segment sit in the decoder until it is drained.
      d.hx->flush([&](bc::VideoFrameIn pic) {
        const int64_t slot = takeSlot(pic.ptsUs);
        if (slot != INT64_MIN) cachePicture(id, store, std::move(pic), w, h, slot, !gopFinal);
      });
      d.primed = false;
    }
    return cache_.count(want) != 0;
  };

  // Carry on when the decoder is at or before the target, or in the reorder tail just past it.
  const bool carryOn = d.primed && d.seg == seg && d.fedIndex != SIZE_MAX && d.fedIndex >= k->index &&
                       (d.fedIndex < ref.index || ref.index + 16 >= d.fedIndex);
  bool found = false;
  if (carryOn) {
    found = feed(d.fedIndex + 1);
  } else {
    d.hx->reset();  // seek: the keyframe is the first decodable frame
    d.primed = false;
    found = feed(k->index);
  }
  if (!found && carryOn) {
    // The picture was fed earlier and has left the cache since: one seek is the only way back.
    d.hx->reset();
    d.primed = false;
    feed(k->index);
  }
  const auto it = cache_.find(want);
  return it == cache_.end() ? nullptr : it->second;
}

void FrameSource::prefetch(const std::string& inputId, int64_t fromUs, int64_t stepUs, int count) {
  for (int i = 0; i < count; ++i) get(inputId, fromUs + static_cast<int64_t>(i) * stepUs);
}

std::shared_ptr<const PreviewImage> FrameSource::preview(const std::string& inputId, int64_t tsUs) {
  std::lock_guard lock(m_);
  const auto sit = stores_.find(inputId);
  if (sit == stores_.end() || !sit->second) return nullptr;
  const InputStore& store = *sit->second;
  const auto ref = store.find(tsUs);
  if (!ref) return nullptr;
  const auto [w, h] = store.frameSize(*ref);
  if (!w || !h) return nullptr;
  if (store.codecOf(*ref) == StoredCodec::Vmx) {
    ensureDecoder(inputId, 2, w, h);
    const auto prefix = store.readVideo(*ref, true);
    if (prefix.empty()) return nullptr;
    const Size ps = VmxDecoder::previewSize(w, h);
    auto image = std::make_shared<PreviewImage>();
    image->tsUs = ref->tsUs;
    image->w = ps.w;
    image->h = ps.h;
    image->bgra.resize(static_cast<size_t>(ps.w) * ps.h * 4);
    decoders_[{inputId, 2}].decoder->decodePreviewBGRA(prefix, image->bgra.data(), static_cast<int>(ps.w * 4));
    ++diskReads_;
    return image;
  }

  try {
  if (!gpu_ || !pool_ || !conv_) return nullptr;
  const auto key = store.keyframeAtOrBefore(*ref);
  if (!key || key->segment != ref->segment) return nullptr;
  auto& d = decoders_[{inputId, 2}];
  const StoredCodec codec = store.codecOf(*key);
  if (!d.preview || d.previewCodec != codec || d.pw != w || d.ph != h) {
    d.preview = std::make_unique<bc::GpuDecoder>(*gpu_, *pool_,
                                                 codec == StoredCodec::Hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    d.previewCodec = codec;
    d.pw = w;
    d.ph = h;
    d.previewKeyTs = INT64_MIN;
    d.previewImage.reset();
  }
  if (d.previewKeyTs == key->tsUs && d.previewImage) return d.previewImage;

  d.preview->reset();
  std::shared_ptr<const PreviewImage> image;
  const auto build = [&](bc::VideoFrameIn pic) {
    if (image || !pic.tex) return;
    const Size ps = VmxDecoder::previewSize(w, h);
    auto linear = conv_->toLinear(*pic.tex, pic.layout, pic.fullRange, pic.bt709);
    auto fitted = conv_->fit(*linear, ps.w, ps.h);
    auto bgraTex = conv_->toBgra8(*fitted, ps.w, ps.h);
    auto cpu = conv_->readback(*bgraTex);
    auto out = std::make_shared<PreviewImage>();
    out->tsUs = key->tsUs;
    out->w = ps.w;
    out->h = ps.h;
    out->bgra = std::move(cpu.bgra);
    image = std::move(out);
  };
  // A keyframe usually emits at once; feed a few access units if the decoder still buffers it.
  for (size_t i = key->index; i < store.frameCountIn(key->segment) && !image; ++i) {
    const auto fr = store.frameAt(key->segment, i);
    if (!fr) break;
    const auto au = store.readVideo(*fr, false);
    if (au.empty()) break;
    d.preview->decode(au, fr->tsUs, build);
    if (i > key->index + 4) break;
  }
  if (image) {
    d.previewKeyTs = key->tsUs;
    d.previewImage = image;
    ++diskReads_;
  }
  return image;
  } catch (const std::exception&) {
    return nullptr;
  }
}

}  // namespace br