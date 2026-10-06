#include "ProcessPicker.h"

#include "../native/Foreground.h"
#include "../util/Log.h"

#include <vector>

namespace sc {

ProcessPicker* ProcessPicker::instance_ = nullptr;

ProcessPicker::~ProcessPicker() { Cancel(); }

bool ProcessPicker::Start(HWND mainWnd, HINSTANCE inst) {
  if (active_ || !mainWnd || !inst) return false;
  mainWnd_ = mainWnd;

  // 系统级把箭头换成十字：失败只是没有特效，点选功能照旧（技术方案 §7 降级表）
  cursorChanged_ = SetSystemCursor(LoadCursorW(nullptr, IDC_CROSS), kOcrNormal) != FALSE;
  if (!cursorChanged_) LogWarn(L"pick", L"SetSystemCursor 失败，点选继续但光标不变十字");

  ShowWindow(mainWnd_, SW_HIDE);                 // 主窗让位，否则它自己就是点选目标
  if (!SetTimer(mainWnd_, ID_PICK, kPickTimeoutMs, nullptr)) {
    LogWarn(L"pick", L"超时定时器创建失败，本次点选中止");
    Cleanup();
    return false;
  }

  instance_ = this;
  hook_ = SetWindowsHookExW(WH_MOUSE_LL, &LlHook, inst, 0);
  if (!hook_) {
    instance_ = nullptr;
    LogWarn(L"pick", L"WH_MOUSE_LL 安装失败，降级为绑定上次外部窗口");
    Cleanup();
    return false;                                // 降级链由调用方执行，服务自己不越权改绑定
  }
  active_ = true;
  LogInfo(L"pick", L"点选开始：十字光标 + 低层钩子就绪，" +
                        std::to_wstring(kPickTimeoutMs) + L"ms 内未点击则自动取消");
  return true;
}

void ProcessPicker::Cancel() {
  if (active_) EndPick(false, nullptr, L"用户取消");
}

void ProcessPicker::OnTimeout() {
  if (active_) EndPick(false, nullptr, L"8s 超时");
}

void ProcessPicker::OnSessionLock() {
  if (active_) EndPick(false, nullptr, L"会话锁屏");
}

void ProcessPicker::OnEndSession() {
  if (active_) EndPick(false, nullptr, L"会话结束");
}

void ProcessPicker::OnPickMessage(HWND target) {
  if (!active_) return;                          // 迟到的投递（已取消或已完成）忽略
  if (!target || !IsWindow(target)) {
    EndPick(false, nullptr, L"目标窗口无效");
    return;
  }
  EndPick(true, target, L"点选命中");
}

void ProcessPicker::EndPick(bool ok, HWND target, const wchar_t* reason) {
  if (!active_) return;                          // 幂等：六条路径可能重叠触发
  active_ = false;
  std::wstring name;
  if (ok) {
    name = ProcessNameOf(target);                // 主线程才查进程名，钩子里绝不做慢调用
    LogInfo(L"pick", std::wstring(reason) + L"，绑定 " +
                         (name.empty() ? L"<进程名未知>" : name) + L" ← " + DescribeWindow(target));
  } else {
    LogInfo(L"pick", std::wstring(L"点选取消（") + reason + L"），保留原绑定");
  }
  Cleanup();                                     // 复位完成后才回调，状态栏读到的一定是新绑定
  if (ok) {
    if (onPicked) onPicked(target, std::move(name));
  } else if (onCanceled) {
    onCanceled();
  }
}

void ProcessPicker::Cleanup() {
  if (hook_) {
    UnhookWindowsHookEx(hook_);                  // 必须先卸钩子，再清单例指针
    hook_ = nullptr;
  }
  if (instance_ == this) instance_ = nullptr;
  KillTimer(mainWnd_, ID_PICK);
  if (cursorChanged_) {
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);   // 一键复位，杜绝十字残留
    cursorChanged_ = false;
  }
  if (mainWnd_ && IsWindow(mainWnd_)) ShowWindow(mainWnd_, SW_SHOW);   // 不抢前台
}

LRESULT CALLBACK ProcessPicker::LlHook(int code, WPARAM wParam, LPARAM lParam) {
  // 只截左键按下；移动与抬起原样放行。钩子必须在 LowLevelHooksTimeout 内返回，
  // 所以这里只做窗口查找 + 投递，进程名解析留给主线程。
  if (code >= 0 && instance_ && wParam == WM_LBUTTONDOWN) {
    const LRESULT swallow = instance_->OnMouse(lParam);
    if (swallow != 0) return swallow;
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT ProcessPicker::OnMouse(LPARAM lParam) {
  const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
  if (!info || !mainWnd_) return 0;
  // GA_ROOT：忽略目标程序内部子控件，绑的是顶层窗口
  const HWND root = GetAncestor(WindowFromPoint(info->pt), GA_ROOT);
  if (!root) return 0;                                 // 取不到窗口就放行，绝不吞点击
  if (root == mainWnd_ || IsOwnWindow(root)) return 1; // 点到自己（气泡/宿主窗）：吞掉，仍在点选
  PostMessageW(mainWnd_, WM_APP_PICK_DONE, reinterpret_cast<WPARAM>(root), 0);
  return 1;   // T2：吞掉本次点击，目标选区/按钮状态不被改动（代价是该点击不再激活窗口）
}

std::wstring ProcessNameOf(HWND hwnd) {
  DWORD pid = 0;
  if (!hwnd) return {};
  GetWindowThreadProcessId(hwnd, &pid);
  if (!pid) return {};

  // 只查映像路径 → LIMITED 权限即可；受保护进程读不到就是空串，不重试也不影响绑定
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) {
    LogWarn(L"pick", L"OpenProcess 失败 pid=" + std::to_wstring(pid) + L"，进程名按未知处理");
    return {};
  }
  std::wstring name;
  std::vector<wchar_t> buf(4096);
  DWORD len = DWORD(buf.size());
  if (QueryFullProcessImageNameW(proc, 0, buf.data(), &len) && len > 0) {
    std::wstring path(buf.data(), len);
    const size_t slash = path.find_last_of(L"\\/");
    name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name.erase(dot);   // EXCEL 而非 EXCEL.EXE
    CharUpperW(&name[0]);                             // 整串转大写
  } else {
    LogWarn(L"pick", L"QueryFullProcessImageNameW 失败 pid=" + std::to_wstring(pid));
  }
  CloseHandle(proc);
  return name;
}

}  // namespace sc
