#include "RelayService.h"

#include "../native/Foreground.h"
#include "../native/Keyboard.h"
#include "../util/Log.h"

namespace sc {

RelayService* RelayService::instance_ = nullptr;

RelayService::~RelayService() { Disarm(L"析构"); }

bool RelayService::Arm(HWND mainWnd, HINSTANCE inst) {
  if (!mainWnd || !inst) return false;
  const bool wasArmed = active_;
  if (wasArmed) UninstallHook();         // 重装路径：先干净卸掉，再走同一条安装流程

  instance_ = this;                      // 先立单例再装钩子（同 ProcessPicker：回调可能先到）
  mainWnd_ = mainWnd;
  hook_ = SetWindowsHookExW(WH_MOUSE_LL, &LlHook, inst, 0);
  if (!hook_) {
    LogWarn(L"relay", L"WH_MOUSE_LL 安装失败（gle=" + std::to_wstring(GetLastError()) +
                          L"），接力不可用");
    UninstallHook();
    return false;
  }
  active_ = true;
  // 重装（wasArmed）不记日志：装/卸由可见性与模式驱动，频率高，日志只留真正的状态跃迁。
  if (!wasArmed) LogInfo(L"relay", L"接力钩子就绪：Alt+左键点输入框 = 贴屏幕第一行，贴过的沉底");
  return true;
}

void RelayService::Disarm(const wchar_t* reason) {
  if (!active_) return;
  active_ = false;
  UninstallHook();
  LogInfo(L"relay", std::wstring(L"接力钩子卸下：") + reason);
}

void RelayService::UninstallHook() {
  if (hook_) {
    UnhookWindowsHookEx(hook_);          // 必须先卸钩子，再清单例指针（同 ProcessPicker §7.2）
    hook_ = nullptr;
  }
  if (instance_ == this) instance_ = nullptr;
  mainWnd_ = nullptr;
}

LRESULT CALLBACK RelayService::LlHook(int code, WPARAM wParam, LPARAM lParam) {
  // 回调必须在 LowLevelHooksTimeout 内返回：超时不是变慢，是系统摘掉钩子并吞掉这次鼠标输入。
  // 所以这里只做"取窗口 + 投递"，剪贴板与按键一律留给主线程消息。
  if (code >= 0 && instance_ && wParam == WM_LBUTTONDOWN) {
    return instance_->OnMouse(lParam);
  }
  return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT RelayService::OnMouse(LPARAM lParam) {
  const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
  if (!info || !mainWnd_) return 0;
  if (!IsAltDownAsync()) return 0;                     // 没按 Alt：这一下与我们无关，原样放行
  const DWORD now = GetTickCount();
  if (lastFire_ && DWORD(now - lastFire_) < kRelayDedupeMs) {
    return 0;                     // Alt+双击的第二次按下：只算一次接力（仍然放行点击）
  }
  // GA_ROOT 而不是 GA_ROOTOWNER：接力要贴的是"被点中的那个顶层窗"。Excel 的模态对话框
  // 用 ROOTOWNER 会退到它的主框架窗，Ctrl+V 就落到对话框外面了。
  const HWND root = GetAncestor(WindowFromPoint(info->pt), GA_ROOT);
  if (!root || IsOwnWindow(root)) return 0;            // 取不到窗口或落到自家窗：放行，绝不吞
  lastFire_ = now;
  PostMessageW(mainWnd_, WM_APP_RELAY_TRIGGER, reinterpret_cast<WPARAM>(root), 0);
  return 0;                          // 关键：返回 0 = 放行，目标输入框要拿到光标
}

}  // namespace sc
