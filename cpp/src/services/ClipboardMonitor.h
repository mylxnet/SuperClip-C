#pragma once
#include "../core/Config.h"
#include "../native/HiddenWindow.h"
#include <functional>
#include <string>

namespace sc {

// FR-01：与主窗口生命周期解耦的常驻监听（收起后仍采集）。技术方案 §5.1。
class ClipboardMonitor {
 public:
  ClipboardMonitor() = default;
  ~ClipboardMonitor() { Destroy(); }
  ClipboardMonitor(const ClipboardMonitor&) = delete;
  ClipboardMonitor& operator=(const ClipboardMonitor&) = delete;

  bool Create(HINSTANCE inst);
  void Destroy();
  HWND window() const { return win_.get(); }

  std::function<void(std::wstring)> onText;
  std::function<bool()> isInternalPaste;
  std::function<std::wstring()> lastPasted;
  // 拦截到自身粘贴的那次更新后立即清标志（v2.0.2 语义），守护定时器只做兜底
  std::function<void()> clearInternalPaste;

 private:
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  void OnClipboardUpdate(HWND hwnd);
  void TryRead(HWND hwnd);

  HiddenWindow win_;
  bool listening_ = false;
  int attempt_ = 0;
};

}  // namespace sc
