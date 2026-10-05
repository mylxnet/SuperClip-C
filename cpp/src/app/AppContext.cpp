#include "AppContext.h"
#include "../native/AppDirs.h"
#include "../native/Foreground.h"
#include "../native/SystemInfo.h"
#include "../util/Log.h"

namespace sc {

AppContext::AppContext(HINSTANCE inst)
    : inst_(inst), storage_(HistoryPath()), settingsPath_(SettingsPath()) {}

AppContext::~AppContext() { Shutdown(); }

bool AppContext::Initialize() {
  if (!storage_.EnsureDir()) LogWarn(L"app", L"数据目录不可用，本次运行不持久化");

  settings_ = LoadSettings(settingsPath_);   // 坏文件已在内部回落默认值，不会挡启动

  store_ = std::make_unique<Store>(storage_);
  store_->LoadFromDisk();
  if (store_->Corrupted()) LogWarn(L"app", L"历史文件已重置");
  ApplySettingsToStore();

  WireStoreEvents();
  WirePicker();

  window_ = std::make_unique<MainWindow>();
  if (!window_->Create(inst_, *this)) {              // 主窗是硬依赖
    LogError(L"app", L"主窗口创建失败，进程退出");
    return false;
  }
  // 落盘的快速模式在 UI 就绪后补一次「选中位钉第一行」（C14，规则本体在 Store::AnchorQuickSelection）
  store_->AnchorQuickSelection();
  RestoreBinding();

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

void AppContext::ApplySettingsToStore() {
  if (!store_) return;
  store_->SetCopyMode(settings_.splitSingleColumn ? CopyMode::TableSingleColumn : CopyMode::Normal);
  store_->SetPasteMode(settings_.pasteMode == 1 ? PasteMode::Quick : PasteMode::Normal);
  store_->SetFilter(static_cast<FilterType>(settings_.filterType));   // LoadSettings 已钳到 0..3
}

// 设计方案 §8.3：启动时按进程名尝试找回绑定窗口。
// 唯一候选才绑；多个候选按 2026-10-04 用户决议一律不自动绑（绑错窗口比不绑更糟），
// 靶心保持红色并提示重新点选。零候选（那个应用还没开）静默：进程名继续留着，下次启动再试。
void AppContext::RestoreBinding() {
  if (settings_.boundProcessName.empty() || !window_) return;
  int matches = 0;
  const HWND found = FindWindowByProcess(settings_.boundProcessName, matches);
  if (found) {
    boundWindow_ = found;
    boundProcessName_ = settings_.boundProcessName;
    LogInfo(L"pick", L"按进程名恢复绑定 " + boundProcessName_ + L"：" + DescribeWindow(found));
    RefreshUi();
    return;
  }
  if (matches > 1) {
    LogWarn(L"pick", L"进程 " + settings_.boundProcessName + L" 有 " +
                        std::to_wstring(matches) + L" 个窗口，不自动绑定");
    ShowStatusHint(settings_.boundProcessName + L" 有 " + std::to_wstring(matches) +
                   L" 个窗口，未自动绑定，请点靶心重新选择");
  }
}

void AppContext::PersistSettings() { SaveSettings(settingsPath_, settings_); }

void AppContext::SaveTopmost(bool on) {
  settings_.topmost = on;
  PersistSettings();
}

void AppContext::SavePasteMode(PasteMode mode) {
  settings_.pasteMode = static_cast<int>(mode);
  PersistSettings();
}

void AppContext::SaveFilterType(FilterType filter) {
  settings_.filterType = static_cast<int>(filter);
  PersistSettings();
}

void AppContext::SaveCopyMode(CopyMode mode) {
  settings_.splitSingleColumn = mode == CopyMode::TableSingleColumn;
  PersistSettings();
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
    RefreshUi();                                 // 主窗已被 Start 隐藏，重画在 EndPick 恢复后生效
    return;
  }
  // 技术方案 §7：钩子装不上 → 直接绑上一个外部窗口，主窗由 Start 内部恢复显示
  boundWindow_ = lastExternalWindow_ && IsWindow(lastExternalWindow_) ? lastExternalWindow_
                                                                      : nullptr;
  boundProcessName_ = boundWindow_ ? ProcessNameOf(boundWindow_) : std::wstring();
  settings_.boundProcessName = boundProcessName_;
  PersistSettings();
  statusHint_ = L"点选不可用，已绑定上次窗口";
  LogWarn(L"pick", L"降级绑定：" + (boundProcessName_.empty() ? L"<无>" : boundProcessName_));
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
void AppContext::OnSessionLock() { picker_.OnSessionLock(); }
void AppContext::OnEndSession() { picker_.OnEndSession(); }

void AppContext::WirePicker() {
  picker_.onPicked = [this](HWND bound, std::wstring name) {
    boundWindow_ = bound;
    boundProcessName_ = std::move(name);
    settings_.boundProcessName = boundProcessName_;   // §8.3：绑定目标变更即落盘进程名
    PersistSettings();
    statusHint_.clear();
    LogInfo(L"pick", L"状态栏：已绑定 " + (boundProcessName_.empty() ? L"<进程名未知>"
                                                                     : boundProcessName_));
    // 用户 2026-10-05 决议：绑完目标就该能直接空格连贴——快速模式下把选中位钉回第一行。
    // 粘贴去向仍走现有链（绑定进程 → 该进程窗口；未绑定 → 呼出前那个窗口），契约未动。
    store_->AnchorQuickSelection();
    RefreshUi();
  };
  picker_.onCanceled = [this]() { RefreshUi(); };
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
  KillTimer(window_ ? window_->get() : nullptr, ID_PICK);
  KillTimer(window_ ? window_->get() : nullptr, ID_PASTE_GUARD);
  KillTimer(window_ ? window_->get() : nullptr, ID_SEARCH);
  KillTimer(window_ ? window_->get() : nullptr, ID_STATUS_HINT);
  KillTimer(monitor_.window(), ID_READ);
  internalPaste_ = false;

  monitor_.Destroy();                                 // ① 注销剪贴板监听
  tray_.Destroy();                                    // ② 摘托盘图标
  if (store_) store_->SaveToDisk();                   // ③ 落盘（失败静默，内存态已一致）
  if (window_) {
    window_->SnapshotGeometry(settings_);             // ④ 位置/尺寸按 §8.3 在退出时存
    PersistSettings();
    window_->Destroy();                               // ⑤ 主窗（内含注销热键）
  }
}

}  // namespace sc
