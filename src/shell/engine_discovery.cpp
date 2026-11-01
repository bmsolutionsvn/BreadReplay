#include "shell/engine_discovery.h"

#include <cwctype>
#include <fstream>
#include <thread>

#include <nlohmann/json.hpp>

namespace br {
std::optional<Discovery> readDiscovery(const std::filesystem::path& file) {
  std::ifstream f(file);
  if (!f) return std::nullopt;
  try {
    auto j = nlohmann::json::parse(f);
    Discovery d{j.at("pid").get<DWORD>(), j.at("port").get<int>(), j.at("token").get<std::string>()};
    if (d.pid == 0 || d.port <= 0 || d.port > 65535 || d.token.size() < 16) return std::nullopt;
    return d;
  } catch (...) {
    return std::nullopt;
  }
}

void writeDiscovery(const std::filesystem::path& file, int port, const std::string& token) {
  std::filesystem::create_directories(file.parent_path());
  const auto tmp = file.wstring() + L".tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    f << nlohmann::json{{"pid", GetCurrentProcessId()}, {"port", port}, {"token", token}}.dump();
  }
  MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::optional<std::wstring> processImageBaseName(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return std::nullopt;
  wchar_t buf[MAX_PATH];
  DWORD size = MAX_PATH;
  const BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &size);
  CloseHandle(h);
  if (!ok) return std::nullopt;
  std::wstring name = std::filesystem::path(buf).filename().wstring();
  for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
  return name;
}

bool isEngineProcess(DWORD pid) {
  auto name = processImageBaseName(pid);
  return name && *name == L"breadreplay-engine.exe";
}

bool engineInstanceRunning() {
  HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\BreadReplayEngine");
  if (!m) return false;
  CloseHandle(m);
  return true;
}

std::optional<Discovery> waitForDiscovery(const std::filesystem::path& file,
                                          std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    if (auto d = readDiscovery(file)) return d;
    if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
}  // namespace br