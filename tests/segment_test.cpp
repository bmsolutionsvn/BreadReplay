#include <doctest/doctest.h>

#include <cstring>
#include <fstream>
#include <numeric>
#include <thread>

#include "store/input_store.h"
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

void put16(std::vector<uint8_t>& o, uint16_t v) {
  o.insert(o.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + 2);
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
  o.insert(o.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + 4);
}
void put64(std::vector<uint8_t>& o, int64_t v) {
  o.insert(o.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + 8);
}

// Byte fixture in the v1 layout: version 1, no codec field, reserved = 0.
std::filesystem::path writeV1Segment(const std::filesystem::path& base, int frames) {
  std::vector<uint8_t> idx;
  put32(idx, 1);
  put32(idx, 4);
  idx.insert(idx.end(), {'c', 'a', 'm', '1'});
  put32(idx, 320);
  put32(idx, 180);
  put32(idx, 60);
  put32(idx, 1);
  put16(idx, 2);
  put32(idx, 48000);

  std::vector<uint8_t> ess;
  for (int i = 0; i < frames; ++i) {
    const int64_t ts = 1'000'000 + i * 16683;
    const auto video = makeVideo(i);
    const int64_t offset = static_cast<int64_t>(ess.size());
    put64(idx, ts);
    put64(idx, offset);
    put32(idx, static_cast<uint32_t>(video.size()));
    put32(idx, 8);
    put32(idx, 0);
    put32(idx, 0);  // reserved

    ess.insert(ess.end(), {'B', 'R', 'F', '1'});
    put64(ess, ts);
    put32(ess, static_cast<uint32_t>(video.size()));
    put32(ess, 0);
    put32(ess, 8);
    ess.insert(ess.end(), video.begin(), video.end());
  }
  std::ofstream(base.string() + ".vmxi", std::ios::binary).write(reinterpret_cast<const char*>(idx.data()), static_cast<std::streamsize>(idx.size()));
  std::ofstream(base.string() + ".vmxs", std::ios::binary).write(reinterpret_cast<const char*>(ess.data()), static_cast<std::streamsize>(ess.size()));
  return base;
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

TEST_CASE("Segment: v2 HEVC round-trips codec, keyframe flags and bytes") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  auto h = headerFor();
  h.codec = br::StoredCodec::Hevc;
  {
    br::SegmentWriter w(base, h);
    for (int i = 0; i < 5; ++i) {
      w.append(2'000'000 + i * 16683, makeVideo(i), 0, makeAudio(i), i == 0 || i == 3);
    }
    w.close();
  }
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  CHECK(r->header().version == 2);
  CHECK(r->header().codec == br::StoredCodec::Hevc);
  REQUIRE(r->records().size() == 5);
  for (int i = 0; i < 5; ++i) {
    CHECK(r->records()[static_cast<size_t>(i)].flags == ((i == 0 || i == 3) ? 1u : 0u));
    CHECK(r->readVideo(static_cast<size_t>(i), false) == makeVideo(i));
  }
}

TEST_CASE("Segment: a v1 segment reads as VMX with every frame a keyframe") {
  bc::test::TempDir dir;
  const auto base = writeV1Segment(dir.path / "v1", 3);
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  CHECK(r->header().version == 1);
  CHECK(r->header().codec == br::StoredCodec::Vmx);
  REQUIRE(r->records().size() == 3);
  for (int i = 0; i < 3; ++i) {
    CHECK(r->records()[static_cast<size_t>(i)].flags == 0u);
    CHECK(r->readVideo(static_cast<size_t>(i), false) == makeVideo(i));
  }

  br::SegmentHeader proto;
  proto.inputId = "cam1";
  proto.width = 320;
  proto.height = 180;
  proto.rate = bc::Rate{60, 1};
  br::InputStore store(dir.path, proto);
  REQUIRE(store.frameCount() == 3);
  auto ref = store.find(1'000'000 + 16'683);
  REQUIRE(ref.has_value());
  CHECK(store.codecOf(*ref) == br::StoredCodec::Vmx);
  CHECK(store.isKeyframe(*ref));
}

TEST_CASE("Segment: v2 recovery drops a record whose bytes are missing") {
  bc::test::TempDir dir;
  const auto base = dir.path / "seg";
  auto h = headerFor();
  h.codec = br::StoredCodec::H264;
  {
    br::SegmentWriter w(base, h);
    for (int i = 0; i < 10; ++i) w.append(i * 16683, makeVideo(i, 100), 0, makeAudio(i), i == 0);
    w.close();
  }
  const auto essence = std::filesystem::path(base.string() + ".vmxs");
  std::filesystem::resize_file(essence, std::filesystem::file_size(essence) - 10);
  br::recoverSegment(base);
  auto r = br::SegmentReader::open(base);
  REQUIRE(r.has_value());
  r->refresh();
  REQUIRE(r->records().size() == 9);
  CHECK(r->records()[0].flags == 1u);
  CHECK(r->records()[8].flags == 0u);
  CHECK(r->readVideo(8, false) == makeVideo(8, 100));
}