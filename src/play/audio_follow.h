#pragma once
#include <cstdint>
#include <vector>

#include "play/frame_source.h"

namespace br {

// Audio follows the angle on air at 100 %; slow motion is silent; an angle cut gets a 10 ms ramp.
class AudioFollow {
 public:
  explicit AudioFollow(int sampleRate = 48000, int channels = 2);
  std::vector<float> forTick(const DecodedFrame* f, double speed, bool angleChanged, uint32_t frames);

 private:
  int sampleRate_;
  int channels_;
  std::vector<float> pending_;
  int64_t lastTs_ = 0;
};

}  // namespace br