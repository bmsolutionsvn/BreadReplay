#include <doctest/doctest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "core/timecode.h"
#include "engine/command_router.h"
#include "engine/engine.h"
#include "gpu/converter.h"
#include "gpu/device.h"
#include "gpu/texture_pool.h"
#include "io/ffmpeg_input.h"
#include "support/temp_dir.h"
#include "support/test_media.h"

namespace {
struct Fixture {
  bc::test::TempDir dir;
  std::unique_ptr<bc::GpuDevice> gpu = bc::GpuDevice::create(bc::GpuDevice::Pref::HardwareOrWarp);
  bc::TexturePool pool{*gpu};
  bc::Converter conv{*gpu, pool};
  std::unique_ptr<br::Engine> engine;

  Fixture() {
    bc::test::writeTestTs(dir.path / "a.ts", 320, 180, 30, 2);
    br::Show show;
    show.name = "Router";
    show.settings.rate = bc::Rate{30, 1};
    show.settings.outW = 320;
    show.settings.outH = 180;
    show.inputs.push_back({"cam1", "Cam 1", bc::InputConfig{"file", {{"path", (dir.path / "a.ts").string()}}}});

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
    engine = std::make_unique<br::Engine>(std::move(deps), show, dir.path / "show");
    engine->start();
    for (int i = 0; i < 200 && engine->newestTs() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
};
}  // namespace

TEST_CASE("Router: unknown command returns an error") {
  Fixture f;
  const auto r = f.engine->handle({{"cmd", "no.such.command"}});
  CHECK_FALSE(r.at("ok").get<bool>());
  CHECK(r.at("error").get<std::string>().find("unknown command") != std::string::npos);
}

TEST_CASE("Router: mark.lastN creates an event ending at the newest timestamp") {
  Fixture f;
  REQUIRE(f.engine->newestTs() > 0);
  const int64_t before = f.engine->newestTs();
  const auto r = f.engine->handle({{"cmd", "mark.lastN"}, {"n", 12}});
  REQUIRE(r.at("ok").get<bool>());
  REQUIRE(f.engine->events().events().size() == 1);
  const auto& e = f.engine->events().events().front();
  CHECK(e.outUs >= before);
  CHECK(e.outUs <= f.engine->newestTs());
  CHECK(e.outUs - e.inUs == 12'000'000);
}

TEST_CASE("Router: marks use Preview's time when it is reviewing") {
  Fixture f;
  REQUIRE(f.engine->newestTs() > 0);
  const int64_t at = f.engine->newestTs() - 10'000'000;
  f.engine->playerFor("preview").cueAt(at, "cam1");
  const auto r = f.engine->handle({{"cmd", "mark.in"}});
  REQUIRE(r.at("ok").get<bool>());
  REQUIRE(f.engine->events().events().size() == 1);
  CHECK(f.engine->events().events().front().inUs == at);
}

TEST_CASE("Router: take copies Preview to Output and plays it") {
  Fixture f;
  REQUIRE(f.engine->newestTs() > 0);
  f.engine->playerFor("preview").cueAt(f.engine->newestTs() - 5'000'000, "cam1");
  f.engine->playerFor("preview").play();
  const auto before = f.engine->playerFor("preview").state();
  const auto r = f.engine->handle({{"cmd", "take"}});
  REQUIRE(r.at("ok").get<bool>());
  const auto after = f.engine->playerFor("output").state();
  CHECK(after.playing);
  CHECK(after.posUs == before.posUs);
  CHECK(after.angle == before.angle);
  CHECK(f.engine->playerFor("preview").state().playing);  // Preview is unchanged
}

TEST_CASE("Router: status carries remainingS") {
  Fixture f;
  REQUIRE(f.engine->newestTs() > 0);
  f.engine->handle({{"cmd", "mark.lastN"}, {"n", 10}});
  const int id = f.engine->events().events().front().id;
  f.engine->playerFor("output").cue(id);
  f.engine->playerFor("output").play();
  const auto st = f.engine->statusJson();
  const auto& rem = st.at("players").at("output").at("remainingS");
  REQUIRE(rem.is_number());
  CHECK(rem.get<double>() == doctest::Approx(20.0).epsilon(0.05));  // 10 s at the default 50 %
  CHECK(st.at("inputs").at(0).at("id") == "cam1");
}