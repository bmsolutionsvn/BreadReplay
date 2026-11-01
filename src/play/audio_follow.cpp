#include "play/audio_follow.h"

#include <algorithm>
#include <cmath>

namespace br {

AudioFollow::AudioFollow(int sampleRate, int channels) : sampleRate_(sampleRate), channels_(channels) {}

std::vector<float> AudioFollow::forTick(const DecodedFrame* f, double speed, bool angleChanged,
                                        uint32_t frames) {
  const size_t n = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
  std::vector<float> out(n, 0.0f);
  if (std::abs(speed - 1.0) >= 0.01) return out;  // slow motion is muted, never pitched

  if (f && !f->audio.empty() && f->tsUs != lastTs_) {
    pending_.insert(pending_.end(), f->audio.begin(), f->audio.end());
    lastTs_ = f->tsUs;
    // Do not let a stalled tick loop grow the buffer without bound.
    const size_t maxKeep = static_cast<size_t>(sampleRate_) * static_cast<size_t>(channels_) * 2;
    if (pending_.size() > maxKeep) pending_.erase(pending_.begin(), pending_.end() - static_cast<ptrdiff_t>(maxKeep));
  }
  const size_t take = std::min(n, pending_.size());
  std::copy_n(pending_.begin(), take, out.begin());
  pending_.erase(pending_.begin(), pending_.begin() + static_cast<ptrdiff_t>(take));

  if (angleChanged) {
    const size_t ramp = std::min<size_t>(480, n);  // 10 ms at 48 kHz
    for (size_t i = 0; i < ramp; ++i) out[i] *= static_cast<float>(i) / static_cast<float>(ramp);
  }
  return out;
}

}  // namespace br