#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

#include "play/player.h"

namespace {
constexpr int64_t kS = 1'000'000;
const bc::Rate kRate{50, 1};  // 20 ms periods
constexpr int64_t kPeriod = 20'000;

const bc::Rate kTen{10, 1};  // 100 ms periods

br::EventStore passStore() {
  br::EventStore store;
  store.markIn(0, "cam1");
  store.markOut(10 * kS, "cam1", 1 * kS);
  auto* e = store.find(1);
  e->inUs = 0;
  e->outUs = 10 * kS;
  e->angles = {{0, "cam1"}, {4 * kS, "cam3"}, {8 * kS, "cam4"}};
  e->speeds = {{0, 1.0}};
  e->zooms = {{0, {}}};
  return store;
}

void ticksTo(br::Player& p, int64_t tUs) {
  for (int i = 0; i < 5000 && p.state().posUs < tUs; ++i) p.tick();
}

br::EventStore storeWithEvent(int64_t in, int64_t out, double speed = 0.5) {
  br::EventStore store;
  store.markIn(in, "cam1");
  store.markOut(out, "cam1", 1 * kS);
  store.find(1)->speeds = {{in, speed}};
  store.find(1)->outUs = out;
  return store;
}
}  // namespace

TEST_CASE("Player: 50 % advances half a period per tick") {
  br::EventStore store;
  br::Player p(store, kRate, true);
  p.cueAt(100 * kS, "cam1");
  p.setSpeedPreset(0.5);
  p.play();
  const auto r = p.tick();
  CHECK(r.sourceUs == 100 * kS);
  CHECK(p.state().posUs == 100 * kS + kPeriod / 2);
}

TEST_CASE("Player: reports ended at Out and stops") {
  auto store = storeWithEvent(100 * kS, 100 * kS + 2 * kPeriod, 1.0);
  br::Player p(store, kRate, true);
  p.cue(1);
  p.play();
  p.tick();
  p.tick();
  const auto r = p.tick();
  CHECK(r.ended);
  CHECK_FALSE(p.state().playing);
  CHECK(p.state().posUs == 100 * kS + 2 * kPeriod);  // clamped at Out
}

TEST_CASE("Player: reverse stops at In") {
  auto store = storeWithEvent(100 * kS, 100 * kS + 2 * kPeriod, 1.0);
  br::Player p(store, kRate, true);
  p.cue(1);
  p.state();  // cue leaves the playhead at In
  p.play();
  p.setDirection(-1);
  const auto r = p.tick();
  CHECK(r.ended);
  CHECK_FALSE(p.state().playing);
  CHECK(p.state().posUs == 100 * kS);
}

TEST_CASE("Player: remaining time at 50 % for a 5 s event is 10 s") {
  auto store = storeWithEvent(100 * kS, 105 * kS, 0.5);
  br::Player p(store, kRate, true);
  p.cue(1);
  CHECK(p.remainingSeconds() == doctest::Approx(10.0));
}

TEST_CASE("Player: an angle cut switches the source at its time") {
  auto store = storeWithEvent(100 * kS, 110 * kS, 1.0);
  store.recordAngle(1, 102 * kS, "cam2");
  store.find(1)->speeds = {{100 * kS, 1.0}};
  br::Player p(store, kRate, true);
  p.cue(1);
  p.play();
  CHECK(p.tick().inputId == "cam1");
  for (int i = 0; i < 50; ++i) p.tick();  // past 102 s
  p.cueAt(103 * kS, "cam1");
  p.cue(1);
  p.play();
  while (p.state().posUs < 102 * kS) p.tick();
  const auto r = p.tick();
  CHECK(r.inputId == "cam2");
  CHECK(r.angleChanged);
}

TEST_CASE("Player: the lever is locked by default but presets work") {
  br::EventStore store;
  br::Player p(store, kRate, true);
  p.cueAt(100 * kS, "cam1");
  CHECK(p.state().leverLocked);
  p.setLever(0.25);
  CHECK(p.state().lever == doctest::Approx(1.0));
  p.setSpeedPreset(0.25);
  CHECK(p.state().lever == doctest::Approx(0.25));
}

TEST_CASE("Player: moving the lever records speed points") {
  auto store = storeWithEvent(100 * kS, 110 * kS, 1.0);
  br::Player p(store, kRate, true);
  p.cue(1);
  p.play();
  p.tick();
  p.lockLever(false);
  p.setLever(0.25);
  const auto& e = *store.find(1);
  REQUIRE(e.speeds.size() == 2);
  CHECK(e.speeds.back().tUs == p.state().posUs);
  CHECK(e.speeds.back().speed == doctest::Approx(0.25));
}

