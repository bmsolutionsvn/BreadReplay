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
#include "support/requires.h"
#include "support/temp_dir.h"
#include "support/test_media.h"

namespace {
constexpr int64_t kS = 1'000'000;

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

TEST_CASE("Router: group commands create, rename, move and delete") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int a = r.handle({{"cmd", "mark.lastN"}, {"n", 1}})["id"];
  const std::string g = r.handle({{"cmd", "list.new"}})["id"];
  CHECK(f.engine->events().lists()[0].name == "Game 1");
  CHECK(r.handle({{"cmd", "list.rename"}, {"id", g}, {"name", ""}})["error"] == "name is empty");
  CHECK(r.handle({{"cmd", "list.rename"}, {"id", g}, {"name", "Final"}})["ok"] == true);
  r.handle({{"cmd", "event.move"}, {"ids", {a}}, {"listId", g}});
  CHECK(f.engine->events().orderOf(g) == std::vector<int>{a});
  r.handle({{"cmd", "list.delete"}, {"id", g}});
  CHECK(f.engine->events().orderOf("") == std::vector<int>{a});
}

TEST_CASE("Router: a mark replies with the camera and time it used") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const auto res = r.handle({{"cmd", "mark.lastN"}, {"n", 2}});
  CHECK(res["ok"] == true);
  CHECK(res["edge"] == "out");
  CHECK(res["inputId"] == "cam1");
  CHECK(res["tUs"].get<int64_t>() == f.engine->events().find(res["id"])->outUs);
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

TEST_CASE("Router: undo reverts a mark and redo brings it back") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  r.handle({{"cmd", "mark.lastN"}, {"n", 1}});
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == true);
  CHECK(f.engine->events().events().empty());
  CHECK(r.handle({{"cmd", "edit.redo"}})["done"] == true);
  CHECK(f.engine->events().events().size() == 1);
}

TEST_CASE("Router: a command that changes nothing adds no undo step") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  r.handle({{"cmd", "event.trim"}, {"id", 42}, {"inFrames", 1}});
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == false);
}

TEST_CASE("Router: event.update takes speeds and zooms") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int id = r.handle({{"cmd", "mark.lastN"}, {"n", 2}})["id"];
  const auto& e = *f.engine->events().find(id);
  r.handle({{"cmd", "event.update"}, {"id", id},
            {"speeds", {{{"tUs", e.inUs + kS}, {"speed", 0.25}}, {{"tUs", e.inUs}, {"speed", 1.0}}}},
            {"zooms", {{{"tUs", e.inUs}, {"rect", {{"cx", 0.4}, {"cy", 0.5}, {"scale", 2.0}}}}}}});
  REQUIRE(e.speeds.size() == 2);
  CHECK(e.speeds[0].speed == doctest::Approx(1.0));
  CHECK(e.zooms[0].rect.scale == doctest::Approx(2.0));
}

TEST_CASE("Router: one pass is one undo step, even when it ends by itself") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int id = r.handle({{"cmd", "mark.lastN"}, {"n", 2}})["id"];
  r.handle({{"cmd", "player.cue"}, {"eventId", id}});
  const auto before = *f.engine->events().find(id);
  r.handle({{"cmd", "player.play"}});
  r.handle({{"cmd", "player.preset"}, {"speed", 0.75}});
  r.handle({{"cmd", "player.preset"}, {"speed", 0.5}});
  r.handle({{"cmd", "player.angle"}, {"inputId", "cam1"}});
  auto& p = f.engine->playerFor("preview");
  for (int i = 0; i < 10000 && p.state().playing; ++i) p.tick();
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == true);
  const auto& after = *f.engine->events().find(id);
  REQUIRE(after.angles.size() == before.angles.size());
  for (size_t i = 0; i < before.angles.size(); ++i) {
    CHECK(after.angles[i].tUs == before.angles[i].tUs);
    CHECK(after.angles[i].inputId == before.angles[i].inputId);
  }
  REQUIRE(after.speeds.size() == before.speeds.size());
  for (size_t i = 0; i < before.speeds.size(); ++i) {
    CHECK(after.speeds[i].tUs == before.speeds[i].tUs);
    CHECK(after.speeds[i].speed == doctest::Approx(before.speeds[i].speed));
  }
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == true);
  CHECK(f.engine->events().events().empty());
}

TEST_CASE("Router: an edit after a pass settles the pass first") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int id = r.handle({{"cmd", "mark.lastN"}, {"n", 2}})["id"];
  r.handle({{"cmd", "player.cue"}, {"eventId", id}});
  const auto marked = *f.engine->events().find(id);
  r.handle({{"cmd", "player.play"}});
  r.handle({{"cmd", "player.preset"}, {"speed", 0.75}});
  auto& p = f.engine->playerFor("preview");
  for (int i = 0; i < 10000 && p.state().playing; ++i) p.tick();
  const auto afterPass = *f.engine->events().find(id);
  r.handle({{"cmd", "player.preset"}, {"speed", 0.5}});
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == true);
  const auto& afterUndo = *f.engine->events().find(id);
  REQUIRE(afterUndo.speeds.size() == afterPass.speeds.size());
  for (size_t i = 0; i < afterPass.speeds.size(); ++i) {
    CHECK(afterUndo.speeds[i].tUs == afterPass.speeds[i].tUs);
    CHECK(afterUndo.speeds[i].speed == doctest::Approx(afterPass.speeds[i].speed));
  }
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == true);
  const auto& afterSecond = *f.engine->events().find(id);
  REQUIRE(afterSecond.speeds.size() == marked.speeds.size());
  for (size_t i = 0; i < marked.speeds.size(); ++i)
    CHECK(afterSecond.speeds[i].speed == doctest::Approx(marked.speeds[i].speed));
}

