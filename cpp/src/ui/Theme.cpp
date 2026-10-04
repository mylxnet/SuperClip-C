#include "Theme.h"
#include "../core/Config.h"
#include "../native/SystemInfo.h"
#include "../util/Log.h"

namespace sc {
namespace {

constexpr D2D1_COLOR_F Rgb(UINT hex, float a = 1.f) {
  return D2D1_COLOR_F{((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f,
                      (hex & 0xFF) / 255.f, a};
}

D2D1_COLOR_F FromSysColor(int index) {
  const COLORREF c = GetSysColor(index);
  return Rgb(0xFF000000u | (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c));
}

// 样式契约（设计方案 §7）：白底 / 边框灰（kBorderGrayHex，用户指定）/ 选中 #00897B /
// 收藏 #FFF8E1 / 悬停 #F5F7FA；墨色与气泡文字共用 kInkGrayHex
constexpr UINT kInkHex = kInkGrayHex;
constexpr UINT kMutedHex = 0x8991A0;
constexpr UINT kHoverHex = 0xF5F7FA;
constexpr UINT kFavHex = 0xFFF8E1;
constexpr UINT kSelectedHex = 0x00897B;
constexpr float kPastedAlpha = 0.55f;         // 整行不透明度等价（预混合实现）

}  // namespace

D2D1_COLOR_F BlendOver(D2D1_COLOR_F fg, D2D1_COLOR_F bg, float alpha) {
  D2D1_COLOR_F out{};
  out.r = bg.r + (fg.r - bg.r) * alpha;
  out.g = bg.g + (fg.g - bg.g) * alpha;
  out.b = bg.b + (fg.b - bg.b) * alpha;
  out.a = 1.f;
  return out;
}

IDWriteTextFormat* Theme::MakeFormat(IDWriteFactory* f, float sizeDip) {
  IDWriteTextFormat* fmt = nullptr;
  const HRESULT hr = f->CreateTextFormat(
      L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, sizeDip, L"zh-CN", &fmt);
  if (FAILED(hr) || !fmt) {
    LogError(L"ui", L"TextFormat 创建失败");
    return nullptr;
  }
  // 中日韩按字断行；顶左对齐（技术方案 §6.4）
  fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  return fmt;
}

bool Theme::InitFonts(IDWriteFactory* factory) {
  if (!factory) return false;
  fmtTitle_.attach(MakeFormat(factory, kFontTitle));
  fmtBody_.attach(MakeFormat(factory, kFontBody));
  fmtMeta_.attach(MakeFormat(factory, kFontMeta));
  fmtButton_.attach(MakeFormat(factory, kFontButton));
  fmtStar_.attach(MakeFormat(factory, kFontStar));
  // 标题与按钮画在固定高度的条内：水平/垂直都居中（正文保持顶左对齐）
  if (fmtTitle_) {
    fmtTitle_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }
  if (fmtButton_) {
    fmtButton_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmtButton_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }
  return fmtBody_ && fmtMeta_ && fmtTitle_ && fmtButton_ && fmtStar_;
}

HRESULT Theme::CreateBrushes(ID2D1RenderTarget* rt) {
  if (!rt) return E_POINTER;
  highContrast_ = IsHighContrast();

  D2D1_COLOR_F ink = Rgb(kInkHex), muted = Rgb(kMutedHex);
  D2D1_COLOR_F card = Rgb(0xFFFFFF), window = Rgb(0xFFFFFF);
  D2D1_COLOR_F stroke = Rgb(kBorderGrayHex);   // 卡片/按钮边框：灰
  D2D1_COLOR_F hover = Rgb(kHoverHex), fav = Rgb(kFavHex), sel = Rgb(kSelectedHex);

  if (highContrast_) {
    // §6.6：高对比度下一切取系统色、取消预混合灰显（纯灰字）、描边保留
    window = FromSysColor(COLOR_WINDOW);
    card = window;
    ink = FromSysColor(COLOR_WINDOWTEXT);
    muted = ink;
    stroke = FromSysColor(COLOR_WINDOWFRAME);   // 浅灰在高对比度底色上会看不见
    hover = window;
    fav = window;
    sel = FromSysColor(COLOR_HIGHLIGHT);
  }

  const D2D1_COLOR_F inkPasted = highContrast_ ? muted : BlendOver(ink, card, kPastedAlpha);
  const D2D1_COLOR_F mutedPasted = highContrast_ ? muted : BlendOver(muted, card, kPastedAlpha);

  auto make = [rt](Com<ID2D1SolidColorBrush>& slot, D2D1_COLOR_F c) {
    if (FAILED(rt->CreateSolidColorBrush(c, slot.put()))) return false;
    return true;
  };
  if (!make(windowBg_, window) || !make(cardBg_, card) || !make(cardStroke_, stroke) ||
      !make(selectedStroke_, sel) || !make(favoriteBg_, fav) || !make(hoverBg_, hover) ||
      !make(ink_, ink) || !make(inkPasted_, inkPasted) || !make(muted_, muted) ||
      !make(mutedPasted_, mutedPasted) ||
      !make(titleBg_, Rgb(0xEFF2F6)) || !make(toolBg_, window) || !make(statusBg_, Rgb(0xEFF2F6)) ||
      !make(btnBg_, card) || !make(btnBgHover_, hover) || !make(accent_, sel) ||
      !make(starOn_, Rgb(0xF5A623)) ||                       // 收藏星标金（非契约色，见报告）
      !make(bindOff_, Rgb(0xE53935)) ||                     // 靶心红：未绑定（§6.6）
      !make(bindOn_, Rgb(0x43A047))) {                      // 靶心绿：已绑定（§6.6）
    LogError(L"ui", L"画刷创建失败");
    return E_FAIL;
  }
  if (highContrast_) {
    titleBg_ = statusBg_ = windowBg_;                        // 系统色下不做自定义底色区分
  }
  return S_OK;
}

}  // namespace sc
