#pragma once
#include "../core/Config.h"
#include "../native/ComPtr.h"
#include "Theme.h"
#include <d2d1.h>
#include <dwrite.h>

namespace sc {

// 步骤 11（技术方案 §6.8）：9 步静态引导。无边框 `WS_POPUP`，入口是主窗右键菜单「使用帮助」。
//
// 模态靠 `EnableWindow(主窗, FALSE)` 实现，**不起嵌套消息循环**：本进程是单消息泵
// （设计方案 §9），嵌套循环会把粘贴阶段机与点选钩子的时序拖进不可复现的状态。
// 主窗是 HWND_TOPMOST，所以本窗也必须 topmost，否则会被自己盖住。
// 渲染工厂借用主窗的（同线程、活得比本窗久），本窗只持有自己的渲染目标；隐藏时释放目标省内存。
class HelpWindow {
 public:
  bool Create(HINSTANCE inst, HWND owner, ID2D1Factory* d2d, IDWriteFactory* write);
  void Destroy();
  void Show();                       // 放主窗左侧（主窗常年贴右缘）→ 禁用主窗 → 回到第 1 页
  bool IsOpen() const { return visible_; }

 private:
  enum Btn { kNone = -1, kClose = 0, kPrev = 1, kNext = 2 };

  static LRESULT CALLBACK Entry(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

  HRESULT CreateTarget(HWND hwnd);
  void ReleaseTarget();
  void Paint(HWND hwnd);
  void Step(int delta);
  void Close();
  RECT ButtonRect(Btn b) const;                       // 客户区 DIP
  int HitButton(POINT ptClient) const;

  HWND hwnd_ = nullptr;
  HWND owner_ = nullptr;
  ID2D1Factory* d2d_ = nullptr;
  IDWriteFactory* write_ = nullptr;
  Com<ID2D1HwndRenderTarget> rt_;
  Theme theme_;
  Com<IDWriteTextFormat> fmtCenter_;      // 按钮文字：D2D 的 DrawText 不带对齐，居中得靠格式
  UINT dpi_ = 96;
  int page_ = 0;
  int hover_ = kNone;
  bool visible_ = false;
};

}  // namespace sc
