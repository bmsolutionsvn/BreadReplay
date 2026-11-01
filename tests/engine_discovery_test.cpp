#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>
#include <fstream>

#include "shell/engine_discovery.h"

namespace {
struct TempDir {
  std::filesystem::path path;
  TempDir()
      : path(std::filesystem::temp_directory_path() /
             ("br-discovery-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()))) {
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};
}  // namespace

TEST_CASE("discovery round-trips pid, port and token") {
  TempDir dir;
  const auto file = dir.path / "engine.json";
  br::writeDiscovery(file, 49123, "0123456789abcdef");
  const auto got = br::readDiscovery(file);
  REQUIRE(got.has_value());
  CHECK(got->pid == GetCurrentProcessId());
  CHECK(got->port == 49123);
  CHECK(got->token == "0123456789abcdef");
}

TEST_CASE("a discovery file naming a live non-engine process is not adopted") {
  TempDir dir;
  const auto file = dir.path / "engine.json";
  br::writeDiscovery(file, 49123, "0123456789abcdef");
  const auto got = br::readDiscovery(file);
  REQUIRE(got.has_value());
  // The test runner is alive but is not breadreplay-engine.exe, so the shell's
  // identity gate must refuse this stale file.
  CHECK_FALSE(br::isEngineProcess(got->pid));
}

TEST_CASE("a torn or empty discovery file is ignored") {
  TempDir dir;
  const auto file = dir.path / "engine.json";
  {
    std::ofstream f(file, std::ios::trunc);
    f << R"({"pid":)";
  }
  CHECK_FALSE(br::readDiscovery(file).has_value());
  {
    std::ofstream f(file, std::ios::trunc);
  }
  CHECK_FALSE(br::readDiscovery(file).has_value());
}