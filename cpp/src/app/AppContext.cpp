#include "AppContext.h"
#include "../native/AppDirs.h"
#include "../native/Foreground.h"
#include "../native/SystemInfo.h"
#include "../util/Log.h"

namespace sc {

AppContext::AppContext(HINSTANCE inst) : inst_(inst), storage_(HistoryPath()) {}

AppContext::~AppContext() { Shutdown(); }

bool AppContext::Initialize() {
  if (!storage_.EnsureDir()) LogWarn(L"app", L"数据目录不可用，本次运行不持久化");

  store_ = std::make_unique<Store>(storage_);
  store_->LoadFromDisk();
  if (store_->Corrupted()) LogWarn(L"app", L"历史文件已重置");
  // 设置一律不持久化（2026-10-06 用户决议）：每次启动都是 Store 自身的默认值——
  // 全部显示 / 普通粘贴模式 / 一般复制模式，绑定目标为空，窗口由 MainWindow 默认停靠。

  WireStoreEvents();
  WirePicker();

  window_ = std::make_unique<MainWindow>();
  if (!window_->Create(inst_, *this)) {              // 主窗是硬依赖
    LogError(L"app", L"主窗口创建失败，进程退出");
    return false;
  }
  // 启动即普通模式，但快速模式下仍需「选中位钉第一行」这一契约成立，补一次（C14）
  store_->AnchorQuickSelection();
  SyncRelayHook();                             // C15：主窗已在屏，按当前（默认普通）模式裁决接力

  monitor_.onText = [this](std::wstring text) { OnClipboardText(std::move(text)); };
  monitor_.isInternalPaste = [this]() { return IsInternalPaste(); };
  monitor_.lastPasted = [this]() { return LastPasted(); };
  monitor_.clearInternalPaste = [this]() { ClearSelfPaste(); };
  monitor_.Create(inst_);                            // 失败已记日志，功能降级不致命

  tray_.onOpen = [this]() { ShowMainWindow(); };
  tray_.onExit = [this]() { Exit(); };
  tray_.Create(inst_);

  LogInfo(L"app", L"启动完成，条目 " + std::to_wstring(store_->TotalCount()) + L" 条");
  return true;
}

void AppContext::WireStoreEvents() {
  store_->onEvent = [this](const StoreEvent& event) {
    if (window_) window_->OnStoreChanged(event);
  };
}

void AppContext::OnClipboardText(std::wstring text) {
  if (shuttingDown_ || !store_) return;
  store_->AddFromClipboard(text);
}

void AppContext::BeginSelfPaste(std::wstring text) {
  internalPaste_ = true;
  lastPasted_ = std::move(text);
}

void AppContext::ClearSelfPaste() {
  internalPaste_ = false;
  lastPasted_.clear();
  KillTimer(window_ ? window_->get() : nullptr, ID_PASTE_GUARD);
}

void AppContext::ArmPasteGuard() {
  if (!window_) return;
  if (!SetTimer(window_->get(), ID_PASTE_GUARD, kPasteGuardMs, nullptr)) {
    ClearSelfPaste();                                 // 无兜底就直接解除，避免永久屏蔽采集
  }
}

void AppContext::OnPasteGuardTimer() {
  KillTimer(window_ ? window_->get() : nullptr, ID_PASTE_GUARD);
  ClearSelfPaste();                                   // C4：兜底清零两层防护
}

void AppContext::ToggleMainWindow() {
  if (!window_) return;
  if (picker_.picking()) {                       // 技术方案 §5.5：点选期间热键 = 取消
    picker_.Cancel();
    return;
  }
  if (!window_->IsVisible()) CapturePasteTarget();
  window_->ToggleVisibility();
}

void AppContext::ShowMainWindow() {
  if (!window_) return;
  if (picker_.picking()) {
    picker_.Cancel();
    return;
  }
  if (!window_->IsVisible()) CapturePasteTarget();
  window_->ShowAndFocus();
}

// 设计方案 §6.5：目标 = 用户唤起主窗前最后活跃的外部窗口。
// 隐藏主窗时不记录（那会儿前台多半就是主窗自己）。
void AppContext::CapturePasteTarget() {
  const HWND fg = GetForegroundWindow();
  if (!fg || !IsWindow(fg) || IsOwnWindow(fg)) return;
  if (window_ && fg == window_->get()) return;
  lastExternalWindow_ = fg;
  // R5：点选绑定的窗口被关掉就解绑（靶心转红、状态栏清空），目标回退为"跟随当前应用"。
  // 不自动改绑新窗口——用户没点过靶心就不算绑定（2026-10-04 决议）。
  if (boundWindow_ && !IsWindow(boundWindow_)) {
    LogInfo(L"pick", L"绑定窗口已关闭，解绑 " +
                         (boundProcessName_.empty() ? L"<进程名未知>" : boundProcessName_) +
                         L"，目标改回当前应用");
    boundWindow_ = nullptr;
    boundProcessName_.clear();
    RefreshUi();
  }
}

HWND AppContext::PasteTarget() const {
  // 用户 2026-10-04 决议：只有点选过的窗口才算绑定，且它必须仍存活；
  // 没绑定过就粘到"唤起前那个窗口"，切换应用即跟随。
  if (boundWindow_ && IsWindow(boundWindow_)) return boundWindow_;
  if (lastExternalWindow_ && IsWindow(lastExternalWindow_)) return lastExternalWindow_;
  return nullptr;
}

bool AppContext::bound() const { return boundWindow_ && IsWindow(boundWindow_); }

void AppContext::TogglePick() {
  if (!window_ || shuttingDown_) return;
  if (picker_.picking()) {                       // 再点一次靶心 = 取消本次点选
    picker_.Cancel();
    return;
  }
  statusHint_.clear();
  if (picker_.Start(window_->get(), inst_)) {
    SyncRelayHook();                             // 点选期间只留一个 LL 钩子：接力的先卸下
    RefreshUi();                                 // 主窗已被 Start 隐藏，重画在 EndPick 恢复后生效
    return;
  }
  // 技术方案 §7：钩子装不上 → 直接绑上一个外部窗口，主窗由 Start 内部恢复显示
  boundWindow_ = lastExternalWindow_ && IsWindow(lastExternalWindow_) ? lastExternalWindow_
                                                                      : nullptr;
  boundProcessName_ = boundWindow_ ? ProcessNameOf(boundWindow_) : std::wstring();
  statusHint_ = L"点选不可用，已绑定上次窗口";
  LogWarn(L"pick", L"降级绑定：" + (boundProcessName_.empty() ? L"<无>" : boundProcessName_));
  SyncRelayHook();                              // Start 内部已把主窗恢复显示，接力跟着回来
  RefreshUi();
}

void AppContext::ShowStatusHint(std::wstring text) {
  if (!window_) return;
  statusHint_ = std::move(text);
  // 定时器起不来就干脆不显示：宁可少一条提示，也不要一条永久赖在状态栏的提示
  if (!SetTimer(window_->get(), ID_STATUS_HINT, kStatusHintMs, nullptr)) statusHint_.clear();
  RefreshUi();
}

void AppContext::OnStatusHintTimer() {
  if (!window_) return;
  KillTimer(window_->get(), ID_STATUS_HINT);
  statusHint_.clear();
  RefreshUi();
}

void AppContext::CancelPick() { picker_.Cancel(); }

void AppContext::OnPickMessage(HWND target) { picker_.OnPickMessage(target); }
void AppContext::OnPickTimeout() { picker_.OnTimeout(); }
void AppContext::OnSessionLock() {
  picker_.OnSessionLock();
  // 钩子绝不跨会话残留：锁屏一律卸，解锁再由 SyncRelayHook 按当前模式与可见性装回来。
  relay_.Disarm(L"会话锁屏");
}
void AppContext::OnSessionUnlock() { SyncRelayHook(); }
void AppContext::OnEndSession() {
  picker_.OnEndSession();
  relay_.Disarm(L"注销/关机");
}

// ============ C15 无开关接力（v2.3.0，2026-10-05 用户决议）============
// 接力不再有"开启/关闭"这一步：能看见列表（主窗在屏 + 快速模式）就能 Alt+左键贴第一行。
// 因此装/卸是状态裁决而非用户动作——每个能改变这两个条件的入口都必须回来同步一次。
// 点选期间让位：ProcessPicker 自己挂着一个 WH_MOUSE_LL，两个钩子叠挂没有意义。
void AppContext::SyncRelayHook() {
  if (!window_ || shuttingDown_ || !store_) return;
  const bool want = store_->pasteMode() == PasteMode::Quick && window_->IsVisible() &&
                    !picker_.picking();
  if (!want) {
    relay_.Disarm(L"快速模式关闭、主窗已收起或点选进行中");
    return;
  }
  if (!relay_.Arm(window_->get(), inst_)) {
    LogWarn(L"relay", L"武装失败，Alt+左键不可用");
    ShowStatusHint(L"接力装不起来（鼠标钩子不可用），空格粘贴不受影响");
  }
}

void AppContext::OnRelayTrigger(HWND target) {
  if (!relay_.armed() || shuttingDown_) return;   // 迟到的投递（已解除）一律忽略
  RelayStep(target);
}

void AppContext::OnRelayHotkey(HWND foreground) {
  if (!relay_.armed() || shuttingDown_) return;
  RelayStep(foreground);
}

void AppContext::RelayStep(HWND target) {
  if (!window_ || !store_) return;
  if (!target || !IsWindow(target) || IsOwnWindow(target)) {
    LogWarn(L"relay", L"目标无效或就是本进程窗口，本次不动作：" + DescribeWindow(target));
    return;
  }
  const ClipItem* item = store_->RelayNext();
  if (!item) {
    // v2.3.0：贴完不卸钩、不算失败——用户随时复制一条新的回来，第一条永远是新的那条。
    LogInfo(L"relay", L"第一行已是灰条（没有未粘贴的条目），本次不动作：" + DescribeWindow(target));
    ShowStatusHint(L"没有未粘贴的条目，复制新的内容即可继续");
    return;
  }
  const auto idx = store_->IndexOfDisplay(item);
  LogInfo(L"relay", L"贴第 " + std::to_wstring(idx ? long(*idx) + 1 : 0) + L" 条 → " +
                       DescribeWindow(target));
  window_->PasteForRelay(item, target);
}

void AppContext::WirePicker() {
  picker_.onPicked = [this](HWND bound, std::wstring name) {
    boundWindow_ = bound;
    boundProcessName_ = std::move(name);              // 仅内存态：不落盘，下次启动不恢复
    statusHint_.clear();
    LogInfo(L"pick", L"状态栏：已绑定 " + (boundProcessName_.empty() ? L"<进程名未知>"
                                                                     : boundProcessName_));
    // 用户 2026-10-05 决议：绑完目标就该能直接空格连贴——快速模式下把选中位钉回第一行。
    // 粘贴去向仍走现有链（绑定进程 → 该进程窗口；未绑定 → 呼出前那个窗口），契约未动。
    store_->AnchorQuickSelection();
    SyncRelayHook();        // 点选结束、主窗已由 Cleanup 恢复显示 → 接力重新裁决
    RefreshUi();
  };
  picker_.onCanceled = [this]() {
    SyncRelayHook();        // 六条取消路径都从这里回来，只在这一处补裁决
    RefreshUi();
  };
}

void AppContext::RefreshUi() {
  if (window_) window_->Repaint();
}

void AppContext::Exit() {
  Shutdown();
  PostQuitMessage(0);
}

void AppContext::Shutdown() {
  if (shuttingDown_) return;
  shuttingDown_ = true;                               // 先置位：后续消息回调一律早退

  picker_.Cancel();                                   // §7.2 ①：卸钩子 + SPI_SETCURSORS
  relay_.Disarm(L"退出清理");                          // C15：接力钩子同样必须在主窗销毁前卸掉
  KillTimer(window_ ? window_->get() : nullptr, ID_PICK);
  KillTimer(window_ ? window_->get() : nullptr, ID_PASTE_GUARD);
  KillTimer(window_ ? window_->get() : nullptr, ID_SEARCH);
  KillTimer(window_ ? window_->get() : nullptr, ID_STATUS_HINT);
  KillTimer(monitor_.window(), ID_READ);
  internalPaste_ = false;

  monitor_.Destroy();                                 // ① 注销剪贴板监听
  tray_.Destroy();                                    // ② 摘托盘图标
  if (store_) store_->SaveToDisk();                   // ③ 落盘（失败静默，内存态已一致）
  if (window_) window_->Destroy();                    // ④ 主窗（内含注销热键）
}

}  // namespace sc
