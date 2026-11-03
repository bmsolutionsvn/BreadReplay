#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include "core/timecode.h"
#include "engine/engine.h"
#include "engine/surface_hub.h"
#include "events/show_file.h"
#include "gpu/converter.h"
#include "gpu/device.h"
#include "gpu/texture_pool.h"
#include "io/ffmpeg_input.h"
#include "support/requires.h"
#include "support/temp_dir.h"
#include "support/test_media.h"

namespace {
struct Fixture {
  bc::test::TempDir dir;
  std::unique_ptr<bc::GpuDevice> gpu = bc::GpuDevice::create(bc::GpuDevice::Pref::HardwareOrWarp);
  bc::TexturePool pool{*gpu};
  bc::Converter conv{*gpu, pool};

  br::EngineDeps depsMakers() {
    br::EngineDeps deps;
    deps.gpu = gpu.get();
    deps.pool = &pool;
    deps.conv = &conv;
    deps.clock = [] { return br::nowUs(); };
    bc::GpuDevice* g = gpu.get();
    bc::TexturePool* p = &pool;
    deps.makeInput = [g, p](const bc::InputConfig& cfg) -> std::unique_ptr<bc::Input> {
      bc::FfmpegInputOptions o;
      o.url = cfg.params.at("path").get<std::string>();
      o.reconnect = false;
      o.realtime = true;
      return std::make_unique<bc::FfmpegInput>(*g, *p, o);
    };
    return deps;
  }

  br::Show makeShow(int inputs) {
    br::Show show;
    show.name = "Engine test";
    show.settings.rate = bc::Rate{30, 1};
    show.settings.outW = 1280;
    show.settings.outH = 720;
    for (int i = 0; i < inputs; ++i) {
      const auto file = dir.path / ("in" + std::to_string(i) + ".ts");
      bc::test::writeTestTs(file, 1280, 720, 30, 3);
      show.inputs.push_back({"cam" + std::to_string(i), "Input " + std::to_string(i),
                             bc::InputConfig{"file", {{"path", file.string()}}}});
    }
    return show;
  }

  bool waitFor(const std::function<bool()>& pred, int ms) {
    for (int i = 0; i < ms / 20; ++i) {
      if (pred()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pred();
  }
};
}  // namespace

TEST_CASE("Engine: status carries the saved hotkeys") {
  Fixture f;
  auto show = f.makeShow(0);
  show.settings.hotkeys["mark.in"] = "J";
  br::Engine engine(f.depsMakers(), show, f.dir.path / "show");
  CHECK(engine.statusJson()["hotkeys"]["mark.in"] == "J");
}

TEST_CASE("Engine: two file inputs record 3 s each") {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(2), f.dir.path / "show");
  engine.start();
  const bool got = f.waitFor(
      [&] {
        const auto* a = engine.store("cam0");
        const auto* b = engine.store("cam1");
        return a && b && a->frameCount() >= 80 && b->frameCount() >= 80;
      },
      25000);
  engine.stop();
  REQUIRE(got);
}

TEST_CASE("Engine: a restarted engine reopens the show and keeps recording") {
  Fixture f;
  const auto folder = f.dir.path / "show";
  int64_t firstNewest = 0;
  {
    br::Engine engine(f.depsMakers(), f.makeShow(1), folder);
    engine.start();
    REQUIRE(f.waitFor([&] { const auto* s = engine.store("cam0"); return s && s->frameCount() > 20; }, 20000));
    engine.events().lastN(engine.newestTs(), 5'000'000, "cam0");
    firstNewest = engine.newestTs();
    engine.saveShow();
    engine.stop();
  }

  const br::Show saved = br::loadShow(folder / "show.json");
  REQUIRE(saved.events.events().size() == 1);

  br::Engine engine(f.depsMakers(), saved, folder);
  engine.start();
  const bool grew = f.waitFor([&] {
    const auto* s = engine.store("cam0");
    return s && s->newestTs() > firstNewest;
  }, 20000);
  const int64_t newest = engine.newestTs();
  engine.stop();
  CHECK(grew);
  CHECK(newest > firstNewest);
  CHECK(engine.events().events().size() == 1);
  CHECK(engine.events().events().front().inUs == saved.events.events().front().inUs);
}
TEST_CASE("Engine: a fresh Preview follows the live edge on the first input") {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(2), f.dir.path / "show");
  engine.start();
  REQUIRE(f.waitFor([&] { const auto* s = engine.store("cam0"); return s && s->frameCount() > 10; }, 20000));
  engine.followLiveEdge();
  const auto pv = engine.playerFor("preview").state();
  const int64_t newest = engine.newestTs();
  engine.stop();
  CHECK(pv.angle == "cam0");
  CHECK(std::llabs(pv.posUs - newest) <= 1'000'000);
}

