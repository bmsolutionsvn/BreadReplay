#include "play/frame_source.h"

namespace br {

FrameSource::FrameSource(std::map<std::string, const InputStore*> stores, int threads, size_t cacheFrames)
    : stores_(std::move(stores)), cacheFrames_(cacheFrames ? cacheFrames : 1) {
  (void)threads;  // shortcut: decode runs on the caller's thread, one frame per tick; a worker
                  // pool is only needed once two players decode big frames at once.
}

void FrameSource::ensureDecoder(const std::string& id, uint32_t w, uint32_t h) {
  auto& d = decoders_[id];
  if (!d.decoder || d.w != w || d.h != h) {
    d.decoder = std::make_unique<VmxDecoder>(w, h);
    d.w = w;
    d.h = h;
  }
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

std::shared_ptr<const DecodedFrame> FrameSource::get(const std::string& inputId, int64_t tsUs) {
  std::lock_guard lock(m_);
  const auto sit = stores_.find(inputId);
  if (sit == stores_.end() || !sit->second) return nullptr;
  const InputStore& store = *sit->second;

  const auto ref = store.find(tsUs);
  if (!ref) return nullptr;
  const Key key{inputId, ref->tsUs};
  if (auto it = cache_.find(key); it != cache_.end()) {
    touch(key);
    return it->second;
  }

  const auto& header = store.header();
  // Decode at the size this frame was recorded at: an input can change size mid-show.
  const auto [w, h] = store.frameSize(*ref);
  if (!w || !h) return nullptr;
  ensureDecoder(inputId, w, h);
  const auto bytes = store.readVideo(*ref, false);
  if (bytes.empty()) return nullptr;  // the writer has not reached this frame yet

  auto frame = std::make_shared<DecodedFrame>();
  frame->tsUs = ref->tsUs;
  frame->w = w;
  frame->h = h;
  frame->uyvy.resize(static_cast<size_t>(w) * h * 2);
  decoders_[inputId].decoder->decodeUYVY(bytes, frame->uyvy.data(), static_cast<int>(w * 2));
  frame->audio = store.readAudio(*ref);
  frame->channels = header.audioChannels;
  frame->sampleRate = header.sampleRate;
  frame->inGap = store.isGapAt(tsUs);
  ++diskReads_;

  cache_[key] = frame;
  touch(key);
  while (cache_.size() > cacheFrames_) {
    const Key& old = lru_.back();
    cache_.erase(old);
    lru_.pop_back();
  }
  return frame;
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
  ensureDecoder(inputId, w, h);
  const auto prefix = store.readVideo(*ref, true);
  if (prefix.empty()) return nullptr;
  const Size ps = VmxDecoder::previewSize(w, h);
  auto image = std::make_shared<PreviewImage>();
  image->tsUs = ref->tsUs;
  image->w = ps.w;
  image->h = ps.h;
  image->bgra.resize(static_cast<size_t>(ps.w) * ps.h * 4);
  decoders_[inputId].decoder->decodePreviewBGRA(prefix, image->bgra.data(), static_cast<int>(ps.w * 4));
  ++diskReads_;
  return image;
}

}  // namespace br