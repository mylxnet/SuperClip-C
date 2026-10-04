#include "app/AppContext.h"
#include "core/Config.h"
#include "native/AppDirs.h"
#include "native/SystemInfo.h"
#include "util/Log.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace sc {
namespace {

AppContext* g_app = nullptr;
HANDLE g_singleInstanceMutex = nullptr;

void LogCrashCode(const wchar_t* where, DWORD code, void* address) {
  wchar_t buf[160];
  swprintf(buf, 160, L"%s code=0x%08lX addr=%p", where, code, address);
  LogFatalRaw(L"fatal", buf);
}

// 崩溃兜底顺序固定（技术方案 §4.5）：复位光标/卸钩子在 M4 接入，此处先做能做的
void CleanupOnCrash() {
  if (g_app) g_app->Shutdown();
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
  void* addr = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
  LogCrashCode(L"unhandled", code, addr);
  CleanupOnCrash();
  if (GetSystemMetrics(SM_SHUTTINGDOWN) == 0) {          // 会话关闭时不弹窗（会卡注销）
    MessageBoxW(nullptr, L"SuperClip 遇到未处理异常，已记录到 error.log。\n程序将继续尝试运行。",
                L"SuperClip", MB_OK | MB_ICONWARNING);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void TerminateHandler() {
  LogFatalRaw(L"fatal", L"未捕获的 C++ 异常，进程退出");
  CleanupOnCrash();
  std::_Exit(3);
}

#if defined(_MSC_VER)
// WndProc 主体包一层 SEH：单条消息出错不闪退（等价于 DispatcherUnhandledException）
// 本函数不含需展开的 C++ 对象，故允许使用 __try/__except
LRESULT DispatchGuarded(MSG* msg) {
  __try {
    return DispatchMessageW(msg);
  } __except (CrashFilter(GetExceptionInformation())) {
    return 0;
  }
}
#else
// MinGW 的 SEH 支持不完整；本地构建仅用于跑逻辑单测，UI 路径由 MSVC 版验证
LRESULT DispatchGuarded(MSG* msg) { return DispatchMessageW(msg); }
#endif

void ActivateExistingInstance() {
  HWND existing = FindWindowW(kMainClass, kWindowTitle);
  if (!existing) return;                                 // 老实例正在退出：静默
  if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
  else ShowWindow(existing, SW_SHOW);
  SetForegroundWindow(existing);
}

}  // namespace

}  // namespace sc

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  using namespace sc;

  const DpiAwareness awareness = InitDpiAwareness();
  LogInit(DataDir());
  const wchar_t* level = awareness == DpiAwareness::PerMonitorV2
                             ? L"PerMonitorV2"
                             : (awareness == DpiAwareness::System ? L"System" : L"Unaware");
  LogInfo(L"app", std::wstring(L"启动，DPI 感知级别 ") + level);

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);      // CoCreateGuid 依赖
  SetUnhandledExceptionFilter(&CrashFilter);
  std::set_terminate(&TerminateHandler);

  g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {           // T1：会话级单实例
    ActivateExistingInstance();
    CloseHandle(g_singleInstanceMutex);
    g_singleInstanceMutex = nullptr;
    CoUninitialize();
    LogClose();
    return 0;
  }

  AppContext ctx(inst);
  g_app = &ctx;
  if (!ctx.Initialize()) {
    g_app = nullptr;
    ctx.Shutdown();
    if (g_singleInstanceMutex) CloseHandle(g_singleInstanceMutex);
    CoUninitialize();
    LogClose();
    return 1;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchGuarded(&msg);
  }

  g_app = nullptr;
  ctx.Shutdown();
  if (g_singleInstanceMutex) CloseHandle(g_singleInstanceMutex);
  g_singleInstanceMutex = nullptr;
  CoUninitialize();
  LogClose();
  return static_cast<int>(msg.wParam);
}
