#pragma once
#include "../native/ComPtr.h"
#include <d2d1.h>
#include <dwrite.h>

namespace sc {

// 颜色/字号/画刷的唯一来源（技术方案 §6.6）。画刷属"目标级"资源，
// 设备丢失重建渲染目标后必须再调一次 CreateBrushes。
class Theme {
 public:
  bool InitFonts(IDWriteFactory* factory);                 // 工厂级：进程存活期复用
  HRESULT CreateBrushes(ID2D1RenderTarget* rt);            // 目标级

  // 语义色（DIP 空间绘制一律用这些）
  ID2D1SolidColorBrush* WindowBg() const { return windowBg_.get(); }
  ID2D1SolidColorBrush* CardBg() const { return cardBg_.get(); }
  ID2D1SolidColorBrush* CardStroke() const { return cardStroke_.get(); }
  ID2D1SolidColorBrush* SelectedStroke() const { return selectedStroke_.get(); }
  ID2D1SolidColorBrush* FavoriteBg() const { return favoriteBg_.get(); }
  ID2D1SolidColorBrush* HoverBg() const { return hoverBg_.get(); }
  ID2D1SolidColorBrush* Ink() const { return ink_.get(); }
  ID2D1SolidColorBrush* InkPasted() const { return inkPasted_.get(); }      // 预混合灰显
  ID2D1SolidColorBrush* Muted() const { return muted_.get(); }
  ID2D1SolidColorBrush* MutedPasted() const { return mutedPasted_.get(); }
  ID2D1SolidColorBrush* TitleBg() const { return titleBg_.get(); }
  ID2D1SolidColorBrush* ToolBg() const { return toolBg_.get(); }
  ID2D1SolidColorBrush* StatusBg() const { return statusBg_.get(); }
  ID2D1SolidColorBrush* BtnBg() const { return btnBg_.get(); }
  ID2D1SolidColorBrush* BtnBgHover() const { return btnBgHover_.get(); }
  ID2D1SolidColorBrush* Accent() const { return accent_.get(); }
  ID2D1SolidColorBrush* StarOn() const { return starOn_.get(); }
  ID2D1SolidColorBrush* BindOff() const { return bindOff_.get(); }    // 靶心红：未绑定
  ID2D1SolidColorBrush* BindOn() const { return bindOn_.get(); }      // 靶心绿：已绑定

  IDWriteTextFormat* Title() const { return fmtTitle_.get(); }
  IDWriteTextFormat* Body() const { return fmtBody_.get(); }
  IDWriteTextFormat* Meta() const { return fmtMeta_.get(); }
  IDWriteTextFormat* Button() const { return fmtButton_.get(); }
  IDWriteTextFormat* Star() const { return fmtStar_.get(); }

  bool highContrast() const { return highContrast_; }

 private:
  static IDWriteTextFormat* MakeFormat(IDWriteFactory* f, float sizeDip);

  Com<IDWriteTextFormat> fmtTitle_, fmtBody_, fmtMeta_, fmtButton_, fmtStar_;
  Com<ID2D1SolidColorBrush> windowBg_, cardBg_, cardStroke_, selectedStroke_, favoriteBg_,
      hoverBg_, ink_, inkPasted_, muted_, mutedPasted_, titleBg_, toolBg_,
      statusBg_, btnBg_, btnBgHover_, accent_, starOn_, bindOff_, bindOn_;
  bool highContrast_ = false;
};

// 灰显不走 PushLayer（会强制灰度 AA），改为前景与底色预混合（技术方案 §6.6）
D2D1_COLOR_F BlendOver(D2D1_COLOR_F fg, D2D1_COLOR_F bg, float alpha);

}  // namespace sc
