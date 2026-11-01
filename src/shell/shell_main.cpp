#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>

#include <WebView2.h>
#include <dwmapi.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <wil/com.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/utf8.h"
#include "engine/control_server.h"
#include "shell/engine_discovery.h"
#include "shell/process_launcher.h"
#include "shell/supervisor.h"

// TaskDialogIndirect exists only in Common Controls v6; without this the exe fails to start
// ("ordinal 345 could not be located").
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using Microsoft::WRL::Callback;

namespace {
constexpr UINT WM_QUIT_CONFIRMED = WM_APP + 2;
constexpr int kQuitButton = 1000;
constexpr wchar_t kClass[] = L"BreadReplayShell";

struct App {
  HWND hwnd = nullptr;
  wil::com_ptr<ICoreWebView2Controller> controller;
  wil::com_ptr<ICoreWebView2> webview;
  std::unique_ptr<bc::ProcessLauncher> launcher;
  std::unique_ptr<bc::Supervisor> supervisor;
  ix::WebSocket ws;
  std::atomic<bool> quitting{false};
  std::thread worker;
  // Below 44620-48715, which WSL2 mirrored networking holds on Windows (47820 failed to bind).
  int port = 38820;
  std::string token;
  std::filesystem::path exeDir;
} g;

std::filesystem::path localAppData() {
  PWSTR p = nullptr;
  SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p);
  std::filesystem::path out(p);
  CoTaskMemFree(p);
  return out / L"BreadReplay";
}

void showWindow() {
  ShowWindow(g.hwnd, SW_SHOW);
  ShowWindow(g.hwnd, SW_RESTORE);
  SetForegroundWindow(g.hwnd);
}

void quitAll() {
  g.quitting = true;
  if (g.supervisor) g.supervisor->stopSupervising();
  g.ws.send(R"({"id":"quit","cmd":"shutdown"})");
  for (int i = 0; i < 30 && g.launcher && g.launcher->isRunning(); ++i) Sleep(100);
  if (g.launcher && g.launcher->isRunning()) g.launcher->kill();
  DestroyWindow(g.hwnd);
}

// The X, Alt+F4 and taskbar Close ask first; the window never hides to a tray.
void confirmQuit() {
  if (g.quitting) return;
  showWindow();
  TASKDIALOGCONFIG cfg{sizeof(cfg)};
  cfg.hwndParent = g.hwnd;
  cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
  cfg.pszWindowTitle = L"BreadReplay";
  cfg.pszMainIcon = TD_WARNING_ICON;
  cfg.pszMainInstruction = L"Stop recording and quit BreadReplay?";
  cfg.pszContent = L"Recording stops when BreadReplay closes.";
  const TASKDIALOG_BUTTON buttons[] = {{kQuitButton, L"Stop and quit"}, {IDCANCEL, L"Keep running"}};
  cfg.pButtons = buttons;
  cfg.cButtons = 2;
  cfg.nDefaultButton = IDCANCEL;
  int pressed = 0;
  TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr);
  if (pressed == kQuitButton) quitAll();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCALCSIZE:
      if (wp == TRUE) {
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        const LONG top = p->rgrc[0].top;
        const LRESULT res = DefWindowProcW(hwnd, msg, wp, lp);
        p->rgrc[0].top = top + (IsZoomed(hwnd) ? GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER) : 0);
        return res;
      }
      break;
    case WM_SIZE:
      if (g.controller) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        g.controller->put_Bounds(rc);
      }
      if (g.webview) {
        g.webview->PostWebMessageAsJson(IsZoomed(hwnd) ? L"{\"type\":\"windowState\",\"maximized\":true}"
                                                       : L"{\"type\":\"windowState\",\"maximized\":false}");
      }
      return 0;
    case WM_CLOSE:
      confirmQuit();
      return 0;
    case WM_QUIT_CONFIRMED:
      quitAll();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void runtimeMissing() {
  MessageBoxW(g.hwnd,
              L"The Microsoft Edge WebView2 Runtime is missing, so the window cannot open. "
              L"The engine is still running and recording.",
              L"BreadReplay", MB_ICONWARNING);
}

