#pragma once
#include "../core/Config.h"
#include <string>

namespace sc {

// 悬浮全文气泡（设计方案 §7）：行内容压成单行后，完整内容由它给出。
// 用自绘弹出窗而不是 comctl32 ToolTip：交叉构建不内嵌 manifest，v6 视觉样式不保证生效，
// 而且样式必须与卡片一致（白底 + 灰边）。
class HoverTip {
 public:
  bool Create(HINSTANCE inst, HWND owner);
  void Destroy();
  // content 为条目原始全文；rowScreen = 该行的屏幕矩形（物理像素），气泡贴其下沿
  void Show(const std::wstring& content, const RECT& rowScreen, UINT dpi);
  void Hide();

 private:
  static LRESULT CALLBACK Entry(HWND, UINT, WPARAM, LPARAM);
  void EnsureFont(UINT dpi);
  void Paint();

  HWND hwnd_ = nullptr;
  HFONT font_ = nullptr;
  UINT fontDpi_ = 0;
  std::wstring text_;
};

}  // namespace sc
