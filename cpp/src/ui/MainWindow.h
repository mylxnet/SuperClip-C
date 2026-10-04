#pragma once
#include "../core/Config.h"
#include "../core/Settings.h"
#include "../core/Store.h"
#include "../native/ComPtr.h"
#include "../services/PasteService.h"
#include "HelpWindow.h"
#include "HoverTip.h"
#include "ListRenderer.h"
#include "Theme.h"
#include <d2d1.h>
#include <dwrite.h>
#include <memory>

namespace sc {

class AppContext;

// 命中区域模型（技术方案 §6.2）
enum class HitZone { None, ModeText, BtnMinimize, BtnTopmost, BtnClose,
                     SearchBox, BtnFilter, BtnClear, BtnReset, BtnPick,
                     RowBody, RowStar, ListBackground };
struct HitResult {
  HitZone zone = HitZone::None;
  const ClipItem* item = nullptr;
  size_t displayIndex = 0;
};

// 输入归属（技术方案 §6.7）：EDIT 获焦经 EN_SETFOCUS/EN_KILLFOCUS 更新
enum class FocusOwner { List, SearchEdit };

// 主窗口：三窗口拓扑里唯一有 UI 的窗口。全自绘（D2D/DWrite），搜索框为真 EDIT 子窗。
// 步骤 6：绘制管线 + 行测量 + 滚动 + 标题栏三按钮；步骤 7：搜索/过滤/收藏/清除/复位/模式切换。
class MainWindow {
 public:
  MainWindow() = default;
  ~MainWindow() { Destroy(); }
  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  bool Create(HINSTANCE inst, AppContext& ctx);
  void Destroy();

  HWND get() const { return hwnd_; }
  void ToggleVisibility();
  void ShowAndFocus();
  bool IsVisible() const;
  void OnStoreChanged(const StoreEvent& event);
  void Repaint() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }   // 绑定态变化后刷新靶心/状态栏

  // 步骤 10：退出链里把当前窗口位置/尺寸按 **DIP** 写回 DTO（与 .NET 存法同口径）。
  // 取不到窗口就保持原值，不覆盖用户已存的位置。
  void SnapshotGeometry(Settings& s) const;

 private:
  static LRESULT CALLBACK Entry(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(HWND, UINT msg, WPARAM wParam, LPARAM lParam);

  HRESULT CreateRenderTarget(HWND hwnd);                  // 硬件失败回退 WARP 一次（§6.5）
  void ReleaseTargetLevel();                              // 目标级资源（画刷随目标一起弃）
  void RebuildRows();
  void LayoutSearchBox(HWND hwnd);
  void OnPaint(HWND hwnd);
  void DrawTitleBar(ID2D1RenderTarget* rt, float w);
  void DrawToolbar(ID2D1RenderTarget* rt, float w);
  void DrawStatusBar(ID2D1RenderTarget* rt, float w, float h);
  void ShowRowTip(HWND hwnd);                        // 悬停到期：为截断行浮现全文气泡
  void CancelRowTip(HWND hwnd);                      // 撤销计时并收起气泡
  void ApplySearchFromEdit();                    // FR-06：防抖到点后把编辑框内容下发 Store
  void ShowFilterMenu(HWND hwnd);                // FR-07：自绘按钮 + TrackPopupMenuEx 四项
  void ShowMainMenu(HWND hwnd, POINT ptScreen);  // 步骤 11：右键菜单，模式项文案随当前值变
  void TogglePasteMode();                        // §5.3：点击标题栏模式文字切换
  void DoPaste(const ClipItem* item, bool moveToEnd);   // FR-09/10/11：粘贴链路入口
  void OnPasteDone(bool ok);                      // WM_APP_PASTE_DONE：成功才置灰
  const wchar_t* FilterLabel() const;            // 工具栏首按钮显示当前过滤值
  HitResult HitTest(POINT ptClient) const;
  void SetTopmost(HWND hwnd, bool on);
  void ClampScroll(float viewportH);
  void DockToWorkArea(HWND hwnd);                 // 贴屏幕右缘、垂直居中（380×600 基准）
  void RestoreOrDock(HWND hwnd);                  // 步骤 10：有有效落盘位置就采纳，否则默认停靠
  void RegisterHotkeys(HWND hwnd);
  void UnregisterHotkeys(HWND hwnd);
  float ScaleF() const { return dpi_ / 96.f; }            // DIP → 物理像素
  float ClientW() const;                                  // 客户区宽度（DIP）
  float ClientH() const;

  HWND hwnd_ = nullptr;                                   // 仅供外部调用；消息路径用入参句柄
  AppContext* ctx_ = nullptr;
  UINT dpi_ = 96;
  bool hotkeyRegistered_ = false;

  Com<ID2D1Factory> d2dFactory_;
  Com<IDWriteFactory> writeFactory_;
  Com<ID2D1HwndRenderTarget> rt_;
  Theme theme_;
  std::unique_ptr<ListRenderer> list_;
  HoverTip tip_;
  HelpWindow help_;                // 步骤 11：右键菜单「使用帮助」的 9 步引导窗
  HWND edit_ = nullptr;
  HFONT editFont_ = nullptr;
  HBRUSH editBgBrush_ = nullptr;

  float scrollY_ = 0.f;
  FocusOwner focus_ = FocusOwner::List;
  PasteService paste_;
  const ClipItem* pendingPasteItem_ = nullptr;   // 等 WM_APP_PASTE_DONE 时用
  bool pasteMoveToEnd_ = false;
  const ClipItem* hover_ = nullptr;
  bool trackingLeave_ = false;
  bool topmost_ = true;    // 设计方案 §8：Topmost 默认 true，启动即浮在各窗之上（★ 按钮可关）
  bool occluded_ = false;
};

}  // namespace sc
