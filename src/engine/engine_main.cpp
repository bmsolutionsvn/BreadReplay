#include <windows.h>
#include <shlobj.h>

#include <ixwebsocket/IXNetSystem.h>

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/timecode.h"
#include "core/utf8.h"
#include "engine/adapter_factory.h"
#include "engine/control_server.h"
#include "engine/engine.h"
#include "engine/surface_hub.h"
#include "gpu/converter.h"
#include "gpu/device.h"
#include "gpu/surface_presenter.h"
#include "gpu/texture_pool.h"
#include "io/ffmpeg_input.h"
#include "shell/engine_discovery.h"

namespace {
std::atomic<bool> g_quit{false};

std::filesystem::path knownFolder(REFKNOWNFOLDERID id) {
  PWSTR p = nullptr;
  SHGetKnownFolderPath(id, 0, nullptr, &p);
  std::filesystem::path out(p);
  CoTaskMemFree(p);
  return out;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  // Below 44620-48715, which WSL2 mirrored networking holds on Windows (47820 failed to bind).
  int port = 38820;
  std::string token;
  std::filesystem::path showDir = knownFolder(FOLDERID_Videos) / L"BreadReplay";
  const std::filesystem::path discovery = knownFolder(FOLDERID_LocalAppData) / L"BreadReplay" / L"engine.json";
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::wstring k = argv[i], v = argv[i + 1];
    if (k == L"--port") {
      try {
        port = std::stoi(v);
      } catch (...) {
        std::wcerr << L"--port must be a whole number\n";
        return 2;
      }
    } else if (k == L"--token") {
      token = bc::toUtf8(v);
    } else if (k == L"--show-dir") {
      showDir = v;
    } else {
      std::wcerr << L"unknown argument " << k << L"\n";
      return 2;
    }
  }
  if (argc > 1 && (argc - 1) % 2 != 0) {
    std::cerr << "every option needs a value\n";
    return 2;
  }
  if (token.size() < 16 || port <= 0 || port > 65535) {
    std::cerr << "usage: breadreplay-engine --token <at least 16 chars> [--port N] [--show-dir D]\n";
    return 2;
  }

  HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\BreadReplayEngine");
  if (GetLastError() == ERROR_ALREADY_EXISTS) return 3;
  SetConsoleCtrlHandler([](DWORD) -> BOOL { g_quit = true; return TRUE; }, TRUE);
  ix::initNetSystem();

  auto gpu = bc::GpuDevice::create(bc::GpuDevice::Pref::HardwareOrWarp);
  bc::TexturePool pool(*gpu);
  bc::Converter conv(*gpu, pool);
  // Native video's start-up self-test (spec §4.3): one 16x16 surface, presented once. Status is
  // true when it works, otherwise the reason the page shows before it falls back.
  std::string nativeWhy;
  const bool nativeOk = bc::SurfacePresenter::available(*gpu, conv, &nativeWhy);
  bc::SurfaceHub surfaces(*gpu, conv);

  std::filesystem::create_directories(showDir);
  br::Show show;
  show.name = "BreadReplay";
  if (std::filesystem::exists(showDir / "show.json")) {
    try {
      show = br::loadShow(showDir / "show.json");
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[breadreplay] the show could not be opened: %s\n", e.what());
    }
  }

  bc::DefaultAdapterFactory factory(*gpu, pool);
  br::EngineDeps deps;
  deps.gpu = gpu.get();
  deps.pool = &pool;
  deps.conv = &conv;
  deps.clock = [] { return br::nowUs(); };
  deps.makeInput = [&factory, &gpu, &pool](const bc::InputConfig& cfg) -> std::unique_ptr<bc::Input> {
    if (cfg.kind == "file") {  // tests and offline shows; the UI never offers it
      bc::FfmpegInputOptions o;
      o.url = cfg.params.at("path").get<std::string>();
      o.reconnect = false;
      o.realtime = true;
      return std::make_unique<bc::FfmpegInput>(*gpu, pool, o);
    }
    return factory.makeInput(cfg);
  };
  deps.makeOutput = [&factory, &show](const bc::OutputConfig& cfg) -> std::unique_ptr<bc::Output> {
    bc::CameraConfig cam;
    cam.name = show.name;
    return factory.makeOutput(cfg, cam, show.settings.rate);
  };

  br::Engine engine(std::move(deps), show, showDir);
  bc::ControlServer server(port, token);

