#include <doctest/doctest.h>

#include <ctime>
#include <string>

#include "core/timecode.h"

namespace {
int64_t localAt(int hour, int minute, int second, int64_t us) {
  std::tm tm{};
  tm.tm_year = 2026 - 1900;
  tm.tm_mon = 0;
  tm.tm_mday = 15;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  tm.tm_isdst = -1;
  const std::time_t t = std::mktime(&tm);
  return static_cast<int64_t>(t) * 1'000'000 + us;
}
}  // namespace

TEST_CASE("Timecode: frames count at the nominal rate") {
  const int64_t t = localAt(1, 0, 0, 500'000);
  CHECK(br::formatTimecode(t, bc::Rate{50, 1}) == "01:00:00:25");
  CHECK(br::formatTimecode(t, bc::Rate{60000, 1001}) == "01:00:00:30");
}

TEST_CASE("Timecode: nowUs never goes backwards") {
  int64_t last = br::nowUs();
  for (int i = 0; i < 10000; ++i) {
    const int64_t now = br::nowUs();
    REQUIRE(now >= last);
    last = now;
  }
}