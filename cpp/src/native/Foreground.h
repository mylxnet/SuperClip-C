#pragma once
#include "../core/Config.h"
#include <windows.h>
#include <string>

// 技术方案 §5.2 三段夺前台的原子操作。这里只提供无状态单步调用，
// 阶段编排与降级都在 services/PasteService。
namespace sc {

// 日志用：类名|标题，把 GetForegroundWindow() 的结果落成可读的一行
inline std::wstring DescribeWindow(HWND hwnd) {
  if (!hwnd) return L"null";
  wchar_t cls[96] = L"", title[96] = L"";
  GetClassNameW(hwnd, cls, 96);
  GetWindowTextW(hwnd, title, 96);
  return std::wstring(cls) + L"|" + title;
}

// 按键真正会落到哪个控件：前台窗口的线程焦点。粘贴失败排查全靠它。
inline std::wstring FocusOf(HWND hwnd) {
  if (!hwnd) return L"null";
  GUITHREADINFO gi{};
  gi.cbSize = sizeof(gi);
  if (!GetGUIThreadInfo(GetWindowThreadProcessId(hwnd, nullptr), &gi)) return L"query-failed";
  return DescribeWindow(gi.hwndFocus);
}

inline DWORD WindowPid(HWND hwnd) {
  DWORD pid = 0;
  if (hwnd) GetWindowThreadProcessId(hwnd, &pid);
  return pid;
}

// 本进程的全部窗口（主窗 / 隐藏 monitor / 托盘宿主 / 气泡）都不是粘贴目标
inline bool IsOwnWindow(HWND hwnd) {
  if (!hwnd) return false;
  return WindowPid(hwnd) == GetCurrentProcessId();
}

// ① 把前台权让给目标进程（绕 Windows 前台锁的官方入口）
inline bool AllowForegroundTo(DWORD pid) { return AllowSetForegroundWindow(pid) != FALSE; }

// ② 置前台 + 置活动窗口
inline bool SetForeground(HWND hwnd) {
  const BOOL set = SetForegroundWindow(hwnd);
  SetActiveWindow(hwnd);
  return set != FALSE;
}

// ③ 半公开兜底：原 R9 行为，仅在前两段失败时生效
inline void SwitchToThis(HWND hwnd) { SwitchToThisWindow(hwnd, TRUE); }

}  // namespace sc
