#include <doctest/doctest.h>

#include <cmath>

#include "play/audio_follow.h"

namespace {
br::DecodedFrame audioFrame(int64_t ts, float value) {
  br::DecodedFrame f;
  f.tsUs = ts;
  f.audio.assign(1600, value);
  f.channels = 2;
  f.sampleRate = 48000;
  return f;
}
}  // namespace

TEST_CASE("AudioFollow: 100 % passes samples") {
  br::AudioFollow af;
  const auto f = audioFrame(1, 0.5f);
  const auto out = af.forTick(&f, 1.0, false, 800);
  REQUIRE(out.size() == 1600);
  for (float s : out) CHECK(s == doctest::Approx(0.5));
}

TEST_CASE("AudioFollow: slow motion is silent") {
  br::AudioFollow af;
  const auto f = audioFrame(1, 0.5f);
  const auto out = af.forTick(&f, 0.5, false, 800);
  REQUIRE(out.size() == 1600);
  for (float s : out) CHECK(s == doctest::Approx(0.0));
}

TEST_CASE("AudioFollow: an angle change crossfades over 10 ms") {
  br::AudioFollow af;
  const auto a = audioFrame(1, 0.5f);
  (void)af.forTick(&a, 1.0, false, 800);
  const auto b = audioFrame(2, 0.5f);
  const auto out = af.forTick(&b, 1.0, true, 800);
  REQUIRE(out.size() == 1600);
  CHECK(std::abs(out[0]) < 0.01f);
  CHECK(out[479] == doctest::Approx(0.5).epsilon(0.01));
  CHECK(out[480] == doctest::Approx(0.5));
}