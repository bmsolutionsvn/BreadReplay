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

TEST_CASE("Groups: a new show has no group and marks go to the root") {
  br::EventStore s;
  CHECK(s.lists().empty());
  CHECK(s.currentList().empty());
  const int id = s.lastN(100 * kS, 12 * kS, "cam1").id;
  CHECK(s.find(id)->listId.empty());
  CHECK(s.orderOf("") == std::vector<int>{id});
}

TEST_CASE("Groups: a new group is named Game N and becomes the target") {
  br::EventStore s;
  const auto g1 = s.newList("");
  const auto g2 = s.newList("");
  CHECK(s.lists()[0].name == "Game 1");
  CHECK(s.lists()[1].name == "Game 2");
  CHECK(s.currentList() == g2);
  const int id = s.lastN(100 * kS, 12 * kS, "cam1").id;
  CHECK(s.orderOf(g2) == std::vector<int>{id});
  s.selectList("");
  const int r = s.lastN(110 * kS, 12 * kS, "cam1").id;
  CHECK(s.orderOf("") == std::vector<int>{r});
  (void)g1;
}

TEST_CASE("Groups: deleting the target group moves its replays to the root and targets the root") {
  br::EventStore s;
  const int a = s.lastN(100 * kS, 1 * kS, "cam1").id;  // root
  const auto g = s.newList("Game 1");
  const int b = s.lastN(110 * kS, 1 * kS, "cam1").id;
  const int c = s.lastN(120 * kS, 1 * kS, "cam1").id;
  s.deleteList(g);
  CHECK(s.lists().empty());
  CHECK(s.currentList().empty());
  CHECK(s.orderOf("") == std::vector<int>{a, b, c});
  CHECK(s.find(b)->listId.empty());
  const int d = s.lastN(130 * kS, 1 * kS, "cam1").id;
  CHECK(s.find(d)->listId.empty());
}

TEST_CASE("Groups: moving a selection into a group before a replay") {
  br::EventStore s;
  const int a = s.lastN(100 * kS, 1 * kS, "c").id, b = s.lastN(101 * kS, 1 * kS, "c").id,
            c = s.lastN(102 * kS, 1 * kS, "c").id;
  const auto g = s.newList("G");
  const int d = s.lastN(103 * kS, 1 * kS, "c").id;
  s.moveEvents({c, a}, g, d);
  CHECK(s.orderOf(g) == std::vector<int>{c, a, d});
  CHECK(s.orderOf("") == std::vector<int>{b});
  CHECK(s.find(a)->listId == g);
}

TEST_CASE("Groups: dropping a selection onto one of its own rows loses nothing") {
  br::EventStore s;
  const int a = s.lastN(100 * kS, 1 * kS, "c").id, b = s.lastN(101 * kS, 1 * kS, "c").id,
            c = s.lastN(102 * kS, 1 * kS, "c").id;
  s.moveEvents({a, b}, "", b);
  CHECK(s.orderOf("") == std::vector<int>{a, b, c});
  s.moveEvents({a}, "", std::nullopt);
  CHECK(s.orderOf("") == std::vector<int>{b, c, a});
}

TEST_CASE("Groups: rename trims and refuses an empty name") {
  br::EventStore s;
  const auto g = s.newList("");
  CHECK(s.renameList(g, "  Final  "));
  CHECK(s.lists()[0].name == "Final");
  CHECK_FALSE(s.renameList(g, "   "));
  CHECK_FALSE(s.renameList("nope", "X"));
}

TEST_CASE("Groups: display order is the root, then each group") {
  br::EventStore s;
  const int a = s.lastN(100 * kS, 1 * kS, "c").id;
  const auto g = s.newList("");
  const int b = s.lastN(101 * kS, 1 * kS, "c").id;
  CHECK(s.displayOrder() == std::vector<int>{a, b});
  (void)g;
}

TEST_CASE("Groups: an old show keeps its Game 1 and round-trips with a root") {
  const auto old = nlohmann::json::parse(R"({"nextId":2,"nextListId":2,"currentList":"list-1",
    "lists":[{"id":"list-1","name":"Game 1","order":[1]}],
    "events":[{"id":1,"listId":"list-1","inUs":0,"outUs":5}]})");
  auto s = br::EventStore::fromJson(old);
  REQUIRE(s.lists().size() == 1);
  CHECK(s.currentList() == "list-1");
  CHECK(s.orderOf("list-1") == std::vector<int>{1});
  CHECK(s.orderOf("").empty());
  s.selectList("");
  const int r = s.lastN(100 * kS, 1 * kS, "c").id;
  const auto back = br::EventStore::fromJson(s.toJson());
  CHECK(back.orderOf("") == std::vector<int>{r});
  CHECK(back.currentList().empty());
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
  CHECK(back.lists().size() == 1);
  CHECK(back.currentList() == back.lists()[0].id);
  CHECK(back.orderOf("") == std::vector<int>{id});
  // A new event after loading still gets a fresh id.
  CHECK(back.lastN(200 * kS, 1 * kS, "cam1").id == 2);
}