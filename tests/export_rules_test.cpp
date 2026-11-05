#include <doctest/doctest.h>

#include <fstream>

#include "export/export_rules.h"
#include "support/temp_dir.h"

TEST_CASE("Export rules: presets scale with pixels and frame rate") {
  using br::ExportCodec, br::ExportQuality;
  CHECK(br::exportBitrate(ExportCodec::H264, ExportQuality::High, {}, 1920, 1080, {30, 1}) == 20'000'000);
  CHECK(br::exportBitrate(ExportCodec::H264, ExportQuality::High, {}, 1920, 1080, {60, 1}) == 40'000'000);
  CHECK(br::exportBitrate(ExportCodec::Hevc, ExportQuality::Small, {}, 1280, 720, {30, 1}) == 1'333'333);
  CHECK(br::exportBitrate(ExportCodec::Hevc, ExportQuality::Medium, {}, 1920, 1080, {60000, 1001}) == 11'988'012);
  CHECK(br::exportBitrate(ExportCodec::H264, ExportQuality::Custom, 8.5, 1920, 1080, {30, 1}) == 8'500'000);
  CHECK_THROWS_AS(br::exportBitrate(ExportCodec::H264, ExportQuality::Custom, 0.0, 1920, 1080, {30, 1}), std::invalid_argument);
}

TEST_CASE("Export rules: names are safe and never overwrite") {
  bc::test::TempDir dir;
  std::tm t{}; t.tm_year = 126; t.tm_mon = 10; t.tm_mday = 5; t.tm_hour = 21; t.tm_min = 7;
  CHECK(br::exportPath(dir.path, "Game 1: Final?", t).filename() == "Game 1_ Final_ - 2026-11-05 2107.mp4");
  std::ofstream(dir.path / "Game 1 - 2026-11-05 2107.mp4") << "x";
  std::ofstream(dir.path / "Game 1 - 2026-11-05 2107 (2).mp4.part") << "x";
  CHECK(br::exportPath(dir.path, "Game 1", t).filename() == "Game 1 - 2026-11-05 2107 (3).mp4");
  CHECK(br::sanitizeFileName(" . ") == "Export");
}