  std::mutex clientsMutex;
  std::set<bc::ClientId> clients;
  std::set<bc::ClientId> binaryClients;
  engine.setSurfaces(nativeOk ? &surfaces : nullptr);
  engine.setNativeVideoReason(nativeOk ? std::string{} : nativeWhy);
  engine.setBinaryWanted([&] {
    std::lock_guard lock(clientsMutex);
    return !binaryClients.empty();
  });
  engine.setSender(
      [&](const std::string& text) { server.broadcast(text); },
      [&](const std::vector<uint8_t>& data) {
        std::lock_guard lock(clientsMutex);
        for (const auto& id : binaryClients) server.sendBinary(id, data);
      });

  server.setHandlers({
      [&](const bc::ClientId& id) {
        {
          std::lock_guard lock(clientsMutex);
          clients.insert(id);
        }
        engine.requestKeyframes();
        server.send(id, engine.statusJson().dump());
        nlohmann::json e;
        e["type"] = "events";
        e["version"] = engine.events().version();
        e["events"] = engine.events().toJson();
        server.send(id, e.dump());
      },
      [&](const bc::ClientId& id) {
        {
          std::lock_guard lock(clientsMutex);
          clients.erase(id);
          binaryClients.erase(id);
        }
        surfaces.dropClient(id);
      },
      [&](const bc::ClientId& id, const std::string& text) {
        try {
          const auto cmd = nlohmann::json::parse(text);
          const std::string name = cmd.value("cmd", std::string{});
          // These two need the client id, so they cannot go through CommandRouter.
          if (name == "subscribeSurfaces") {
            if (!cmd.contains("surfaces") || !cmd["surfaces"].is_array())
              return nlohmann::json{{"ok", false}, {"error", "surfaces must be a list"}}.dump();
            if (cmd["surfaces"].size() > 64)
              return nlohmann::json{{"ok", false}, {"error", "at most 64 surfaces can be subscribed"}}.dump();
            std::vector<bc::SurfaceRequest> wanted;
            for (const auto& v : cmd["surfaces"]) {
              if (!v.is_object()) return nlohmann::json{{"ok", false}, {"error", "each surface must be an object"}}.dump();
              if (!v.contains("id") || !v["id"].is_string())
                return nlohmann::json{{"ok", false}, {"error", "id must be text"}}.dump();
              auto size = [&](const char* key) {
                if (!v.contains(key) || !v[key].is_number_integer())
                  return std::pair<bool, uint32_t>{false, 0};
                const long long n = v[key].get<long long>();
                if (n < 0) return std::pair<bool, uint32_t>{false, 0};
                return std::pair<bool, uint32_t>{true, static_cast<uint32_t>(std::min<long long>(n, 4096))};
              };
              const auto [wOk, w] = size("w");
              const auto [hOk, h] = size("h");
              if (!wOk || !hOk)
                return nlohmann::json{{"ok", false}, {"error", "w and h must be whole numbers"}}.dump();
              wanted.push_back({v["id"].get<std::string>(), w, h});
            }
            nlohmann::json out = nlohmann::json::array();
            for (const auto& g : surfaces.subscribe(id, wanted)) {
              out.push_back({{"id", g.id}, {"handle", std::to_string(g.handle)}, {"w", g.w}, {"h", g.h}});
            }
            return nlohmann::json{{"ok", true}, {"pid", GetCurrentProcessId()}, {"surfaces", out}}.dump();
          }
          if (name == "wantPreviews") {
            {
              std::lock_guard lock(clientsMutex);
              binaryClients.insert(id);
            }
            engine.requestKeyframes();  // a second fallback window gets a picture at once
            return nlohmann::json{{"ok", true}}.dump();
          }
          return engine.handle(cmd).dump();
        } catch (const std::exception& e) {
          return nlohmann::json{{"ok", false}, {"error", e.what()}}.dump();
        }
      },
  });

  try {
    server.start();
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 4;
  }
  // A relaunched shell reads this to re-attach instead of starting a second engine.
  // Write it as soon as the server listens; startup can take seconds.
  br::writeDiscovery(discovery, port, token);
  engine.start();
  std::fprintf(stderr, "[breadreplay] engine on 127.0.0.1:%d\n", port);

  while (!g_quit) std::this_thread::sleep_for(std::chrono::milliseconds(100));

  engine.stop();
  server.stop();
  std::error_code ec;
  std::filesystem::remove(discovery, ec);
  CloseHandle(single);
  ix::uninitNetSystem();
  return 0;
}