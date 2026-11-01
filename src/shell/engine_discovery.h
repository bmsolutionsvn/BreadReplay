#pragma once
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace br {
struct Discovery {
  DWORD pid = 0;
  int port = 0;
  std::string token;
};

// %LOCALAPPDATA%\BreadReplay\engine.json: written by the engine, read by a
// relaunched shell so it can re-attach to the engine that is still recording.
std::optional<Discovery> readDiscovery(const std::filesystem::path& file);
void writeDiscovery(const std::filesystem::path& file, int port, const std::string& token);

// True only for a live process whose image is breadreplay-engine.exe: a stale
// engine.json can name a pid that has since been reused.
bool isEngineProcess(DWORD pid);
// True when some process holds the engine's single-instance mutex.
bool engineInstanceRunning();
// Waits for a discovery file naming a live engine; nullopt at the timeout.
std::optional<Discovery> waitForDiscovery(const std::filesystem::path& file,
                                          std::chrono::milliseconds timeout);
// The shell sends only ping and shutdown; any successful reply proves the engine is alive.
bool isAliveReply(const nlohmann::json& reply);
}  // namespace br