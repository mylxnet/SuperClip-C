#pragma once
#include "../core/Config.h"
#include "../native/HiddenWindow.h"
#include "../native/WinUtil.h"
#include <shellapi.h>
#include <cwchar>
#include <functional>

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
