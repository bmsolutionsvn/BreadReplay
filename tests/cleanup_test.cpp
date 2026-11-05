#include <doctest/doctest.h>

#include <algorithm>
#include <ctime>

#include "store/cleanup.h"
#include "support/temp_dir.h"

namespace {
constexpr int64_t kMs = 1'000'000;

int64_t noon() {
  std::tm tm{};
  tm.tm_year = 2026 - 1900;
  tm.tm_mon = 0;
  tm.tm_mday = 15;
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&tm)) * 1'000'000;
}

br::SegmentHeader proto(br::StoredCodec codec = br::StoredCodec::Vmx) {
  br::SegmentHeader h;
  h.inputId = "cam1";
  h.width = 320;
  h.height = 180;
  h.rate = bc::Rate{10, 1};
  h.codec = codec;
  return h;
}

std::vector<uint8_t> video(int i) { return std::vector<uint8_t>(32, static_cast<uint8_t>(i & 0xFF)); }
std::vector<float> audio(int i) { return std::vector<float>{static_cast<float>(i), 0.5f}; }

// 10 fps from base for count frames, then one frame next minute so the first segment closes.
void record(br::InputStore& store, int64_t base, int count, bool h264Keyframes = false) {
  for (int i = 0; i < count; ++i) {
    store.append(base + i * 100'000, video(i), 4, audio(i), !h264Keyframes || i % 20 == 0);
  }
  store.append(base + 60'500'000, video(999), 4, audio(0));
}

std::vector<br::FrameRecord> recordsOf(const std::filesystem::path& base) {
  auto reader = br::SegmentReader::open(base);
  REQUIRE(reader.has_value());
  reader->refresh();
  return std::vector<br::FrameRecord>(reader->records().begin(), reader->records().end());
}
}  // namespace

TEST_CASE("Cleanup: kept ranges add the margin and merge") {
  std::vector<br::Event> events(4);
  events[0].inUs = 10 * kMs;
  events[0].outUs = 12 * kMs;
  events[1].inUs = 14 * kMs;
  events[1].outUs = 15 * kMs;
  events[2].inUs = 40 * kMs;
  events[2].outUs = 40 * kMs;
  events[3].inUs = 52 * kMs;
  events[3].outUs = 50 * kMs;

  const auto ranges = br::keptRanges(events, br::kCleanupMarginUs);
  REQUIRE(ranges.size() == 3);
  CHECK(ranges[0].inUs == 7 * kMs);
  CHECK(ranges[0].outUs == 18 * kMs);
  CHECK(ranges[1].inUs == 37 * kMs);
  CHECK(ranges[1].outUs == 43 * kMs);
  CHECK(ranges[2].inUs == 47 * kMs);
  CHECK(ranges[2].outUs == 55 * kMs);
}

TEST_CASE("Cleanup: a VMX segment around one replay is rewritten to the kept range") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  record(store, base, 600);

  const std::vector<br::ProtectedRange> kept{{base + 17 * kMs, base + 33 * kMs}};
  const auto plans = br::planCleanup({{"cam1", &store}}, kept, base + 1000 * kMs);
  REQUIRE(plans.size() == 1);
  CHECK(plans[0].action == br::CleanupAction::Rewrite);
  REQUIRE(plans[0].keep.size() == 1);
  CHECK(plans[0].keep[0].inUs == base + 17 * kMs);
  CHECK(plans[0].keep[0].outUs == base + 33 * kMs);

  int64_t expected = 0;
  for (const auto& rec : recordsOf(plans[0].base)) {
    if (rec.tsUs < base + 17 * kMs || rec.tsUs > base + 33 * kMs) expected += rec.videoLen + rec.audioLen + 24 + 40;
  }
  CHECK(plans[0].bytesFreed == expected);
}

TEST_CASE("Cleanup: an H.264 segment keeps from the keyframe before") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto(br::StoredCodec::H264));
  const int64_t base = noon();
  record(store, base, 600, true);

  const std::vector<br::ProtectedRange> kept{{base + 17 * kMs, base + 33 * kMs}};
  const auto plans = br::planCleanup({{"cam1", &store}}, kept, base + 1000 * kMs);
  REQUIRE(plans.size() == 1);
  CHECK(plans[0].action == br::CleanupAction::Rewrite);
  REQUIRE(plans[0].keep.size() == 1);
  CHECK(plans[0].keep[0].inUs == base + 16 * kMs);
}

TEST_CASE("Cleanup: untouched, open and recent segments are not planned; unused ones are deleted") {
  bc::test::TempDir dir;
  bc::test::TempDir other;
  br::InputStore unused(dir.path, proto());
  br::InputStore recent(other.path, proto());
  const int64_t base = noon();
  record(unused, base, 400);
  record(recent, base, 599);

  const std::vector<br::ProtectedRange> kept;
  const auto plans = br::planCleanup({{"cam1", &unused}, {"cam2", &recent}}, kept, base + 50 * kMs);
  REQUIRE(plans.size() == 1);
  CHECK(plans[0].inputId == "cam1");
  CHECK(plans[0].action == br::CleanupAction::Delete);
  CHECK(plans[0].bytesFreed > 0);
}