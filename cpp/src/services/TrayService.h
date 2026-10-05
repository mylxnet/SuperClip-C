#pragma once
#include "../core/Config.h"
#include "../native/HiddenWindow.h"
#include "../native/WinUtil.h"
#include <shellapi.h>
#include <cwchar>
#include <functional>
#include <string>

namespace sc {

// FR-15：托盘常驻。纯 Win32 Shell_NotifyIcon，不引 WinForms（C1）。
class TrayService {
 public:
  TrayService() = default;
  ~TrayService() { Destroy(); }
  TrayService(const TrayService&) = delete;
  TrayService& operator=(const TrayService&) = delete;

  bool Create(HINSTANCE inst);
  void Destroy();
  HWND window() const { return win_.get(); }
  // C15：v2.2.0 为接力武装/解除新增（那时主窗是隐藏的，状态栏看不见，反馈只能走托盘气泡 NIF_INFO）。
  // v2.3.0 起**没有调用方**——接力全程不藏窗、状态栏一直在眼前；方法保留是 T5「热键被占用要可见提示」的现成落点。
  // 失败只记日志：气泡弹不出来不影响功能本身。
  void ShowBalloon(const std::wstring& title, const std::wstring& text);

  std::function<void()> onOpen;
  std::function<void()> onExit;

 private:
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  bool Add();                       // NIM_ADD（Explorer 重启后重挂同一路径）
  void HandleTrayClick(HWND hwnd, LPARAM lParam);
  void ShowContextMenu(HWND hwnd);

  HiddenWindow win_;
  HINSTANCE inst_ = nullptr;
  NOTIFYICONDATAW nid_{};
  UINT taskbarCreated_ = 0;
  bool added_ = false;
};

}  // namespace sc
