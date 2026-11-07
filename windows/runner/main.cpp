#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>

#include <shobjidl.h>
#include <propsys.h>
#include <propkey.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>

#include "flutter_window.h"
#include "render_engine.h"
#include "utils.h"

namespace {

// TEMPORARY DIAGNOSTIC (app-exit freeze): 与 flutter_window.cpp 的
// ShutdownTrace 同格式、同时钟、同一个文件，用来把「消息循环退出 / COM
// 反初始化 / CRT 退出」这三段接在窗口销毁（C9）之后。定位后删除。
void ExitTrace(const char* tag) {
  wchar_t dir[MAX_PATH];
  if (::GetTempPathW(MAX_PATH, dir) == 0) return;
  std::wstring file(dir);
  file += L"md3music_shutdown.log";
  std::ofstream out(file.c_str(), std::ios::app);
  if (!out) return;
  out << std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
             .count()
      << ' ' << tag << '\n';
}

// AppUserModelID：Shell 用它把 SMTC 媒体会话、任务栏、通知归属到应用身份。
// 未打包（非 MSIX）的 Win32 exe 不设置 AUMID 时，播控中心会显示
// 「未知应用」+ 默认灰色图标。取与 Runner.rc CompanyName 一致的
// com.md3music 域名前缀，保持稳定不再变更（变更会使旧会话身份失效）。
constexpr wchar_t kAppUserModelId[] = L"com.md3music.md3music";

// SMTC 解析 AUMID → 应用名/图标时，需要在开始菜单里存在一条携带
// System.AppUserModel.ID 属性的快捷方式（名称即应用显示名，图标默认
// 取 exe 内嵌图标）。便携分发没有安装器，这里运行时幂等补建：
// 已存在且 target 指向当前 exe 时直接跳过；exe 位置变化（debug↔release、
// 移动目录）后下次启动自动改写。任何失败静默降级 —— 只影响播控中心的
// 名称/图标显示，不影响 SMTC 功能本身（与 media_controls 的容错口径一致）。
void EnsureStartMenuAumidShortcut() {
  wchar_t appdata[MAX_PATH] = {};
  const DWORD len = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return;

  const std::wstring shortcut_path =
      std::wstring(appdata) +
      L"\\Microsoft\\Windows\\Start Menu\\Programs\\MD3Music.lnk";

  wchar_t exe_path[MAX_PATH] = {};
  const DWORD exe_len =
      ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (exe_len == 0 || exe_len >= MAX_PATH) return;

  IShellLinkW* link = nullptr;
  IPersistFile* file = nullptr;
  IPropertyStore* store = nullptr;
  HRESULT hr = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&link));
  if (FAILED(hr) || link == nullptr) return;
  hr = link->QueryInterface(IID_PPV_ARGS(&file));
  if (FAILED(hr) || file == nullptr) {
    link->Release();
    return;
  }

  do {
    // 已存在且仍指向当前 exe：无需改写（避免每次启动重写触发 Shell 重索引）。
    // Load 失败（首启/损坏）走下方完整创建路径。
    WCHAR existing[MAX_PATH] = {};
    if (SUCCEEDED(file->Load(shortcut_path.c_str(), STGM_READ)) &&
        SUCCEEDED(link->GetPath(existing, MAX_PATH, nullptr, 0)) &&
        _wcsicmp(existing, exe_path) == 0) {
      break;
    }

    hr = link->SetPath(exe_path);
    if (FAILED(hr)) break;
    // 工作目录取 exe 所在目录（去掉文件名部分）。
    std::wstring dir(exe_path);
    const size_t slash = dir.find_last_of(L"\\/");
    hr = link->SetWorkingDirectory(
        slash == std::wstring::npos ? exe_path : dir.substr(0, slash).c_str());
    if (FAILED(hr)) break;

    hr = link->QueryInterface(IID_PPV_ARGS(&store));
    if (FAILED(hr) || store == nullptr) break;
    PROPVARIANT value;
    ::PropVariantInit(&value);
    value.vt = VT_LPWSTR;
    value.pwszVal = const_cast<wchar_t*>(kAppUserModelId);
    // SetValue 会拷贝值；pwszVal 指向静态常量，绝不能 PropVariantClear
    // （它会对 pwszVal 执行 CoTaskMemFree，释放静态存储 → 堆损坏崩溃）。
    hr = store->SetValue(PKEY_AppUserModel_ID, value);
    value.vt = VT_EMPTY;
    value.pwszVal = nullptr;
    if (FAILED(hr)) break;
    hr = store->Commit();
    if (FAILED(hr)) break;

    hr = file->Save(shortcut_path.c_str(), TRUE);
  } while (false);

  if (store) store->Release();
  if (file) file->Release();
  if (link) link->Release();
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  // TEMPORARY DIAGNOSTIC (app-exit crash): 退出时进程以 0xC0000409 结束
  // （ucrtbase 里的 abort/fail-fast），默认不留任何信息。这三个 handler 会在
  // abort 之前把原因写进日志。定位后删除。
  std::set_terminate([]() {
    ExitTrace("T-std-terminate");
    if (auto eptr = std::current_exception()) {
      try {
        std::rethrow_exception(eptr);
      } catch (const std::exception& e) {
        ExitTrace(e.what());
      } catch (...) {
        ExitTrace("T-unknown-exception");
      }
    }
    std::abort();
  });
  _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*,
                                    const wchar_t*, unsigned int, uintptr_t) {
    ExitTrace("T-invalid-parameter");
  });
  _set_purecall_handler([]() { ExitTrace("T-purecall"); });

  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  // 必须记返回值：RPC_E_CHANGED_MODE 表示该线程已被初始化成另一种套间模型，
  // 此时引用计数没有增加，后面再 CoUninitialize 就会减掉不属于自己的那一份，
  // 让仍在使用的 COM 对象（SMTC / 任务栏按钮）工作在已释放的状态上。
  const HRESULT com_init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  // AUMID 必须在创建窗口 / 初始化 SMTC 之前设置，进程级一次即可。
  ::SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
  EnsureStartMenuAumidShortcut();

  flutter::DartProject project(L"data");
  // 播放页歌词是整屏重绘，Impeller-on-Windows 在这个场景会把光栅线程压满
  // （实测播放时 GPU 70%+、窗口缩小即恢复流畅、CPU 仍有富余）。
  // 引擎开关与「关于」页显示的引擎名同源，都由 render_engine.h 决定。
#if !MD3MUSIC_USE_IMPELLER
  project.set_impeller_switch(flutter::ImpellerSwitch::Disabled);
#endif

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(1280, 720);
  if (!window.Create(L"md3Music", origin, size)) {
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  ExitTrace("C10-loop-exited");
  if (SUCCEEDED(com_init)) {
    ::CoUninitialize();
  }
  ExitTrace("C11-com-uninitialized");
  // atexit 注册的回调在 CRT 静态析构之前执行，此时 ofstream 仍可用；
  // 再往后（DLL 卸载，含 kugou_server.dll 的 Rust 运行时）就打不到了。
  std::atexit([]() { ExitTrace("C12-atexit"); });
  return EXIT_SUCCESS;
}
