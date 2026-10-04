#include "ClipboardMonitor.h"
#include "../native/Clipboard.h"
#include "../util/Log.h"

namespace sc {

bool ClipboardMonitor::Create(HINSTANCE inst) {
  if (!win_.Create(inst, kMonitorClass, kMonitorClass,
                  [this](HWND hwnd, UINT msg, WPARAM w, LPARAM l) { return Handle(hwnd, msg, w, l); })) {
    LogError(L"monitor", L"监听窗口创建失败");
    return false;
  }
  if (!AddClipboardFormatListener(win_.get())) {
    // 降级而非致命：手动复制粘贴仍可用，只是不自动入列（§5.1）
    LogWarn(L"monitor", L"AddClipboardFormatListener 失败，自动采集不可用");
    return true;
  }
  listening_ = true;
  return true;
}

void ClipboardMonitor::Destroy() {
  if (listening_) {
    RemoveClipboardFormatListener(win_.get());
    listening_ = false;
  }
  win_.Destroy();
}

LRESULT ClipboardMonitor::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case kMsgClipboardUpdate:
      OnClipboardUpdate(hwnd);
      return 0;
    case WM_TIMER:
      if (wParam == ID_READ) { KillTimer(hwnd, ID_READ); TryRead(hwnd); }
      return 0;
    case WM_DESTROY:
      if (listening_) { RemoveClipboardFormatListener(hwnd); listening_ = false; }
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

void ClipboardMonitor::OnClipboardUpdate(HWND hwnd) {
  if (isInternalPaste && isInternalPaste()) {
    if (clearInternalPaste) clearInternalPaste();      // 自身粘贴写剪贴板：拦一次并即刻解除
    return;
  }
  attempt_ = 1;
  TryRead(hwnd);                                       // 立即读；失败才由 Busy 分支装重试定时器
}

void ClipboardMonitor::TryRead(HWND hwnd) {
  std::wstring text;
  const ClipRead result = ReadClipboardText(hwnd, text);
  if (result == ClipRead::Busy) {
    if (attempt_ < kReadRetryMax) {                    // Excel 等源程序复制瞬间独占剪贴板
      ++attempt_;
      SetTimer(hwnd, ID_READ, kReadRetryMs, nullptr);
    } else {
      LogWarn(L"monitor", L"剪贴板连续占用，读取失败");
    }
    return;
  }
  if (result != ClipRead::Ok) return;                  // NotText / Empty：不重试也不入列

  if (lastPasted) {
    const std::wstring previous = lastPasted();
    if (!previous.empty() && previous == text) return;  // 第二层：自粘贴回环兜底
  }
  if (onText) onText(std::move(text));
}

}  // namespace sc
