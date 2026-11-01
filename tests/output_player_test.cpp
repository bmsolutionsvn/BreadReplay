#include <doctest/doctest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "play/output_player.h"
#include "support/fake_output.h"
#include "support/temp_dir.h"

namespace {
constexpr int64_t kS = 1'000'000;
constexpr int64_t kBase = 500'000'000;

struct Fixture {
  bc::test::TempDir dir;
  std::unique_ptr<br::InputStore> store;
  std::unique_ptr<bc::GpuDevice> gpu = bc::GpuDevice::create(bc::GpuDevice::Pref::HardwareOrWarp);
  bc::TexturePool pool{*gpu};
  bc::Converter conv{*gpu, pool};
  std::unique_ptr<br::FrameSource> src;
  std::unique_ptr<br::Renderer> renderer;
  std::unique_ptr<br::Player> player;
  br::ShowSettings settings;
  br::EventStore events;

  Fixture() {
    br::SegmentHeader h;
    h.inputId = "cam1";
    h.width = 640;
    h.height = 360;
    h.rate = bc::Rate{60, 1};
    store = std::make_unique<br::InputStore>(dir.path, h);

    std::vector<uint8_t> uyvy(static_cast<size_t>(640) * 360 * 2, 128);
    bc::CpuVideoFrame f;
    f.data = uyvy.data();
    f.stride = 640 * 2;
    f.width = 640;
    f.height = 360;
    f.format = bc::CpuPixel::UYVY;
    br::VmxEncoder enc(640, 360, br::VmxQuality::SQ);
    const auto e = enc.encode(f);
    for (int i = 0; i < 400; ++i) {
      store->append(kBase + static_cast<int64_t>(i) * 16683, e.bytes, e.previewLen,
                    std::vector<float>(1600, 0.25f));
    }
    src = std::make_unique<br::FrameSource>(std::map<std::string, const br::InputStore*>{{"cam1", store.get()}});
    renderer = std::make_unique<br::Renderer>(*gpu, pool, conv, 640, 360);
    player = std::make_unique<br::Player>(events, settings.rate, true);
    settings.transition = "cut";
  }

  int makeEvent(int64_t in, int64_t out) {
    events.markIn(in, "cam1");
    const int id = events.events().back().id;
    events.markOut(out, "cam1", 1 * kS);
    events.find(id)->speeds = {{in, 1.0}};
    return id;
  }

  bool waitDelivered(bc::test::FakeOutput& out, int n) {
    for (int i = 0; i < 200; ++i) {
      if (out.delivered.load() >= n) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return out.delivered.load() >= n;
  }
};
}  // namespace

TEST_CASE("OutputPlayer: a 2 s event at 100 % submits 120 frames at 60 fps then holds") {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  const int id = f.makeEvent(kBase, kBase + 2 * kS);
  auto out = std::make_unique<bc::test::FakeOutput>(true);
  auto* raw = out.get();
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs([&] {
    std::vector<std::unique_ptr<bc::Output>> v;
    v.push_back(std::move(out));
    return v;
  }());
  f.player->cue(id);
  f.player->play();
  for (int i = 0; i < 150; ++i) op.tickOnce();
  REQUIRE(f.waitDelivered(*raw, 150));
  const auto seen = raw->seen();
  REQUIRE(seen.size() == 150);
  const auto last = seen.back().firstPixel;
  for (size_t i = seen.size() - 30; i < seen.size(); ++i) CHECK(seen[i].firstPixel == last);
  // The first 120 ticks moved (the event is 2 s at 60 fps).
  CHECK(seen[0].firstPixel == seen[119].firstPixel);  // the source picture is uniform
  CHECK(seen[1].tick == seen[0].tick + 1);
  op.stop();
}

TEST_CASE("OutputPlayer: highlight plays the list in order") {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  f.makeEvent(kBase, kBase + 500'000);
  f.makeEvent(kBase + 600'000, kBase + 1'100'000);
  f.makeEvent(kBase + 1'200'000, kBase + 1'700'000);
  auto out = std::make_unique<bc::test::FakeOutput>(false);
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs([&] {
    std::vector<std::unique_ptr<bc::Output>> v;
    v.push_back(std::move(out));
    return v;
  }());
  op.playHighlight(f.events.currentList());
  std::vector<int> order;
  for (int i = 0; i < 200; ++i) {
    op.tickOnce();
    if (const auto id = op.currentEvent()) {
      if (order.empty() || order.back() != *id) order.push_back(*id);
    }
  }
  CHECK(order == std::vector<int>{1, 2, 3});
  op.stop();
}

TEST_CASE("OutputPlayer: a slow output never slows ticks") {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  const int id = f.makeEvent(kBase, kBase + 2 * kS);
  auto out = std::make_unique<bc::test::FakeOutput>(true);
  out->delayMs = 100;
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs([&] {
    std::vector<std::unique_ptr<bc::Output>> v;
    v.push_back(std::move(out));
    return v;
  }());
  f.player->cue(id);
  f.player->play();
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 60; ++i) op.tickOnce();
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1500));
  op.stop();
}