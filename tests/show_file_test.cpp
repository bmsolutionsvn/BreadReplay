#include <doctest/doctest.h>

#include "events/show_file.h"
#include "support/temp_dir.h"

TEST_CASE("ShowFile: export settings round-trip") {
  bc::test::TempDir dir;
  br::Show show;
  show.settings.exportFolder = "D:/Exports";
  show.settings.exportCodec = "hevc";
  show.settings.exportQuality = "small";
  show.settings.exportMbps = 6.5;
  const auto path = dir.path / "show.json";
  br::saveShow(path, show);
  const br::Show back = br::loadShow(path);
  CHECK(back.settings.exportFolder == "D:/Exports");
  CHECK(back.settings.exportCodec == "hevc");
  CHECK(back.settings.exportQuality == "small");
  CHECK(back.settings.exportMbps == doctest::Approx(6.5));
}

TEST_CASE("ShowFile: save leaves no temp file and loads back equal") {
  bc::test::TempDir dir;
  br::Show show;
  show.name = "Match day";
  show.inputs.push_back({"cam1", "Observer 1", bc::InputConfig{"srt", {{"url", "srt://127.0.0.1:9000"}}}});
  show.outputs.push_back(bc::OutputConfig{"out1", "ndi", false, true, {{"name", "BreadReplay"}}, false});
  show.settings.rate = bc::Rate{50, 1};
  show.settings.outW = 1280;
  show.settings.outH = 720;
  show.settings.quality = br::VmxQuality::HQ;
  show.settings.lastNSeconds = 8;
  show.settings.endAction = "slate";
  show.settings.limit.hours = 2.5;
  show.settings.hotkeys["mark"] = "Space";
  show.events.lastN(100'000'000, 12'000'000, "cam1");

  const auto path = dir.path / "show.json";
  br::saveShow(path, show);
  CHECK(std::filesystem::exists(path));

  int files = 0;
  for (const auto& e : std::filesystem::directory_iterator(dir.path)) {
    ++files;
    CHECK(e.path().filename() == "show.json");
  }
  CHECK(files == 1);

  const br::Show back = br::loadShow(path);
  CHECK(back.name == show.name);
  REQUIRE(back.inputs.size() == 1);
  CHECK(back.inputs[0].name == "Observer 1");
  CHECK(back.inputs[0].input.kind == "srt");
  CHECK(back.inputs[0].input.params.at("url") == "srt://127.0.0.1:9000");
  REQUIRE(back.outputs.size() == 1);
  CHECK(back.outputs[0].id == "out1");
  CHECK(back.settings.rate == bc::Rate{50, 1});
  CHECK(back.settings.outW == 1280);
  CHECK(back.settings.outH == 720);
  CHECK(back.settings.quality == br::VmxQuality::HQ);
  CHECK(back.settings.lastNSeconds == doctest::Approx(8.0));
  CHECK(back.settings.endAction == "slate");
  REQUIRE(back.settings.limit.hours.has_value());
  CHECK(*back.settings.limit.hours == doctest::Approx(2.5));
  CHECK(back.settings.hotkeys.at("mark") == "Space");
  REQUIRE(back.events.events().size() == 1);
  CHECK(back.events.events()[0].inUs == 88'000'000);
}