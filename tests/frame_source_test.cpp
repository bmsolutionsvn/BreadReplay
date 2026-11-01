#include <doctest/doctest.h>

#include <memory>

#include "core/vmx_codec.h"
#include "play/frame_source.h"
#include "support/temp_dir.h"

namespace {
constexpr int64_t kStep = 16683;
constexpr int64_t kBase = 100'000'000;

struct Store {
  bc::test::TempDir dir;
  std::unique_ptr<br::InputStore> store;

  Store(uint32_t w, uint32_t h, const std::vector<int64_t>& times) {
    br::SegmentHeader header;
    header.inputId = "cam1";
    header.width = w;
    header.height = h;
    header.rate = bc::Rate{60000, 1001};
    store = std::make_unique<br::InputStore>(dir.path, header);
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 2);
    for (size_t i = 0; i < frame.size(); i += 2) {
      frame[i] = 128;
      frame[i + 1] = static_cast<uint8_t>(40 + (i / 2) % 100);
    }
    bc::CpuVideoFrame f;
    f.data = frame.data();
    f.stride = static_cast<int>(w * 2);
    f.width = w;
    f.height = h;
    f.format = bc::CpuPixel::UYVY;
    br::VmxEncoder enc(w, h, br::VmxQuality::SQ);
    for (int64_t t : times) {
      const auto e = enc.encode(f);
      store->append(t, e.bytes, e.previewLen, std::vector<float>{0.1f, 0.2f});
    }
  }

  std::map<std::string, const br::InputStore*> map() const { return {{"cam1", store.get()}}; }
};
}  // namespace

TEST_CASE("FrameSource: get returns the frame at or before") {
  Store s(320, 180, {kBase, kBase + kStep, kBase + 2 * kStep});
  br::FrameSource src(s.map());
  const auto f = src.get("cam1", kBase + kStep + 500);
  REQUIRE(f);
  CHECK(f->tsUs == kBase + kStep);
  CHECK(f->uyvy.size() == 320u * 180u * 2u);
  CHECK(f->w == 320);
  CHECK_FALSE(src.get("nope", kBase));
}

TEST_CASE("FrameSource: a cached frame is not read twice") {
  Store s(320, 180, {kBase, kBase + kStep});
  br::FrameSource src(s.map());
  REQUIRE(src.get("cam1", kBase));
  const auto before = src.diskReads();
  REQUIRE(src.get("cam1", kBase + 10));
  CHECK(src.diskReads() == before);
  CHECK(before == 1);
}

TEST_CASE("FrameSource: a gap returns the last frame before it, flagged") {
  Store s(320, 180, {kBase, kBase + kStep, kBase + 180 * kStep, kBase + 181 * kStep});
  br::FrameSource src(s.map());
  const auto f = src.get("cam1", kBase + 90 * kStep);
  REQUIRE(f);
  CHECK(f->tsUs == kBase + kStep);
  CHECK(f->inGap);
  const auto live = src.get("cam1", kBase + 181 * kStep);
  REQUIRE(live);
  CHECK_FALSE(live->inGap);
}

TEST_CASE("FrameSource: preview is 240x135 and reads only the prefix") {
  Store s(1920, 1080, {kBase});
  br::FrameSource src(s.map());
  const auto p = src.preview("cam1", kBase);
  REQUIRE(p);
  CHECK(p->w == 240);
  CHECK(p->h == 135);
  CHECK(p->bgra.size() == 240u * 135u * 4u);
  CHECK(src.diskReads() == 1);
}

TEST_CASE("FrameSource: prefetch makes the next get a cache hit") {
  Store s(320, 180, {kBase, kBase + kStep, kBase + 2 * kStep, kBase + 3 * kStep});
  br::FrameSource src(s.map());
  src.prefetch("cam1", kBase, kStep, 4);
  const auto before = src.diskReads();
  CHECK(before == 4);
  REQUIRE(src.get("cam1", kBase + 3 * kStep));
  CHECK(src.diskReads() == before);
}