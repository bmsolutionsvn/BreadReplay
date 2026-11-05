#include <doctest/doctest.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <thread>

#include "store/cleanup.h"
#include "store/cleanup_runner.h"
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

void waitDone(br::CleanupRunner& runner) {
  for (int i = 0; i < 500; ++i) {
    if (runner.status().state != "running") return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
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

TEST_CASE("Cleanup: a rewritten segment keeps exactly the kept frames, byte for byte") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  record(store, base, 600);

  const auto before = store.find(base + 20 * kMs);
  REQUIRE(before.has_value());
  const auto video = store.readVideo(*before, false);

  const std::vector<br::ProtectedRange> kept{{base + 17 * kMs, base + 33 * kMs}};
  const auto plans = br::planCleanup({{"cam1", &store}}, kept, base + 1000 * kMs);
  REQUIRE(plans.size() == 1);
  REQUIRE(plans[0].action == br::CleanupAction::Rewrite);

  const auto newBase = br::rewriteSegment(plans[0].base, plans[0].keep);
  REQUIRE(store.replaceSegment(plans[0].base, newBase));

  const auto after = store.find(base + 20 * kMs);
  REQUIRE(after.has_value());
  CHECK(store.readVideo(*after, false) == video);
  CHECK(store.isGapAt(base + 40 * kMs));

  bool splitAt33 = false;
  for (const auto& [first, last] : store.coverage()) splitAt33 = splitAt33 || last == base + 33 * kMs;
  CHECK(splitAt33);
}

TEST_CASE("Cleanup: a store left mid-swap recovers on load") {
  bc::test::TempDir dir;
  const int64_t base = noon();
  std::filesystem::path segmentBase;
  {
    br::InputStore store(dir.path, proto());
    record(store, base, 20);
    const auto segments = store.segments();
    REQUIRE(segments.size() >= 2);
    segmentBase = segments[0].base;
  }
  std::filesystem::rename(segmentBase.string() + ".vmxs", segmentBase.string() + ".old.vmxs");
  std::filesystem::rename(segmentBase.string() + ".vmxi", segmentBase.string() + ".old.vmxi");
  const auto stray = dir.path / "stray";
  std::ofstream(stray.string() + ".cleanup.vmxs");
  std::ofstream(stray.string() + ".cleanup.vmxi");

  br::InputStore reopened(dir.path, proto());
  bool restored = false;
  for (const auto& segment : reopened.segments()) restored = restored || segment.base == segmentBase;
  CHECK(restored);
  CHECK_FALSE(std::filesystem::exists(segmentBase.string() + ".old.vmxs"));
  CHECK_FALSE(std::filesystem::exists(segmentBase.string() + ".old.vmxi"));
  CHECK_FALSE(std::filesystem::exists(stray.string() + ".cleanup.vmxs"));
  CHECK_FALSE(std::filesystem::exists(stray.string() + ".cleanup.vmxi"));
}

TEST_CASE("Cleanup: a store left after the third swap rename keeps the original") {
  bc::test::TempDir dir;
  const int64_t base = noon();
  std::filesystem::path segmentBase;
  std::filesystem::path newBase;
  size_t n = 0;
  {
    br::InputStore store(dir.path, proto());
    record(store, base, 20);
    segmentBase = store.segments()[0].base;
    n = recordsOf(segmentBase).size();
    newBase = br::rewriteSegment(segmentBase, std::vector<br::ProtectedRange>{{base, base + 500'000}});
  }
  std::filesystem::rename(segmentBase.string() + ".vmxs", segmentBase.string() + ".old.vmxs");
  std::filesystem::rename(segmentBase.string() + ".vmxi", segmentBase.string() + ".old.vmxi");
  std::filesystem::rename(newBase.string() + ".vmxs", segmentBase.string() + ".vmxs");

  br::InputStore reopened(dir.path, proto());
  CHECK(recordsOf(segmentBase).size() == n);
  CHECK_FALSE(std::filesystem::exists(segmentBase.string() + ".old.vmxs"));
  CHECK_FALSE(std::filesystem::exists(segmentBase.string() + ".old.vmxi"));
  CHECK_FALSE(std::filesystem::exists(newBase.string() + ".vmxs"));
  CHECK_FALSE(std::filesystem::exists(newBase.string() + ".vmxi"));
}

TEST_CASE("Cleanup: a swap that cannot finish leaves the original readable") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  record(store, base, 20);
  const auto segmentBase = store.segments()[0].base;
  const size_t n = recordsOf(segmentBase).size();
  const auto newBase = br::rewriteSegment(segmentBase, std::vector<br::ProtectedRange>{{base, base + 500'000}});

  const auto heldPath = std::filesystem::path(newBase.string() + ".vmxi");
  HANDLE held = CreateFileW(heldPath.wstring().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  REQUIRE(held != INVALID_HANDLE_VALUE);
  CHECK_FALSE(store.replaceSegment(segmentBase, newBase));

  const auto ref = store.find(base + 1'000'000);
  REQUIRE(ref.has_value());
  CHECK_FALSE(store.readVideo(*ref, false).empty());
  CHECK(recordsOf(segmentBase).size() == n);
  CHECK_FALSE(std::filesystem::exists(segmentBase.string() + ".old.vmxs"));
  CHECK_FALSE(std::filesystem::exists(segmentBase.string() + ".old.vmxi"));

  CloseHandle(held);
  CHECK_FALSE(std::filesystem::exists(newBase.string() + ".vmxs"));
}
TEST_CASE("Cleanup: the runner frees what the plan said") {
  bc::test::TempDir unusedDir;
  bc::test::TempDir aroundDir;
  br::InputStore unused(unusedDir.path, proto());
  br::InputStore around(aroundDir.path, proto());
  const int64_t base = noon();
  record(unused, base + 600 * kMs, 600);  // far outside the replay, so nothing there is kept
  record(around, base, 600);
  std::vector<br::Event> events(1);
  events[0].inUs = base + 17 * kMs;
  events[0].outUs = base + 33 * kMs;
  const auto keep = br::keptRanges(events, br::kCleanupMarginUs);
  const std::map<std::string, const br::InputStore*> map{{"cam1", &unused}, {"cam2", &around}};
  const auto newest = [&] { return std::max(unused.newestTs(), around.newestTs()); };
  const auto plans = br::planCleanup(map, keep, newest());

  br::CleanupRunner runner({{"cam1", &unused}, {"cam2", &around}}, [&] { return keep; }, newest, 0);
  REQUIRE(runner.start());
  waitDone(runner);
  const auto status = runner.status();
  CHECK(status.state == "done");
  CHECK(status.bytesFreed == br::totalFreed(plans));
  CHECK(unused.segments().size() == 1);  // only the open segment is left
  CHECK(around.find(base + 20 * kMs).has_value());
}

TEST_CASE("Cleanup: a replay made during the run is kept") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  record(store, base, 600);
  std::atomic<int> calls{0};
  const auto kept = [&] {
    if (calls++ == 0) return std::vector<br::ProtectedRange>{};
    return std::vector<br::ProtectedRange>{{base + 20 * kMs, base + 25 * kMs}};
  };
  br::CleanupRunner runner({{"cam1", &store}}, kept, [&] { return store.newestTs(); }, 0);
  REQUIRE(runner.start());
  waitDone(runner);
  CHECK(runner.status().state == "done");
  const auto ref = store.find(base + 22 * kMs);
  REQUIRE(ref.has_value());
  CHECK_FALSE(store.readVideo(*ref, false).empty());
}

TEST_CASE("Cleanup: a segment removed meanwhile is skipped") {
  bc::test::TempDir dir;
  br::InputStore store(dir.path, proto());
  const int64_t base = noon();
  record(store, base, 600);
  const auto planned = store.segments().front().base;
  std::atomic<int> calls{0};
  const auto kept = [&] {
    if (calls++ == 1) {
      std::error_code ec;
      std::filesystem::remove(planned.string() + ".vmxs", ec);
      std::filesystem::remove(planned.string() + ".vmxi", ec);
    }
    return std::vector<br::ProtectedRange>{};
  };
  br::CleanupRunner runner({{"cam1", &store}}, kept, [&] { return store.newestTs(); }, 0);
  REQUIRE(runner.start());
  waitDone(runner);
  const auto status = runner.status();
  CHECK(status.state == "done");
  CHECK(status.error.empty());
}