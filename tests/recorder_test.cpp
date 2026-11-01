#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "record/recorder.h"
#include "support/temp_dir.h"

namespace {
br::SegmentHeader proto() {
  br::SegmentHeader h;
  h.inputId = "cam1";
  h.width = 320;
  h.height = 180;
  h.rate = bc::Rate{60000, 1001};
  return h;
}

int64_t baseTs() {
  std::tm tm{};
  tm.tm_year = 2026 - 1900;
  tm.tm_mon = 0;
  tm.tm_mday = 15;
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&tm)) * 1'000'000;
}

struct Frame {
  std::vector<uint8_t> data;
  bc::CpuVideoFrame frame;
  Frame(uint32_t w, uint32_t h, uint8_t value) {
    data.assign(static_cast<size_t>(w) * h * 2, value);
    frame.data = data.data();
    frame.stride = static_cast<int>(w * 2);
    frame.width = w;
    frame.height = h;
    frame.format = bc::CpuPixel::UYVY;
  }
};

bool waitFor(const std::function<bool()>& pred, int ms) {
  for (int i = 0; i < ms / 5; ++i) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return pred();
}
}  // namespace

TEST_CASE("Recorder: 180 frames at 60 fps all reach the store in order") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  br::Recorder rec(store, br::VmxQuality::SQ, 512, 2);
  const int64_t base = baseTs();
  for (int i = 0; i < 180; ++i) {
    Frame f(320, 180, static_cast<uint8_t>(i));
    rec.pushVideo(f.frame, base + i * 16683);
  }
  REQUIRE(waitFor([&] { return rec.stats().framesWritten == 180; }, 20000));
  rec.stop();
  const auto s = rec.stats();
  CHECK(s.framesIn == 180);
  CHECK(s.dropped == 0);
  for (int i = 1; i < 180; ++i) {
    const auto ref = store.find(base + i * 16683);
    REQUIRE(ref.has_value());
    CHECK(ref->tsUs == base + i * 16683);
  }
}

TEST_CASE("Recorder: a full queue drops and counts, push never blocks") {
  bc::test::TempDir dir;
  br::SegmentHeader p = proto();
  p.width = 1920;
  p.height = 1080;
  br::InputStore store(dir.path, p);
  br::Recorder rec(store, br::VmxQuality::SQ, 2, 1);
  Frame f(1920, 1080, 128);
  const int64_t base = baseTs();
  double slowest = 0;
  for (int i = 0; i < 100; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    rec.pushVideo(f.frame, base + i * 16683);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    slowest = std::max(slowest, ms);
  }
  const auto s = rec.stats();
  CHECK(s.framesIn == 100);
  CHECK(s.dropped > 0);
  CHECK(slowest < 10.0);
  rec.stop();
}

TEST_CASE("Recorder: audio stays with its frame") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  br::Recorder rec(store, br::VmxQuality::SQ, 64, 1);
  const int64_t base = baseTs();
  std::vector<float> block{0.25f, -0.5f, 0.75f, -1.0f};
  for (int i = 0; i < 10; ++i) {
    rec.pushAudio(block, 2, 48000);
    Frame f(320, 180, static_cast<uint8_t>(i));
    rec.pushVideo(f.frame, base + i * 16683);
  }
  REQUIRE(waitFor([&] { return rec.stats().framesWritten == 10; }, 5000));
  rec.stop();
  const auto ref = store.find(base + 5 * 16683);
  REQUIRE(ref.has_value());
  CHECK(store.readAudio(*ref) == block);
}

TEST_CASE("Recorder: pushCoded stores the frame with its preview length") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  Frame src(320, 180, 77);
  br::VmxEncoder enc(320, 180, br::VmxQuality::SQ);
  const auto encoded = enc.encode(src.frame);
  br::Recorder rec(store, br::VmxQuality::SQ, 8, 1);
  const int64_t ts = baseTs();
  rec.pushCoded(ts, encoded.bytes);
  REQUIRE(waitFor([&] { return rec.stats().framesWritten == 1; }, 5000));
  rec.stop();
  const auto ref = store.find(ts);
  REQUIRE(ref.has_value());
  CHECK(store.readVideo(*ref, false) == encoded.bytes);
  CHECK(store.readVideo(*ref, true).size() == encoded.previewLen);
}

TEST_CASE("Recorder: a size change starts a new segment") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  br::Recorder rec(store, br::VmxQuality::SQ, 64, 1);
  const int64_t base = baseTs();
  for (int i = 0; i < 20; ++i) {
    Frame f(320, 180, static_cast<uint8_t>(i));
    rec.pushVideo(f.frame, base + i * 16683);
  }
  for (int i = 20; i < 40; ++i) {
    Frame f(640, 360, static_cast<uint8_t>(i));
    rec.pushVideo(f.frame, base + i * 16683);
  }
  REQUIRE(waitFor([&] { return rec.stats().framesWritten == 40; }, 10000));
  rec.stop();
  CHECK(store.segments().size() == 2);
}