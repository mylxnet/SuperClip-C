#include "HiddenWindow.h"

namespace sc {

LRESULT CALLBACK HiddenWindow::Entry(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  HiddenWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = cs ? static_cast<HiddenWindow*>(cs->lpCreateParams) : nullptr;
    if (self) {
      self->hwnd_ = hwnd;                    // 让 WM_NCCREATE 之后的回调就能取到句柄
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
  } else {
    self = reinterpret_cast<HiddenWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self && self->handler_) return self->handler_(hwnd, msg, wParam, lParam);
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool HiddenWindow::Create(HINSTANCE inst, const wchar_t* className, const wchar_t* title,
                          Handler handler) {
  Destroy();
  handler_ = std::move(handler);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &HiddenWindow::Entry;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = className;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    handler_ = nullptr;
    return false;
  }

  // 禁 HWND_MESSAGE：托盘需要 TaskbarCreated 广播（技术方案 §5.1/§5.4）
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, className, title, WS_POPUP,
                          -32000, -32000, 0, 0, nullptr, nullptr, inst, this);
  if (!hwnd_) handler_ = nullptr;
  return hwnd_ != nullptr;
}

void HiddenWindow::Destroy() {
  if (!hwnd_) return;
  HWND hwnd = hwnd_;
  hwnd_ = nullptr;                       // 先摘，避免回调里重入
  DestroyWindow(hwnd);
  handler_ = nullptr;
}

}  // namespace sc
