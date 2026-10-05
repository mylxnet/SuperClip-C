#pragma once
#include "../core/Config.h"
#include <windows.h>

namespace sc {

// C15 填表接力（v2.3.0 起为**无开关**接力，2026-10-05 用户决议）：
// 列表窗在屏且处于快速模式时，Alt+左键点目标程序的输入框，就把「屏幕上的第一行」
// 贴进被点中的那个窗口；贴过的沉底（C8），下一条自动上位（C14）。
//
// 与点选绑定（ProcessPicker）同为 WH_MOUSE_LL，但语义**相反**，别照抄：
//   点选 = 吞掉这一击（不能改动用户当前选区），命中根窗口后结束；
//   接力 = 放行这一击（输入框必须拿到光标），命中后只投递消息、继续挂着。
// 本类只管钩子机制；"装不装、贴哪条、提示什么"全在 AppContext（装/卸由 SyncRelayHook 裁决）。
// 钩子装在本进程 UI 线程，回调也在该线程执行，所以 active_ 无锁读写（设计方案 §9.3 单线程假设）。
class RelayService {
 public:
  RelayService() = default;
  ~RelayService();
  RelayService(const RelayService&) = delete;
  RelayService& operator=(const RelayService&) = delete;

  // 幂等：没挂就挂上，已经挂着就**卸掉重挂**。重装不是浪费——低层钩子被系统按
  // LowLevelHooksTimeout 静默摘掉后，没有任何 API 能查出"我还有钩子吗"，症状只是
  // "Alt+点不灵且不报错"，所以每次状态确认都重挂一次是唯一能自愈的做法。
  // false = 装不上（已自行复位），降级提示由调用方决定。
  bool Arm(HWND mainWnd, HINSTANCE inst);
  void Disarm(const wchar_t* reason);
  bool armed() const { return active_; }

 private:
  static LRESULT CALLBACK LlHook(int code, WPARAM wParam, LPARAM lParam);
  LRESULT OnMouse(LPARAM lParam);
  void UninstallHook();

  HHOOK hook_ = nullptr;
  HWND mainWnd_ = nullptr;
  bool active_ = false;
  DWORD lastFire_ = 0;                  // Alt+双击的第二次按下不重复贴（kRelayDedupeMs 内忽略）
  static RelayService* instance_;       // LL 钩子没有用户数据参数，靠进程内单例转发
};

}  // namespace sc
