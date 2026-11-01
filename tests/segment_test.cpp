#include <doctest/doctest.h>

#include <numeric>
#include <thread>

#include "store/segment.h"
#include "support/temp_dir.h"

namespace {
br::SegmentHeader headerFor(int width = 320, int height = 180) {
  br::SegmentHeader header;
  header.inputId = "cam1";
  header.width = static_cast<uint32_t>(width);
  header.height = static_cast<uint32_t>(height);
  header.rate = bc::Rate{60000, 1001};
  return header;
}

std::vector<uint8_t> makeVideo(int i, size_t n = 64) {
  std::vector<uint8_t> v(n);
  for (size_t k = 0; k < n; ++k) v[k] = static_cast<uint8_t>((i * 7 + k) & 0xFF);
  return v;
}

std::vector<float> makeAudio(int i, size_t frames = 8) {
  std::vector<float> a(frames * 2);
  for (size_t k = 0; k < a.size(); ++k) a[k] = static_cast<float>(i) + static_cast<float>(k) * 0.01f;
  return a;
}
}  // namespace

TEST_CASE("Segment: frames read back in order by timestamp") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  {
    br::SegmentWriter w(base, headerFor());
    for (int i = 0; i < 100; ++i) {
      const auto video = makeVideo(i);
      w.append(1'000'000 + i * 16683, video, static_cast<uint32_t>(video.size() / 2), makeAudio(i));
    }
    w.close();
  }
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  REQUIRE(r->records().size() == 100);
  CHECK(r->header().inputId == "cam1");
  CHECK(r->header().width == 320);
  for (int i = 0; i < 100; ++i) {
    CHECK(r->records()[static_cast<size_t>(i)].tsUs == 1'000'000 + i * 16683);
    const auto video = r->readVideo(static_cast<size_t>(i), false);
    CHECK(video == makeVideo(i));
  }
}

TEST_CASE("Segment: a reader sees frames flushed while the writer is open") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  br::SegmentWriter w(base, headerFor());
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  for (int i = 0; i < 60; ++i) {
    w.append(i * 16683, makeVideo(i), 8, makeAudio(i));
    if (i == 29) {
      std::this_thread::sleep_for(std::chrono::milliseconds(600));
      r->refresh();
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  r->refresh();
  CHECK(r->records().size() >= 30);
  w.close();
  r->refresh();
  CHECK(r->records().size() == 60);
}

TEST_CASE("Segment: previewOnly reads exactly previewLen bytes") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  {
    br::SegmentWriter w(base, headerFor());
    const auto video = makeVideo(3, 100);
    w.append(5'000'000, video, 40, makeAudio(3));
    w.close();
  }
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  const auto expected = makeVideo(3, 100);
  const auto preview = r->readVideo(0, true);
  CHECK(preview.size() == 40);
  CHECK(preview == std::vector<uint8_t>(expected.begin(), expected.begin() + 40));
}

TEST_CASE("Segment: indexAtOrBefore") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  {
    br::SegmentWriter w(base, headerFor());
    for (int i = 0; i < 5; ++i) w.append(10'000'000 + i * 1000, makeVideo(i), 8, makeAudio(i));
    w.close();
  }
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  CHECK_FALSE(r->indexAtOrBefore(9'999'999).has_value());
  CHECK(r->indexAtOrBefore(10'000'000) == 0);
  CHECK(r->indexAtOrBefore(10'002'500) == 2);
  CHECK(r->indexAtOrBefore(10'100'000) == 4);
}

TEST_CASE("Segment: recovery drops a record whose bytes are missing") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  {
    br::SegmentWriter w(base, headerFor());
    for (int i = 0; i < 10; ++i) w.append(i * 16683, makeVideo(i, 100), 20, makeAudio(i));
    w.close();
  }
  // Cut the essence mid-way through the tenth frame's video.
  const auto essence = std::filesystem::path(base.string() + ".vmxs");
  const auto size = std::filesystem::file_size(essence);
  std::filesystem::resize_file(essence, size - 10);
  br::recoverSegment(base);
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  CHECK(r->records().size() == 9);
}

TEST_CASE("Segment: audio stays with its frame") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  {
    br::SegmentWriter w(base, headerFor());
    for (int i = 0; i < 10; ++i) w.append(i * 16683, makeVideo(i), 8, makeAudio(i));
    w.close();
  }
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  CHECK(r->readAudio(5) == makeAudio(5));
}