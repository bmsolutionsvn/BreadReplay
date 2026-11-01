#include <doctest/doctest.h>

#include <ctime>

#include "store/input_store.h"
#include "support/temp_dir.h"

namespace {
br::SegmentHeader proto() {
  br::SegmentHeader h;
  h.inputId = "cam1";
  h.width = 320;
  h.height = 180;
  h.rate = bc::Rate{30, 1};
  return h;
}

std::vector<uint8_t> video(int i) {
  std::vector<uint8_t> v(32, static_cast<uint8_t>(i & 0xFF));
  return v;
}

std::vector<float> audio(int i) { return std::vector<float>{static_cast<float>(i), 0.5f}; }

// 2026-01-15 12:00:00.000 local, in epoch microseconds.
int64_t noon() {
  std::tm tm{};
  tm.tm_year = 2026 - 1900;
  tm.tm_mon = 0;
  tm.tm_mday = 15;
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&tm)) * 1'000'000;
}
}  // namespace

TEST_CASE("InputStore: rolls a new segment each minute") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  store.append(base + 59'500'000, video(0), 4, audio(0));
  store.append(base + 59'900'000, video(1), 4, audio(1));
  store.append(base + 60'100'000, video(2), 4, audio(2));
  store.append(base + 60'500'000, video(3), 4, audio(3));
  CHECK(store.segments().size() == 2);
}

TEST_CASE("InputStore: find works across a minute boundary") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  store.append(base + 59'900'000, video(0), 4, audio(0));
  store.append(base + 60'100'000, video(1), 4, audio(1));

  auto a = store.find(base + 59'950'000);
  REQUIRE(a.has_value());
  CHECK(a->segment == 0);
  CHECK(a->index == 0);

  auto b = store.find(base + 60'200'000);
  REQUIRE(b.has_value());
  CHECK(b->segment == 1);
  CHECK(b->index == 0);
  CHECK(store.readVideo(*b, false) == video(1));
}

TEST_CASE("InputStore: a gap splits coverage") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  for (int i = 0; i <= 30; ++i) store.append(base + i * 33'333, video(i), 4, audio(i));
  for (int i = 90; i <= 120; ++i) store.append(base + i * 33'333, video(i), 4, audio(i));
  const auto cov = store.coverage();
  REQUIRE(cov.size() == 2);
  CHECK(cov[0].first == base);
  CHECK(cov[1].second == base + 120 * 33'333);
  CHECK(store.isGapAt(base + 60 * 33'333));
  CHECK_FALSE(store.isGapAt(base + 10 * 33'333));
}

TEST_CASE("InputStore: reopening loads existing segments") {
  bc::test::TempDir dir;
  const int64_t base = noon();
  {
    br::InputStore store(dir.path, proto());
    for (int i = 0; i < 10; ++i) store.append(base + i * 33'333, video(i), 4, audio(i));
  }
  br::InputStore reopened(dir.path, proto());
  CHECK(reopened.segments().size() == 1);
  CHECK(reopened.newestTs() == base + 9 * 33'333);
  // Recording continues into the same minute's segment.
  reopened.append(base + 10 * 33'333, video(10), 4, audio(10));
  CHECK(reopened.newestTs() == base + 10 * 33'333);
  CHECK(reopened.segments().size() == 1);
  auto direct = br::SegmentReader::open(reopened.segments()[0].base);
  REQUIRE(direct.has_value());
  direct->refresh();
  CHECK(direct->records().size() == 11);
  CHECK(direct->records()[10].videoLen == 32);
  CHECK(direct->records()[10].audioLen == 8);
  CHECK(direct->records()[10].offset == 640);
  const auto essence = std::filesystem::path(reopened.segments()[0].base.string() + ".vmxs");
  CHECK(std::filesystem::file_size(essence) == 704);
  CHECK(direct->readVideo(10, false) == video(10));
  const auto ref = reopened.find(base + 10 * 33'333);
  REQUIRE(ref.has_value());
  CHECK(ref->index == 10);
  CHECK(reopened.readVideo(*ref, false) == video(10));
}

TEST_CASE("InputStore: removeSegment refuses the open segment") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  store.append(base + 59'000'000, video(0), 4, audio(0));
  store.append(base + 60'100'000, video(1), 4, audio(1));
  REQUIRE(store.segments().size() == 2);
  CHECK_FALSE(store.removeSegment(store.segments()[1].base));  // still being written
  CHECK(store.removeSegment(store.segments()[0].base));
  CHECK(store.segments().size() == 1);
}