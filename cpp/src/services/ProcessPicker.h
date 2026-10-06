#pragma once
#include "../core/Config.h"
#include <functional>
#include <string>

namespace sc {

// 技术方案 §5.5（含 T2 决议）：WH_MOUSE_LL 点选绑定 + 系统级十字光标 + 超时/锁屏取消。
// 钩子安装在本进程 UI 线程，回调也在该线程执行，所以 active_/绑定态无锁读写
// （设计方案 §9.3 单线程假设：三个窗口同线程）。
class ProcessPicker {
 public:
  ProcessPicker() = default;
  ~ProcessPicker();
  ProcessPicker(const ProcessPicker&) = delete;
  ProcessPicker& operator=(const ProcessPicker&) = delete;

  // 换十字光标 → 隐藏主窗 → 装钩子 → 起 8s 超时。
  // false = 钩子/定时器没建起来且已自行复位，调用方走降级链（绑上次外部窗口）。
  bool Start(HWND mainWnd, HINSTANCE inst);
  void Cancel();                       // 六条取消路径共用；非点选态是空操作
  bool picking() const { return active_; }

  // MainWindow 只做消息路由，点选状态一律在这里落地
  void OnPickMessage(HWND target);     // WM_APP_PICK_DONE（钩子投递的根窗口）
  void OnTimeout();                    // ID_PICK：8s 没点任何东西
  void OnSessionLock();                // WM_WTSSESSION_CHANGE / WTS_SESSION_LOCK
  void OnEndSession();                 // WM_ENDSESSION（注销/关机）

  std::function<void(HWND bound, std::wstring processName)> onPicked;
  std::function<void()> onCanceled;

 private:
  static LRESULT CALLBACK LlHook(int code, WPARAM wParam, LPARAM lParam);
  LRESULT OnMouse(LPARAM lParam);
  void EndPick(bool ok, HWND target, const wchar_t* reason);
  void Cleanup();                      // 卸钩子 → 停表 → 复位光标 → 恢复主窗

  HHOOK hook_ = nullptr;
  HWND mainWnd_ = nullptr;
  bool active_ = false;
  bool cursorChanged_ = false;
  static ProcessPicker* instance_;     // LL 钩子没有用户数据参数，靠进程内单例转发
};

// 状态栏只用进程名、不含文档标题（技术方案 §5.5）：
// 根窗口 → 映像基名大写、去 .exe（EXCEL 而非 EXCEL.xlsx - Excel）。取不到返回空串，绑定仍生效。
std::wstring ProcessNameOf(HWND hwnd);

}  // namespace sc