void startWebView() {
  const auto userData = localAppData() / L"WebView2";
  const auto uiDir = g.exeDir / L"ui";
  const std::wstring url = L"https://app.breadreplay/index.html#port=" + std::to_wstring(g.port) +
                           L"&token=" + bc::toWide(g.token);
  CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [uiDir, url](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) {
              runtimeMissing();
              return S_OK;
            }
            env->CreateCoreWebView2Controller(
                g.hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [uiDir, url](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
                              if (FAILED(hr2) || !c) {
                                runtimeMissing();
                                return S_OK;
                              }
                              g.controller = c;
                              g.controller->get_CoreWebView2(&g.webview);
                              wil::com_ptr<ICoreWebView2Settings> settings;
                              g.webview->get_Settings(&settings);
                              if (auto s9 = settings.try_query<ICoreWebView2Settings9>()) {
                                s9->put_IsNonClientRegionSupportEnabled(TRUE);
                              }
                              RECT rc;
                              GetClientRect(g.hwnd, &rc);
                              g.controller->put_Bounds(rc);
                              if (auto wv3 = g.webview.try_query<ICoreWebView2_3>()) {
                                wv3->SetVirtualHostNameToFolderMapping(L"app.breadreplay", uiDir.c_str(),
                                                                       COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                              }
                              EventRegistrationToken tok;
                              g.webview->add_WebMessageReceived(
                                  Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                      [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        wil::unique_cotaskmem_string msg;
                                        if (FAILED(args->TryGetWebMessageAsString(&msg))) return S_OK;
                                        const std::wstring text(msg.get());
                                        if (text.find(L"quitConfirmed") != std::wstring::npos) {
                                          PostMessageW(g.hwnd, WM_QUIT_CONFIRMED, 0, 0);
                                        } else if (text.find(L"\"window\"") != std::wstring::npos) {
                                          if (text.find(L"minimize") != std::wstring::npos) ShowWindow(g.hwnd, SW_MINIMIZE);
                                          else if (text.find(L"maximize") != std::wstring::npos)
                                            ShowWindow(g.hwnd, IsZoomed(g.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                                          else if (text.find(L"close") != std::wstring::npos)
                                            PostMessageW(g.hwnd, WM_CLOSE, 0, 0);
                                        }
                                        return S_OK;
                                      })
                                      .Get(),
                                  &tok);
                              g.webview->Navigate(url.c_str());
                              return S_OK;
                            })
                            .Get());
            return S_OK;
          })
          .Get());
}

void startEngineSupervision() {
  const auto discoveryFile = localAppData() / L"engine.json";
  auto discovery = br::readDiscovery(discoveryFile);
  // A stale file can name a live process that is not our engine, or a dead pid.
  bool engineAlive = discovery && br::isEngineProcess(discovery->pid);
  if (!engineAlive && br::engineInstanceRunning()) {
    // An engine started moments before this shell may still be writing its
    // discovery file; wait for a live one instead of launching a second engine
    // that would die on the engine's single-instance mutex.
    discovery = br::waitForDiscovery(discoveryFile, std::chrono::seconds(3));
    engineAlive = discovery && br::isEngineProcess(discovery->pid);
  }
  if (engineAlive) {
    g.port = discovery->port;
    g.token = discovery->token;
  } else {
    g.token = bc::randomToken();
  }
  g.launcher = std::make_unique<bc::ProcessLauncher>(
      g.exeDir / L"breadreplay-engine.exe",
      L"--port " + std::to_wstring(g.port) + L" --token " + bc::toWide(g.token));
  if (engineAlive) g.launcher->adopt(discovery->pid);
  g.supervisor = std::make_unique<bc::Supervisor>(*g.launcher);

  g.ws.setUrl("ws://127.0.0.1:" + std::to_string(g.port) + "/?token=" + g.token);
  g.ws.setMinWaitBetweenReconnectionRetries(500);
  g.ws.setMaxWaitBetweenReconnectionRetries(1000);
  g.ws.setOnMessageCallback([](const ix::WebSocketMessagePtr& m) {
    if (m->type != ix::WebSocketMessageType::Message) return;
    try {
      // The engine echoes no request id, so any successful reply proves liveness.
      auto j = nlohmann::json::parse(m->str);
      if (br::isAliveReply(j)) g.supervisor->heartbeat(std::chrono::steady_clock::now());
    } catch (...) {
    }
  });
  g.ws.start();
  g.worker = std::thread([] {
    int n = 0;
    while (!g.quitting) {
      g.supervisor->poll(std::chrono::steady_clock::now());
      if (++n % 4 == 0) g.ws.send(R"({"id":"hb","cmd":"ping"})");
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
  });
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\BreadReplayShell");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(kClass, nullptr)) {
      ShowWindow(other, SW_SHOW);
      SetForegroundWindow(other);
    }
    return 0;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ix::initNetSystem();
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  g.exeDir = std::filesystem::path(path).parent_path();

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = inst;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  g.hwnd = CreateWindowExW(0, kClass, L"BreadReplay", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           1600, 940, nullptr, nullptr, inst, nullptr);
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(g.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  SetWindowPos(g.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  ShowWindow(g.hwnd, show);

  startEngineSupervision();
  startWebView();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  g.quitting = true;
  if (g.worker.joinable()) g.worker.join();
  g.ws.stop();
  CloseHandle(single);
  CoUninitialize();
  return 0;
}