TEST_CASE("Player: a pass replaces only the stretch it played over") {
  auto store = passStore();
  br::Player p(store, kTen, true);
  p.cue(1);
  p.step(20);  // 2 s
  p.play();
  ticksTo(p, 3 * kS);
  p.setAngle("cam2");
  ticksTo(p, 6 * kS);
  p.pause();
  const auto& e = *store.find(1);
  REQUIRE(e.angles.size() == 4);
  CHECK(e.angles[0].inputId == "cam1");
  CHECK(e.angles[1].tUs == 3 * kS);
  CHECK(e.angles[1].inputId == "cam2");
  CHECK(e.angles[2].tUs == 6 * kS);
  CHECK(e.angles[2].inputId == "cam3");
  CHECK(e.angles[3].tUs == 8 * kS);
  CHECK(e.angles[3].inputId == "cam4");
}

TEST_CASE("Player: an old cut the pass plays over does not fire") {
  auto store = passStore();
  br::Player p(store, kTen, true);
  p.cue(1);
  p.step(20);
  p.play();
  ticksTo(p, 3 * kS);
  p.setAngle("cam2");
  ticksTo(p, 4500'000);
  CHECK(p.tick().inputId == "cam2");
}

TEST_CASE("Player: riding speed leaves camera cuts alone") {
  auto store = passStore();
  br::Player p(store, kTen, true);
  p.cue(1);
  p.step(20);
  p.play();
  ticksTo(p, 3 * kS);
  p.setSpeedPreset(0.25);
  ticksTo(p, 4 * kS);
  p.pause();
  const auto& angles = store.find(1)->angles;
  REQUIRE(angles.size() == 3);
  CHECK(angles[0].inputId == "cam1");
  CHECK(angles[1].inputId == "cam3");
  CHECK(angles[2].inputId == "cam4");
}

TEST_CASE("Player: riding in reverse changes the picture but not the replay") {
  auto store = passStore();
  br::Player p(store, kTen, true);
  p.cue(1);
  p.step(70);  // 7 s
  p.setDirection(-1);
  p.play();
  while (p.state().posUs > 6 * kS) p.tick();
  p.setAngle("cam2");
  CHECK(p.tick().inputId == "cam2");
  while (p.state().posUs > 5 * kS) p.tick();
  p.pause();
  const auto& angles = store.find(1)->angles;
  REQUIRE(angles.size() == 3);
  CHECK(angles[0].inputId == "cam1");
  CHECK(angles[1].inputId == "cam3");
  CHECK(angles[2].inputId == "cam4");
}

TEST_CASE("Player: camera keys while paused only switch Preview") {
  auto store = passStore();
  br::Player p(store, kTen, true);
  p.cue(1);
  p.setAngle("cam2");
  const auto& angles = store.find(1)->angles;
  REQUIRE(angles.size() == 3);
  CHECK(p.state().angle == "cam2");
}

TEST_CASE("Player: Output plays a frozen copy") {
  auto store = passStore();
  br::Player p(store, kTen, true, true);
  p.cue(1);
  store.trim(1, 0, -5 * kS);
  p.play();
  CHECK(p.remainingSeconds() == doctest::Approx(10.0));
  ticksTo(p, 6 * kS);
  CHECK(p.state().posUs >= 6 * kS);
  CHECK(p.state().playing);
}

TEST_CASE("Player: riding on Output records into the store and the picture") {
  auto store = passStore();
  br::Player p(store, kTen, true, true);
  p.cue(1);
  p.play();
  ticksTo(p, 2 * kS);
  p.setAngle("cam2");
  CHECK(p.tick().inputId == "cam2");
  const auto& angles = store.find(1)->angles;
  CHECK(std::any_of(angles.begin(), angles.end(), [](const br::AngleCut& a) { return a.inputId == "cam2"; }));
}

TEST_CASE("Player: pass writes take the store lock") {
  auto store = passStore();
  std::recursive_mutex lock;
  br::Player p(store, kTen, true, false, &lock);
  p.cue(1);
  p.play();
  std::atomic<bool> held{false};
  std::atomic<bool> done{false};
  std::thread holder([&] {
    std::unique_lock held_(lock);
    held = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  });
  while (!held) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::thread ticker([&] {
    p.tick();
    done = true;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const bool blocked = !done;
  holder.join();
  ticker.join();
  CHECK(blocked);
  CHECK(done);
}

TEST_CASE("Player: a 30 fps angle in a 60 fps show repeats frames") {
  bc::Rate showRate{60000, 1001};
  const int64_t period = static_cast<int64_t>(1'000'000.0 / showRate.fps() + 0.5);  // 16683
  auto store = storeWithEvent(100 * kS, 110 * kS, 0.5);
  br::Player p(store, showRate, true);
  p.cue(1);
  p.play();
  int64_t lastFrame = (p.tick().sourceUs - 100 * kS) / 33'333;
  CHECK(lastFrame == 0);
  int repeats = 0;
  // Frames arrive every ~33.3 ms; at 50 % the 16.7 ms tick step lands twice on most frames.
  for (int i = 0; i < 20; ++i) {
    const auto r = p.tick();
    const int64_t frame = (r.sourceUs - 100 * kS) / 33'333;
    if (frame == lastFrame) ++repeats;
    lastFrame = frame;
  }
  CHECK(repeats > 0);
  CHECK(p.state().posUs - (100 * kS) == doctest::Approx(period * 0.5 * 21).epsilon(0.01));
}