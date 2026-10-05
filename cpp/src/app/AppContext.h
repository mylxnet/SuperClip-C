#pragma once
#include "../core/Settings.h"
#include "../core/Store.h"
#include "../services/ClipboardMonitor.h"
#include "../services/ProcessPicker.h"
#include "../services/RelayService.h"
#include "../services/StorageService.h"
#include "../services/TrayService.h"
#include "../ui/MainWindow.h"

namespace sc {

// 组合根：持有全部服务与主窗，负责初始化顺序与固定顺序的清理链（N3）。
// 依赖方向：AppContext → services/core/ui；下层不得反向持有 AppContext 之外的东西。
class AppContext {
 public:
  explicit AppContext(HINSTANCE inst);
  ~AppContext();
  AppContext(const AppContext&) = delete;
  AppContext& operator=(const AppContext&) = delete;

  bool Initialize();          // 失败已写日志，调用方直接退出
  void Shutdown();            // 固定顺序清理（可重复调用）

  // 采集与粘贴
  void OnClipboardText(std::wstring text);
  bool IsInternalPaste() const { return internalPaste_; }
  const std::wstring& LastPasted() const { return lastPasted_; }
  void BeginSelfPaste(std::wstring text);      // M4 粘贴前调用
  void ClearSelfPaste();
  void OnPasteGuardTimer();

  // 窗口编排
  void ToggleMainWindow();
  void ShowMainWindow();
  void Exit();                                   // 托盘「退出」/ 标题栏 ✕
  void ArmPasteGuard();

  // 粘贴目标（设计方案 §6.5）：点选绑定过的窗口优先，且只在它仍存活时生效；
  // 没绑定过就用"唤起主窗前那个窗口"，切换应用即跟随（2026-10-04 用户决议）。
  HWND PasteTarget() const;

  // 点选绑定（技术方案 §5.5）：靶心按钮的开关与六条取消路径的落点
  void TogglePick();
  void CancelPick();
  bool picking() const { return picker_.picking(); }
  void OnPickMessage(HWND target);         // WM_APP_PICK_DONE
  void OnPickTimeout();                    // ID_PICK 8s
  void OnSessionLock();                    // WM_WTSSESSION_CHANGE
  void OnEndSession();                     // WM_ENDSESSION
  bool bound() const;                      // 绑定窗仍在世才算"已绑定"（R5）
  const std::wstring& boundProcessName() const { return boundProcessName_; }

  // C15 无开关接力（v2.3.0，2026-10-05 用户决议）：没有"开启/关闭"这一步，也没有菜单项。
  // 主窗在屏 + 快速模式 = 接力可用；两者任一不成立 = 钩子卸下。每个能改变这两个条件的
  // 入口都必须调 SyncRelayHook()（收放窗、切粘贴模式、点选起止、会话锁屏/解锁、退出）。
  void SyncRelayHook();              // 幂等裁决：该装就装（并自愈式重挂），该卸就卸
  void OnRelayTrigger(HWND target);  // WM_APP_RELAY_TRIGGER：钩子投来的根窗口
  void OnRelayHotkey(HWND foreground);  // 兜底热键 Alt+`：目标 = 按键瞬间的前台窗
  void OnSessionUnlock();            // WM_WTSSESSION_CHANGE / WTS_SESSION_UNLOCK
  const std::wstring& statusHint() const { return statusHint_; }   // 非空优先显示（降级/收藏提示）
  void ShowStatusHint(std::wstring text);  // 临时提示，kStatusHintMs 后自动消隐
  void OnStatusHintTimer();

  Store& store() { return *store_; }
  MainWindow& window() { return *window_; }
  HINSTANCE instance() const { return inst_; }
  bool shuttingDown() const { return shuttingDown_; }

  // 设置（技术方案 §11 步骤 10 / 设计方案 §8.3）：变更即落盘，位置与尺寸在退出链里存。
  const Settings& settings() const { return settings_; }
  void SaveTopmost(bool on);
  void SavePasteMode(PasteMode mode);
  void SaveFilterType(FilterType filter);
  void SaveCopyMode(CopyMode mode);

 private:
  void WireStoreEvents();
  void WirePicker();
  void CapturePasteTarget();            // 唤起前记录目标，比 Deactivated 时机可靠
  void RefreshUi();                     // 绑定态变化后重画靶心与状态栏
  void ApplySettingsToStore();          // 复制模式/粘贴模式/过滤：读盘 → Store 初值
  void RestoreBinding();                // 按进程名找回绑定窗口（同名多窗口不自动绑）
  void PersistSettings();               // settings.json 写盘（失败静默，内存态仍可用）
  void RelayStep(HWND target);          // 两条触发（钩子/热键）共用的推进动作：贴屏幕第一行

  HINSTANCE inst_;
  StorageService storage_;
  std::wstring settingsPath_;
  Settings settings_;
  std::unique_ptr<Store> store_;
  ClipboardMonitor monitor_;
  TrayService tray_;
  std::unique_ptr<MainWindow> window_;
  ProcessPicker picker_;                // 与下面两个钩子服务都声明在 window_ 之后：析构时主窗仍有效
  RelayService relay_;                  // 同上，且必须在 picker_ 之后声明（先卸接力钩子）

  HWND lastExternalWindow_ = nullptr;
  HWND boundWindow_ = nullptr;          // 只有点选成功才会赋值
  std::wstring boundProcessName_;
  std::wstring statusHint_;
  bool internalPaste_ = false;
  std::wstring lastPasted_;
  bool shuttingDown_ = false;
};

}  // namespace sc
