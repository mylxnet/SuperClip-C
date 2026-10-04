#include "SystemInfo.h"

namespace sc {
namespace {

// ---- 自行声明的新符号原型（旧 SDK 头文件里没有）----
using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

constexpr int kMonitorDpiEffective = 0;                            // MDT_EFFECTIVE_DPI
constexpr DWORD kDwmwaCornerPreference = 33;                       // Win11 22H2+
constexpr DWORD kDwmwcpRound = 2;                                  // DWMWCP_ROUND

// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 定义为 (DPI_AWARENESS_CONTEXT)-4。
// 整数→指针的 reinterpret_cast 不是常量表达式（GCC 直接拒绝），只能运行期构造。
HANDLE DpiContextPerMonitorV2() { return reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)); }

HMODULE g_user32 = nullptr;
HMODULE g_shcore = nullptr;
HMODULE g_dwmapi = nullptr;
SetProcessDpiAwarenessContextFn g_setCtx = nullptr;
GetDpiForWindowFn g_getDpiForWindow = nullptr;
GetDpiForMonitorFn g_getDpiForMonitor = nullptr;
DwmSetWindowAttributeFn g_dwmSetAttr = nullptr;
bool g_resolved = false;

void ResolveOnce() {
  if (g_resolved) return;
  g_resolved = true;
  g_user32 = GetModuleHandleW(L"user32.dll");
  if (g_user32) {
    g_setCtx = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        GetProcAddress(g_user32, "SetProcessDpiAwarenessContext"));
    g_getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(g_user32, "GetDpiForWindow"));
  }
  g_shcore = LoadLibraryW(L"shcore.dll");            // Win7 上失败 → 保持空，走 GetDeviceCaps
  if (g_shcore) {
    g_getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
        GetProcAddress(g_shcore, "GetDpiForMonitor"));
  }
  g_dwmapi = LoadLibraryW(L"dwmapi.dll");
  if (g_dwmapi) {
    g_dwmSetAttr = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(g_dwmapi, "DwmSetWindowAttribute"));
  }
}

}  // namespace

DpiAwareness InitDpiAwareness() {
  ResolveOnce();
  if (g_setCtx) {
    if (g_setCtx(DpiContextPerMonitorV2())) return DpiAwareness::PerMonitorV2;
    // manifest 已声明 PerMonitorV2 时这里会失败（ERROR_ACCESS_DENIED），级别其实已生效
    if (GetLastError() == ERROR_ACCESS_DENIED) return DpiAwareness::PerMonitorV2;
  }
  if (SetProcessDPIAware()) return DpiAwareness::System;
  return DpiAwareness::Unaware;
}

UINT DpiForWindow(HWND hwnd) {
  ResolveOnce();
  if (g_getDpiForWindow && hwnd) {
    const UINT dpi = g_getDpiForWindow(hwnd);
    if (dpi) return dpi;
  }
  if (g_getDpiForMonitor && hwnd) {
    const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 0, dpiY = 0;
    if (SUCCEEDED(g_getDpiForMonitor(mon, kMonitorDpiEffective, &dpiX, &dpiY)) && dpiX) return dpiX;
  }
  const HDC dc = GetDC(hwnd);
  const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
  if (dc) ReleaseDC(hwnd, dc);
  return dpi > 0 ? UINT(dpi) : 96u;
}

void ApplyRoundedCorners(HWND hwnd) {
  ResolveOnce();
  if (!g_dwmSetAttr || !hwnd) return;                // Win10 及以下：保持直角
  const DWORD pref = kDwmwcpRound;
  g_dwmSetAttr(hwnd, kDwmwaCornerPreference, &pref, sizeof(pref));
}

bool IsHighContrast() {
  HIGHCONTRASTW hc{};
  hc.cbSize = sizeof(hc);
  if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) return false;
  return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool FitRectToDesktop(RECT& rc) {
  const HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
  if (!mon) return false;                       // 所有显示器都不挨着 = 位置失效
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) return false;
  const RECT wa = mi.rcWork;                    // 用工作区：避开任务栏与停靠条
  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  if (rc.left < wa.left) rc.left = wa.left;     // 只把露不出去的那一边推回来，
  if (rc.left + w > wa.right) rc.left = wa.right - w;   // 否则落盘位置会被抹成工作区左上角
  if (rc.top < wa.top) rc.top = wa.top;
  if (rc.top + h > wa.bottom) rc.top = wa.bottom - h;
  rc.right = rc.left + w;
  rc.bottom = rc.top + h;
  return true;
}

}  // namespace sc
