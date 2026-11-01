#include <doctest/doctest.h>

#include "events/event_store.h"

using namespace std::chrono_literals;

namespace {
constexpr int64_t kS = 1'000'000;
}  // namespace

TEST_CASE("Events: lastN makes an event ending now") {
  br::EventStore store;
  const auto& e = store.lastN(100 * kS, 12 * kS, "cam1");
  CHECK(e.inUs == 88 * kS);
  CHECK(e.outUs == 100 * kS);
  CHECK(e.listId == store.currentList());
  REQUIRE(e.angles.size() == 1);
  CHECK(e.angles[0].inputId == "cam1");
  REQUIRE(e.speeds.size() == 1);
  CHECK(e.speeds[0].speed == doctest::Approx(0.5));
}

TEST_CASE("Events: pre/post roll widen marks") {
  br::EventStore store(2 * kS, 3 * kS);
  auto& e = store.markIn(100 * kS, "cam1");
  store.markOut(110 * kS, "cam1", 12 * kS);
  CHECK(e.inUs == 98 * kS);
  CHECK(e.outUs == 113 * kS);
}

TEST_CASE("Events: a second Mark In moves the open In") {
  br::EventStore store;
  auto& e = store.markIn(100 * kS, "cam1");
  const int id = e.id;
  auto& moved = store.markIn(95 * kS, "cam2");
  CHECK(moved.id == id);
  CHECK(moved.inUs == 95 * kS);
  CHECK(moved.angles.front().inputId == "cam2");
  CHECK(store.events().size() == 1);
}

TEST_CASE("Events: Mark Out without In uses Last N") {
  br::EventStore store;
  const auto& e = store.markOut(100 * kS, "cam1", 12 * kS);
  CHECK(e.inUs == 88 * kS);
  CHECK(e.outUs == 100 * kS);
}

TEST_CASE("Events: angleAt follows cuts") {
  br::EventStore store;
  const auto& e = store.lastN(110 * kS, 20 * kS, "cam1");
  const int id = e.id;
  store.recordAngle(id, 102 * kS, "cam2");
  store.recordAngle(id, 106 * kS, "cam3");
  CHECK(br::angleAt(*store.find(id), 101 * kS) == "cam1");
  CHECK(br::angleAt(*store.find(id), 103 * kS) == "cam2");
  CHECK(br::angleAt(*store.find(id), 107 * kS) == "cam3");
}

TEST_CASE("Events: recordAngle overwrites what comes after") {
  br::EventStore store;
  const int id = store.lastN(110 * kS, 20 * kS, "cam1").id;
  store.recordAngle(id, 104 * kS, "cam2");
  store.recordAngle(id, 106 * kS, "cam3");
  store.recordAngle(id, 104 * kS, "cam4");
  const auto& e = *store.find(id);
  REQUIRE(e.angles.size() == 2);
  CHECK(e.angles[0].inputId == "cam1");
  CHECK(e.angles[1].inputId == "cam4");
  CHECK(br::angleAt(e, 110 * kS) == "cam4");
}

TEST_CASE("Events: ids are never reused") {
  br::EventStore store;
  const int a = store.lastN(10 * kS, 1 * kS, "cam1").id;
  const int b = store.lastN(20 * kS, 1 * kS, "cam1").id;
  store.remove(b);
  const int c = store.lastN(30 * kS, 1 * kS, "cam1").id;
  CHECK(a == 1);
  CHECK(b == 2);
  CHECK(c == 3);
}

TEST_CASE("Events: json round trip") {
  br::EventStore store(1 * kS, 2 * kS);
  const auto& e = store.lastN(100 * kS, 12 * kS, "cam1");
  const int id = e.id;
  store.recordAngle(id, 92 * kS, "cam2");
  store.recordSpeed(id, 94 * kS, 0.25);
  store.recordZoom(id, 96 * kS, br::ZoomRect{0.3f, 0.6f, 2.0f});
  store.find(id)->tags = {"fight", "round1"};
  store.find(id)->angleNotes["cam1"] = "wide";
  store.newList("Game 2");

  const auto json = store.toJson();
  auto back = br::EventStore::fromJson(json);
  CHECK(back.version() >= 1);
  REQUIRE(back.events().size() == 1);
  const auto& b = back.events()[0];
  CHECK(b.id == id);
  CHECK(b.inUs == 88 * kS);
  CHECK(b.outUs == 100 * kS);
  CHECK(b.tags == std::vector<std::string>{"fight", "round1"});
  CHECK(b.angleNotes.at("cam1") == "wide");
  REQUIRE(b.angles.size() == 2);
  CHECK(b.angles[1].inputId == "cam2");
  REQUIRE(b.speeds.size() == 2);
  CHECK(b.speeds[1].speed == doctest::Approx(0.25));
  REQUIRE(b.zooms.size() == 2);
  CHECK(b.zooms[1].rect.scale == doctest::Approx(2.0));
  CHECK(back.lists().size() == 2);
  CHECK(back.currentList() == back.lists()[1].id);
  // A new event after loading still gets a fresh id.
  CHECK(back.lastN(200 * kS, 1 * kS, "cam1").id == 2);
}