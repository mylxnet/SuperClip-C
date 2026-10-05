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
  // v2.3.2：这里**不再**单独抬 Alt。那一次孤立 Alt↑（前面只有用户的物理 Alt↓、中间无别的键）
  // 恰好是 Excel/WPS 触发 keytip 菜单态的手势，60ms 后的 Ctrl+V 会被表格的快捷键表吞掉。
  // Alt 的处置全部收进 InjectCtrlV() 的同批注入，顺序是 Ctrl↓ → Alt↑。

  stage_ = Stage::WaitForeground;
  // C15：接力场景下目标已是前台（用户刚点进去），再走三段夺前台只会带来窗口抖动，
  // 还会撞上连贴竞态（N9）。夺前台那套只在"目标不是前台"时才需要。
  if (GetForegroundWindow() != target_ && !BringTargetToFront(target_)) {
    LogWarn(L"paste", L"三段夺前台失败，仍在当前前台发送 Ctrl+V");
  }
  if (!SetTimer(owner_, ID_FOCUS_WAIT, kFocusWaitMs, nullptr)) {
    InjectCtrlV();   // 无定时器就当场按键，绝不把阶段机留在等待态
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
  const UINT events = InjectCtrlV();
  LogInfo(L"paste", L"Ctrl+V → " + DescribeWindow(target_) + L"，前台=" + DescribeWindow(fg) +
                      L"，焦点控件=" + FocusOf(fg) + L"，事件数=" + std::to_wstring(events) +
                      L"，同批抬键=" + injected_);
  Finish(true);
}

// v2.3.1：抬 Alt/Ctrl 并进 Ctrl+V 的同一个 SendInput 批次（分两批的旧写法在 C15 接力下必挂——
// 用户手势是「按住 Alt 点」，60ms 后手指还压着，目标收到 Alt+Ctrl+V＝选择性粘贴）。
// v2.3.2：① 批次内顺序改 Ctrl↓ → Alt↑。Alt↑ 排在 Ctrl↓ 之前等于送了一次干净的 Alt 点按，
// Excel/WPS 会进 keytip 菜单态并把紧随的 Ctrl+V 当菜单加速键吞掉（现象：单元格在编辑态才贴得进）。
// ② 每个事件补扫描码（Keyboard.h 的 ScanOf），WPS 网格对 wScan=0 的注入键不认。
// Alt↑ 恒发；Ctrl↑ 仍按键态判断，注入在 WM_TIMER 里，只能读异步态。
UINT PasteService::InjectCtrlV() {
  const bool ctrl = IsCtrlDownAsync();
  injected_ = ctrl ? L"Ctrl↑+Ctrl↓→Alt↑(含扫描码)" : L"Ctrl↓→Alt↑(含扫描码)";
  return SendCtrlV(ctrl);
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
