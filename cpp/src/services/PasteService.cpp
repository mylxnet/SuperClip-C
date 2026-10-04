#include "PasteService.h"

#include "../native/Clipboard.h"
#include "../native/Foreground.h"
#include "../native/Keyboard.h"
#include "../util/Log.h"

#include <string>

namespace sc {

void PasteService::Start(HWND owner, HWND target, std::wstring text) {
  if (!owner || stage_ != Stage::Idle) return;   // 上一次未完成：静默丢弃，避免防护标志互踩
  owner_ = owner;
  target_ = target;
  stage_ = Stage::Write;

  if (!WriteClipboard(text)) {
    LogWarn(L"paste", L"剪贴板写入失败（被占用，重试 " + std::to_wstring(kWriteRetryMax) +
                         L" 次，gle=" + std::to_wstring(GetLastError()) + L"），本次粘贴中止");
    Finish(false);
    return;
  }

  // §5.2 表首行：没有外部目标（或目标就是自己）→ 只写剪贴板，不发键，防粘回自己
  if (!target_ || target_ == owner_ || !IsWindow(target_) || IsOwnWindow(target_)) {
    LogInfo(L"paste", L"无外部目标（未捕获或窗口已失效），只写剪贴板不发键");
    target_ = nullptr;
    Finish(true);
    return;
  }

  LogInfo(L"paste", L"目标=" + DescribeWindow(target_));
  if (IsIconic(target_)) ShowWindow(target_, SW_RESTORE);   // T4：最小化窗口 SetForegroundWindow 无效
  if (IsCtrlDown()) SendKey(VK_CONTROL, true);              // 防目标收到裸 v

  stage_ = Stage::WaitForeground;
  if (!BringTargetToFront(target_)) {
    LogWarn(L"paste", L"三段夺前台失败，仍在当前前台发送 Ctrl+V");
  }
  if (!SetTimer(owner_, ID_FOCUS_WAIT, kFocusWaitMs, nullptr)) {
    SendCtrlV();   // 无定时器就当场按键，绝不把阶段机留在等待态
    Finish(true);
  }
}

bool PasteService::WriteClipboard(const std::wstring& text) {
  for (int attempt = 0; attempt < kWriteRetryMax; ++attempt) {
    if (WriteClipboardText(owner_, text)) return true;
    if (attempt + 1 < kWriteRetryMax) Sleep(kReadRetryMs);
  }
  return false;
}

bool PasteService::BringTargetToFront(HWND target) {
  // 绝不因"前台已经是目标"而跳过：T4 里 SW_RESTORE 已把目标顶到前台，但没有这一步的
  // AllowSetForegroundWindow/SetForegroundWindow，按键仍会被上一个前台线程吞掉。
  AllowForegroundTo(WindowPid(target));
  if (SetForeground(target)) return true;
  SwitchToThis(target);
  return GetForegroundWindow() == target;
}

void PasteService::OnTimerId(UINT_PTR id) {
  if (id != ID_FOCUS_WAIT || stage_ != Stage::WaitForeground) return;
  KillTimer(owner_, ID_FOCUS_WAIT);
  // 按键落到哪个窗口只有运行期才知道，留一行 Info 供实机走查核对（AC-2/6）
  const HWND fg = GetForegroundWindow();
  if (fg != target_) LogWarn(L"paste", L"按键时前台已不是目标：" + DescribeWindow(fg));
  LogInfo(L"paste", L"Ctrl+V → " + DescribeWindow(target_) + L"，前台=" + DescribeWindow(fg) +
                      L"，焦点控件=" + FocusOf(fg) + L"，事件数=" + std::to_wstring(SendCtrlV()));
  Finish(true);
}

void PasteService::Cancel() {
  if (stage_ == Stage::WaitForeground && owner_) KillTimer(owner_, ID_FOCUS_WAIT);
  stage_ = Stage::Idle;
  target_ = nullptr;
}

void PasteService::Finish(bool ok) {
  stage_ = Stage::Idle;
  target_ = nullptr;
  if (onDone) onDone(ok);
}

}  // namespace sc
