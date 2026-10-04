#include "HoverTip.h"
#include "../core/Text.h"
#include "../util/Log.h"
#include <algorithm>
#include <cmath>

namespace sc {
namespace {

constexpr wchar_t kTipClass[] = L"SuperClipHoverTip";
constexpr int kTipPadDip = 8;     // 文字与边框内距
constexpr int kTipGapDip = 2;     // 气泡与行之间的间隙

COLORREF RgbToRef(UINT hex) {
  return RGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

int ScaleTip(int baseDip, UINT dpi) { return MulDiv(baseDip, static_cast<int>(dpi), 96); }

// 在 width 内排版所需的尺寸：宽 = 最长行宽，高 = 行数总高
SIZE WrapSize(HDC dc, const std::wstring& text, int width) {
  if (!dc || text.empty() || width <= 0) return SIZE{0, 0};
  RECT rc{0, 0, width, 0};
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc,
            DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
  return SIZE{rc.right - rc.left, rc.bottom - rc.top};
}

// 代理对不切断
std::wstring CutAt(std::wstring_view text, size_t count) {
  std::wstring out(text.substr(0, count));
  if (!out.empty() && (out.back() & 0xFC00) == 0xD800) out.pop_back();
  out.push_back(L'…');
  return out;
}

// 与列表预览同一套折叠规则：换行归一、制表符→4 空格、超长截断
std::wstring TipText(std::wstring_view content) {
  std::wstring out = TrimNewlines(NormalizeNewlines(content));
  std::wstring fixed;
  fixed.reserve(out.size() + 8);
  for (const wchar_t c : out) {
    if (c == L'\t') fixed.append(L"    ");
    else fixed.push_back(c);
  }
  if (fixed.size() > kMaxTipChars) fixed = CutAt(fixed, kMaxTipChars);
  return fixed;
}

}  // namespace

bool HoverTip::Create(HINSTANCE inst, HWND owner) {
  if (hwnd_) return true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &HoverTip::Entry;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kTipClass;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LogError(L"ui", L"气泡窗口类注册失败");
    return false;
  }
  // 无激活 + 点击穿透 + 置顶：气泡不抢焦点，也不挡住条目上的点击
  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, kTipClass, L"",
      WS_POPUP, 0, 0, 0, 0, owner, nullptr, inst, this);
  if (!hwnd_) LogError(L"ui", L"气泡窗口创建失败");
  return hwnd_ != nullptr;
}

void HoverTip::Destroy() {
  const HWND hwnd = hwnd_;
  hwnd_ = nullptr;
  if (font_) {
    DeleteObject(font_);
    font_ = nullptr;
    fontDpi_ = 0;
  }
  text_.clear();
  if (hwnd) DestroyWindow(hwnd);
}

void HoverTip::EnsureFont(UINT dpi) {
  if (font_ && fontDpi_ == dpi) return;
  if (font_) DeleteObject(font_);
  font_ = CreateFontW(-ScaleTip(static_cast<int>(std::lround(kFontBody)), dpi), 0, 0, 0, FW_NORMAL,
                      FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI");
  fontDpi_ = font_ ? dpi : 0;
}

void HoverTip::Show(const std::wstring& content, const RECT& rowScreen, UINT dpi) {
  if (!hwnd_) return;
  EnsureFont(dpi);
  if (!font_) {
    Hide();
    return;
  }
  text_ = TipText(content);
  if (text_.empty()) {
    Hide();
    return;
  }

  const int pad = ScaleTip(kTipPadDip, dpi);
  const int maxW = ScaleTip(kTipMaxW, dpi);
  const int maxH = ScaleTip(kTipMaxH, dpi);
  const int textW = maxW - 2 * pad;
  const int textH = maxH - 2 * pad;

  HDC dc = GetDC(hwnd_);
  HGDIOBJ oldFont = dc ? SelectObject(dc, font_) : nullptr;
  SIZE box = WrapSize(dc, text_, textW);
  if (box.cy > textH) {
    // 超出气泡高度：二分"放得下的最长前缀"再补 …（与列表单行裁剪同一思路）
    size_t lo = 0;
    size_t hi = text_.size();
    while (lo < hi) {
      const size_t mid = (lo + hi + 1) / 2;
      if (WrapSize(dc, CutAt(text_, mid), textW).cy <= textH) lo = mid;
      else hi = mid - 1;
    }
    text_ = CutAt(text_, lo);
    box = WrapSize(dc, text_, textW);
  }
  if (oldFont) SelectObject(dc, oldFont);
  if (dc) ReleaseDC(hwnd_, dc);

  const int winW = std::min(maxW, static_cast<int>(box.cx) + 2 * pad);
  const int winH = std::min(maxH, static_cast<int>(box.cy) + 2 * pad);
  if (winW <= 2 * pad || winH <= 2 * pad) {
    Hide();
    return;
  }

  RECT work{};
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) work = rowScreen;
  const int wl = static_cast<int>(work.left), wt = static_cast<int>(work.top);
  const int wr = static_cast<int>(work.right), wb = static_cast<int>(work.bottom);
  const int gap = ScaleTip(kTipGapDip, dpi);
  int x = rowScreen.left;
  int y = rowScreen.bottom + gap;
  if (y + winH > wb) {
    const int above = rowScreen.top - gap - winH;
    y = above >= wt ? above : std::max(wt, wb - winH);
  }
  x = std::min(x, wr - winW);
  x = std::max(x, wl);

  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, winW, winH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, nullptr, TRUE);
}

void HoverTip::Hide() {
  text_.clear();
  if (hwnd_ && IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
}

LRESULT CALLBACK HoverTip::Entry(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    if (cs) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<HoverTip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_PAINT && self) {
    self->Paint();
    return 0;
  }
  if (msg == WM_ERASEBKGND) return 1;   // Paint 全量覆盖，防闪白
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void HoverTip::Paint() {
  PAINTSTRUCT ps{};
  HDC dc = BeginPaint(hwnd_, &ps);
  if (!dc) {
    EndPaint(hwnd_, &ps);
    return;
  }
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  HBRUSH edge = CreateSolidBrush(RgbToRef(kBorderGrayHex));
  FrameRect(dc, &rc, edge);
  DeleteObject(edge);

  if (font_) {
    HGDIOBJ oldFont = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RgbToRef(kInkGrayHex));
    const int pad = ScaleTip(kTipPadDip, fontDpi_);
    RECT text{rc.left + pad, rc.top + pad, rc.right - pad, rc.bottom - pad};
    DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &text,
              DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_LEFT);
    SelectObject(dc, oldFont);
  }
  EndPaint(hwnd_, &ps);
}

}  // namespace sc
