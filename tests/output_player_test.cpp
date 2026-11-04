#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>

#include "io/omt_output.h"
#include "play/output_player.h"
#include "support/fake_output.h"
#include "support/requires.h"
#include "support/temp_dir.h"

namespace {
constexpr int64_t kS = 1'000'000;
constexpr int64_t kBase = 500'000'000;

// Records every frame the tick hands it, synchronously: a test reads exactly this tick's frame.
class RecordingOutput final : public bc::Output {
 public:
  RecordingOutput(bool needsCpu, bool wantsGpu) : needsCpu_(needsCpu), wantsGpu_(wantsGpu) {}
  bool needsCpu() const override { return needsCpu_; }
  bool wantsGpuRgba8() const override { return wantsGpu_; }
  void start() override {}
  void submit(bc::OutFrame f) override { frames.push_back(std::move(f)); }
  void stop() override {}
  bc::OutputStatus status() const override { return {}; }

  std::vector<bc::OutFrame> frames;

 private:
  bool needsCpu_, wantsGpu_;
};

std::vector<std::unique_ptr<bc::Output>> oneOutput(std::unique_ptr<bc::Output> o) {
  std::vector<std::unique_ptr<bc::Output>> v;
  v.push_back(std::move(o));
  return v;
}

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

  explicit Fixture(bool red = false) {
    br::SegmentHeader h;
    h.inputId = "cam1";
    h.width = 640;
    h.height = 360;
    h.rate = bc::Rate{60, 1};
    store = std::make_unique<br::InputStore>(dir.path, h);

    std::vector<uint8_t> uyvy(static_cast<size_t>(640) * 360 * 2, 128);
    if (red) {
      // Solid red in limited-range BT.601 UYVY.
      for (size_t i = 0; i < uyvy.size(); i += 4) {
        uyvy[i + 0] = 90;   // U
        uyvy[i + 1] = 81;   // Y
        uyvy[i + 2] = 240;  // V
        uyvy[i + 3] = 81;   // Y
      }
    }
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

TEST_CASE("OutputPlayer: a highlight plays the replays as they were at Play") {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  const int a = f.makeEvent(kBase, kBase + 500'000);
  const int b = f.makeEvent(kBase + 500'000, kBase + 1'000'000);
  auto out = std::make_unique<bc::test::FakeOutput>(false);
  auto* raw = out.get();
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs(oneOutput(std::move(out)));
  op.playEvents({a, b});
  f.events.remove(b);
  for (int i = 0; i < 100 && op.currentEvent() != b; ++i) op.tickOnce();
  CHECK(op.currentEvent() == b);
  for (int i = 0; i < 10; ++i) op.tickOnce();
  CHECK(f.waitDelivered(*raw, 40));
  op.stop();
}

TEST_CASE("OutputPlayer: a selection plays in displayed order, whatever order it was sent") {
  Fixture f;
  const int a = f.events.lastN(kBase + 1 * kS, 1 * kS, "cam1").id;  // root
  f.events.newList("");
  const int c = f.events.lastN(kBase + 3 * kS, 1 * kS, "cam1").id;  // group
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.playEvents({c, a, 999});
  CHECK(op.currentEvent() == a);
}

TEST_CASE("OutputPlayer: a selection of unknown ids plays nothing") {
  Fixture f;
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.playEvents({999});
  CHECK(op.idle());
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

TEST_CASE("OutputPlayer: a GPU-only output gets rgba8 and no bgra") {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  const int id = f.makeEvent(kBase, kBase + 2 * kS);
  auto gpu = std::make_unique<RecordingOutput>(false, true);
  auto* gpuRaw = gpu.get();
  auto plain = std::make_unique<RecordingOutput>(false, false);
  auto* plainRaw = plain.get();
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs([&] {
    std::vector<std::unique_ptr<bc::Output>> v;
    v.push_back(std::move(gpu));
    v.push_back(std::move(plain));
    return v;
  }());
  f.player->cue(id);
  f.player->play();
  op.tickOnce();
  REQUIRE(gpuRaw->frames.size() == 1);
  const auto& frame = gpuRaw->frames[0];
  REQUIRE(frame.rgba8);
  CHECK(frame.rgba8->width == 640);
  CHECK(frame.rgba8->height == 360);
  CHECK_FALSE(frame.bgra);
  // A fake that asks for no picture gets none, even from the same tick.
  REQUIRE(plainRaw->frames.size() == 1);
  CHECK_FALSE(plainRaw->frames[0].rgba8);
  CHECK_FALSE(plainRaw->frames[0].bgra);
  op.stop();
}

TEST_CASE("OutputPlayer: a GPU output and a CPU output each get their picture from one tick") {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  const int id = f.makeEvent(kBase, kBase + 2 * kS);
  auto gpu = std::make_unique<RecordingOutput>(false, true);
  auto* gpuRaw = gpu.get();
  auto cpu = std::make_unique<RecordingOutput>(true, false);
  auto* cpuRaw = cpu.get();
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs([&] {
    std::vector<std::unique_ptr<bc::Output>> v;
    v.push_back(std::move(gpu));
    v.push_back(std::move(cpu));
    return v;
  }());
  f.player->cue(id);
  f.player->play();
  op.tickOnce();
  REQUIRE(gpuRaw->frames.size() == 1);
  REQUIRE(cpuRaw->frames.size() == 1);
  CHECK(gpuRaw->frames[0].tick == cpuRaw->frames[0].tick);
  REQUIRE(gpuRaw->frames[0].rgba8);
  CHECK_FALSE(gpuRaw->frames[0].bgra);
  REQUIRE(cpuRaw->frames[0].bgra);
  CHECK(cpuRaw->frames[0].bgra->width == 640);
  CHECK_FALSE(cpuRaw->frames[0].rgba8);
  op.stop();
}

TEST_CASE("OutputPlayer: no picture on air makes no frame and throws nothing") {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  auto gpu = std::make_unique<RecordingOutput>(false, true);
  auto* gpuRaw = gpu.get();
  auto cpu = std::make_unique<RecordingOutput>(true, false);
  auto* cpuRaw = cpu.get();
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs([&] {
    std::vector<std::unique_ptr<bc::Output>> v;
    v.push_back(std::move(gpu));
    v.push_back(std::move(cpu));
    return v;
  }());
  CHECK_NOTHROW(op.tickOnce());  // nothing cued: no source frame
  REQUIRE(gpuRaw->frames.size() == 1);
  REQUIRE(cpuRaw->frames.size() == 1);
  CHECK_FALSE(gpuRaw->frames[0].linear);
  CHECK_FALSE(gpuRaw->frames[0].rgba8);
  CHECK_FALSE(gpuRaw->frames[0].bgra);
  CHECK_FALSE(cpuRaw->frames[0].bgra);
  op.stop();
}

TEST_CASE("OutputPlayer: an HX OMT output sends the picture on air" BC_REQUIRES(bc::test::Req::Omt)
              BC_REQUIRES(bc::test::Req::HardwareGpu)) {
  Fixture f(/*red=*/true);
  f.settings.rate = bc::Rate{30, 1};
  const int id = f.makeEvent(kBase, kBase + 10 * kS);
  auto out = std::make_unique<bc::OmthOutput>("BreadReplay HX " + std::to_string(GetCurrentProcessId()),
                                             omthx::Mode::Hx, omthx::Codec::Hevc, /*keyed=*/false);
  auto* raw = out.get();
  br::OutputPlayer op(*f.player, *f.src, *f.renderer, f.events, f.settings);
  op.setOutputs(oneOutput(std::move(out)));
  f.player->cue(id);
  f.player->play();
  op.start();

  std::string address;
  for (int i = 0; i < 50 && address.empty(); ++i) {
    address = raw->address();
    if (address.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  REQUIRE_FALSE(address.empty());

  bool got = false;
  omthx::VideoFrame vf;
  {
    omthx::Receiver rx(address);
    for (int i = 0; i < 60 && !got; ++i) {
      if (rx.receiveVideo(vf, 200)) got = vf.format.width == 640 && vf.format.height == 360;
    }
  }
  op.stop();
  REQUIRE(got);
  const size_t c = (static_cast<size_t>(vf.format.height / 2) * vf.format.width + vf.format.width / 2) * 4;
  CHECK(std::abs(vf.bgra[c + 2] - 255) <= 8);  // R
  CHECK(std::abs(vf.bgra[c + 1] - 0) <= 8);    // G
  CHECK(std::abs(vf.bgra[c + 0] - 0) <= 8);    // B
}