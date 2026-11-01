#include <doctest/doctest.h>

#include <fstream>

#include "store/retention.h"
#include "support/temp_dir.h"

namespace {
br::StoredSegment seg(int64_t first, int64_t last, int64_t bytes, bool open = false) {
  br::StoredSegment s;
  s.inputId = "cam1";
  s.info.base = "seg-" + std::to_string(first);
  s.info.firstTs = first;
  s.info.lastTs = last;
  s.info.bytes = bytes;
  s.open = open;
  return s;
}
}  // namespace

TEST_CASE("Retention: oldest goes first") {
  std::vector<br::StoredSegment> all{seg(0, 60'000'000, 100), seg(60'000'000, 120'000'000, 100),
                                    seg(120'000'000, 180'000'000, 100)};
  br::RetentionLimit limit;
  limit.bytes = 150;
  const auto del = br::segmentsToDelete(all, limit, 1'000'000'000, 0, {});
  REQUIRE(del.size() == 2);
  CHECK(del[0].info.firstTs == 0);
  CHECK(del[1].info.firstTs == 60'000'000);
}

TEST_CASE("Retention: protected segments survive") {
  std::vector<br::StoredSegment> all{seg(0, 60'000'000, 100), seg(60'000'000, 120'000'000, 100),
                                    seg(120'000'000, 180'000'000, 100)};
  br::RetentionLimit limit;
  limit.bytes = 200;
  const std::vector<br::ProtectedRange> keep{{30'000'000, 40'000'000}};
  const auto del = br::segmentsToDelete(all, limit, 1'000'000'000, 0, keep);
  REQUIRE(del.size() == 1);
  CHECK(del[0].info.firstTs == 60'000'000);
}

TEST_CASE("Retention: the hours limit deletes beyond it") {
  std::vector<br::StoredSegment> all{seg(0, 3'600'000'000, 10), seg(3'600'000'000, 7'200'000'000, 10),
                                    seg(7'200'000'000, 10'800'000'000, 10)};
  br::RetentionLimit limit;
  limit.hours = 1.0;
  const auto del = br::segmentsToDelete(all, limit, 1'000'000'000, 0, {});
  REQUIRE(del.size() == 2);
  CHECK(del[0].info.firstTs == 0);
  CHECK(del[1].info.firstTs == 3'600'000'000);
}

TEST_CASE("Retention: nothing deletable returns empty") {
  std::vector<br::StoredSegment> all{seg(0, 60'000'000, 100, true), seg(60'000'000, 120'000'000, 100)};
  br::RetentionLimit limit;
  limit.bytes = 1;
  const std::vector<br::ProtectedRange> keep{{0, 120'000'000}};
  CHECK(br::segmentsToDelete(all, limit, 0, 1'000'000'000, keep).empty());
}

TEST_CASE("Retention: secondsLeft") {
  const auto s = br::secondsLeft(10'000'000'000LL, 100.0 * 1000 * 1000);
  REQUIRE(s.has_value());
  CHECK(*s == doctest::Approx(100.0));
  CHECK_FALSE(br::secondsLeft(10'000'000'000LL, 0.0).has_value());
}

TEST_CASE("Retention: the write meter averages the last five seconds") {
  br::WriteRateMeter m;
  for (int i = 0; i <= 50; ++i) m.add(1'000'000, i * 100'000);  // 1 MB every 100 ms
  CHECK(m.bytesPerSec(5'000'000) == doctest::Approx(10'000'000.0).epsilon(0.15));
}

TEST_CASE("Preflight: a temp folder can record") {
  bc::test::TempDir dir;
  const auto p = br::preflight(dir.path, 2, 20.0);
  CHECK(p.canRecord);
  CHECK(p.minutesFree > 0);
  CHECK(p.writeMBps > 0);
  CHECK_FALSE(p.error.has_value());
}

TEST_CASE("Preflight: an unwritable folder is refused with a message") {
  bc::test::TempDir dir;
  const auto file = dir.path / "a-file";
  { std::ofstream f(file); f << "x"; }
  const auto p = br::preflight(file, 2, 20.0);
  CHECK_FALSE(p.canRecord);
  REQUIRE(p.error.has_value());
  CHECK_FALSE(p.error->empty());
}