TEST_CASE("Engine: a Preview restored with an angle but no position follows the live edge on it") {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(2), f.dir.path / "show");
  engine.start();
  engine.playerFor("preview").setAngle("cam1");  // as a workspace restore leaves it: angle, time 0
  REQUIRE(f.waitFor([&] { const auto* s = engine.store("cam1"); return s && s->frameCount() > 10; }, 20000));
  engine.followLiveEdge();
  const auto pv = engine.playerFor("preview").state();
  const int64_t newest = engine.newestTs();
  engine.stop();
  CHECK(pv.angle == "cam1");
  CHECK(std::llabs(pv.posUs - newest) <= 1'000'000);
}

TEST_CASE("Engine: no binary preview without a wantPreviews client") {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(1), f.dir.path / "show");
  std::atomic<int> binary{0};
  engine.setSender([](const std::string&) {},
                   [&](const std::vector<uint8_t>&) { binary.fetch_add(1); });
  engine.setBinaryWanted([] { return false; });
  engine.start();
  REQUIRE(f.waitFor([&] { const auto* s = engine.store("cam0"); return s && s->frameCount() > 10; }, 20000));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const int count = binary.load();
  engine.stop();
  CHECK(count == 0);
}

TEST_CASE("Engine: binary previews resume for a fallback client") {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(1), f.dir.path / "show");
  std::atomic<bool> wanted{false};
  std::atomic<int> previews{0};
  engine.setSender([](const std::string&) {}, [&](const std::vector<uint8_t>& d) {
    if (!d.empty() && d[0] == 1) previews.fetch_add(1);  // the preview message's tag
  });
  engine.setBinaryWanted([&] { return wanted.load(); });
  engine.start();
  REQUIRE(f.waitFor([&] { const auto* s = engine.store("cam0"); return s && s->frameCount() > 10; }, 20000));
  wanted = true;
  const bool got = f.waitFor([&] { return previews.load() > 0; }, 500);
  engine.stop();
  CHECK(got);
}

TEST_CASE("Engine: presents to wanted surfaces" BC_REQUIRES(bc::test::Req::HardwareGpu)) {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(1), f.dir.path / "show");
  bc::SurfaceHub hub(*f.gpu, f.conv);
  REQUIRE(hub.subscribe("c", {{"preview", 320, 180}, {"output", 320, 180}}).size() == 2);
  engine.setSurfaces(&hub);
  engine.setBinaryWanted([] { return false; });
  engine.setSender([](const std::string&) {}, [](const std::vector<uint8_t>&) {});
  engine.start();
  REQUIRE(f.waitFor([&] { const auto* s = engine.store("cam0"); return s && s->frameCount() > 10; }, 20000));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const bool broken = hub.anyBroken();
  engine.stop();
  CHECK_FALSE(broken);
}

TEST_CASE("Engine: output with nothing on air presents nothing and does not throw" BC_REQUIRES(bc::test::Req::HardwareGpu)) {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(1), f.dir.path / "show");
  bc::SurfaceHub hub(*f.gpu, f.conv);
  REQUIRE(hub.subscribe("c", {{"output", 320, 180}}).size() == 1);
  engine.setSurfaces(&hub);
  engine.setBinaryWanted([] { return false; });
  engine.setSender([](const std::string&) {}, [](const std::vector<uint8_t>&) {});
  engine.start();
  REQUIRE(f.waitFor([&] { const auto* s = engine.store("cam0"); return s && s->frameCount() > 10; }, 20000));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const bool broken = hub.anyBroken();
  engine.stop();
  CHECK_FALSE(broken);
}

TEST_CASE("Engine: statusJson reports nativeVideo") {
  Fixture f;
  br::Engine engine(f.depsMakers(), f.makeShow(0), f.dir.path / "show");
  CHECK(engine.statusJson()["nativeVideo"].is_string());
  engine.setNativeVideoReason("no swap chain");
  CHECK(engine.statusJson()["nativeVideo"] == "no swap chain");
  bc::SurfaceHub hub(*f.gpu, f.conv);
  engine.setSurfaces(&hub);
  CHECK(engine.statusJson()["nativeVideo"] == true);
}