TEST_CASE("Router: undo during a pass stops the pass") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int id = r.handle({{"cmd", "mark.lastN"}, {"n", 2}})["id"];
  r.handle({{"cmd", "player.cue"}, {"eventId", id}});
  const auto marked = *f.engine->events().find(id);
  r.handle({{"cmd", "player.play"}});
  r.handle({{"cmd", "player.preset"}, {"speed", 0.75}});
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == true);
  f.engine->playerFor("preview").tick();
  const auto& after = *f.engine->events().find(id);
  REQUIRE(after.speeds.size() == marked.speeds.size());
  for (size_t i = 0; i < marked.speeds.size(); ++i)
    CHECK(after.speeds[i].speed == doctest::Approx(marked.speeds[i].speed));
}

TEST_CASE("Router: status says Undo is available while a pass is being ridden") {
  Fixture f;
  const int id = f.engine->handle({{"cmd", "mark.lastN"}, {"n", 2}})["id"];
  f.engine->history().clear();  // as if the show had just been opened
  f.engine->handle({{"cmd", "player.cue"}, {"eventId", id}});
  f.engine->handle({{"cmd", "player.play"}});
  f.engine->handle({{"cmd", "player.preset"}, {"speed", 0.5}});
  CHECK(f.engine->statusJson()["edit"]["canUndo"] == true);
}

TEST_CASE("Router: riding a highlight is an undo step") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int id = r.handle({{"cmd", "mark.lastN"}, {"n", 10}})["id"];
  r.handle({{"cmd", "highlight.play"}, {"ids", {id}}});
  const auto beforeRide = *f.engine->events().find(id);
  r.handle({{"cmd", "player.preset"}, {"player", "output"}, {"speed", 0.25}});
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == true);
  const br::Event* after = f.engine->events().find(id);
  REQUIRE(after != nullptr);
  REQUIRE(after->speeds.size() == beforeRide.speeds.size());
  for (size_t i = 0; i < beforeRide.speeds.size(); ++i)
    CHECK(after->speeds[i].speed == doctest::Approx(beforeRide.speeds[i].speed));
}

TEST_CASE("Router: undo while Output plays does not change the picture") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int id = r.handle({{"cmd", "mark.lastN"}, {"n", 2}})["id"];
  r.handle({{"cmd", "player.cue"}, {"eventId", id}});
  r.handle({{"cmd", "player.play"}});
  r.handle({{"cmd", "take"}});
  const double before = f.engine->playerFor("output").remainingSeconds();
  r.handle({{"cmd", "event.trim"}, {"id", id}, {"outFrames", -30}});
  r.handle({{"cmd", "edit.undo"}});
  r.handle({{"cmd", "edit.redo"}});
  CHECK(f.engine->playerFor("output").remainingSeconds() == doctest::Approx(before));
}

TEST_CASE("Router: a new show clears undo history") {
  Fixture f;
  br::CommandRouter r(*f.engine);
  r.handle({{"cmd", "mark.lastN"}, {"n", 1}});
  r.handle({{"cmd", "show.new"}, {"name", "Reopen"}, {"folder", f.dir.path.string()}});
  CHECK(r.handle({{"cmd", "edit.undo"}})["done"] == false);
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

TEST_CASE("Router: export.start names a group export after the group"
          BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  br::CommandRouter r(*f.engine);
  CHECK(r.handle({{"cmd", "export.start"}})["ok"] == false);
  const int a = r.handle({{"cmd", "mark.lastN"}, {"n", 1}})["id"];
  const std::string g = r.handle({{"cmd", "list.new"}, {"name", "Game 1"}})["id"];
  r.handle({{"cmd", "event.move"}, {"ids", {a}}, {"listId", g}});
  const auto res = r.handle({{"cmd", "export.start"}, {"listId", g}});
  REQUIRE(res["ok"] == true);
  const auto st = f.engine->statusJson();
  REQUIRE(st["exports"].size() == 1);
  CHECK(st["exports"][0]["name"] == "Game 1");
  CHECK(st["exports"][0]["path"].get<std::string>().ends_with(".mp4"));
}

TEST_CASE("Router: later edits do not change a queued export"
          BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  br::CommandRouter r(*f.engine);
  const int a = r.handle({{"cmd", "mark.lastN"}, {"n", 1}})["id"];
  const auto res = r.handle({{"cmd", "export.start"}, {"id", a}});
  REQUIRE(res["ok"] == true);
  const int id = res["id"];
  r.handle({{"cmd", "event.delete"}, {"id", a}});
  std::string state;
  for (int i = 0; i < 3000; ++i) {
    const auto st = f.engine->statusJson();
    for (const auto& e : st["exports"]) {
      if (e["id"] == id) state = e["state"];
    }
    if (state == "done" || state == "failed" || state == "cancelled") break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(state == "done");
}