#pragma once
#include "../core/Config.h"
#include <functional>

namespace sc {

// 隐藏常驻窗口（监听 / 托盘共用）：
//   WS_POPUP + WS_EX_TOOLWINDOW，0×0 @(-32000,-32000)，**不使用 HWND_MESSAGE**
//   ——message-only 窗口收不到 TaskbarCreated 这类广播消息（技术方案 §5.1/§5.4）。
// WndProc 只把消息转交给 owner 回调，指针经 GWLP_USERDATA 传递。
class HiddenWindow {
 public:
  using Handler = std::function<LRESULT(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)>;

  HiddenWindow() = default;
  ~HiddenWindow() { Destroy(); }
  HiddenWindow(const HiddenWindow&) = delete;
  HiddenWindow& operator=(const HiddenWindow&) = delete;

  bool Create(HINSTANCE inst, const wchar_t* className, const wchar_t* title, Handler handler);
  void Destroy();

  HWND get() const { return hwnd_; }
  bool ok() const { return hwnd_ != nullptr; }

 private:
  static LRESULT CALLBACK Entry(HWND, UINT, WPARAM, LPARAM);

  HWND hwnd_ = nullptr;
  Handler handler_;
};

}  // namespace sc
