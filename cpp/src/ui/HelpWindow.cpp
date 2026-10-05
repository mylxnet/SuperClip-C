#include "HelpWindow.h"
#include "../native/SystemInfo.h"
#include "../util/Log.h"
#include <windowsx.h>
#include <cmath>
#include <cwchar>
#include <string>

namespace sc {
namespace {

// 名字不能叫 Step：类里有成员函数 HelpWindow::Step，在成员函数体内匿名命名空间的同名类型
// 会被类作用域吞掉（C++ 名字查找先走类作用域），编译直接报 "does not name a type"。
struct Guide {
  const wchar_t* title;
  const wchar_t* body;
};

// 九步主题固定（设计方案 §7 / 技术方案 §6.8）：呼出热键、自动记录、双模式、收藏、绑定、
// 复制模式、搜索、清除/复位、置顶。文案描述的是**当前实现的实际行为**，不是愿望清单。
const Guide kSteps[] = {
    {L"呼出与收起",
     L"按 Ctrl + ` 显示或隐藏窗口。\n"
     L"点标题栏 ∨ 只是收起，剪贴板监听继续在后台跑；点 ✕ 才是彻底退出。"},
    {L"自动记录",
     L"在任何应用里按复制，这里就自动记下一条。\n"
     L"最多保留 500 条，超出时从最旧的非收藏记录开始淘汰，收藏项不参与淘汰。"},
    {L"两种粘贴模式",
     L"普通模式：双击一条即粘贴到目标的光标处。\n"
     L"快速模式：选中位始终停在第一行，直接按空格就粘最新那条，不用先点；粘过的沉底并变灰。\n"
     L"要先贴别的某条，单击它再按空格即可，新复制一条会重新回到第一行。\n"
     L"点标题栏上的模式文字，或在窗口里右键选「粘贴模式」都能切换。"},
    {L"收藏",
     L"点条目右侧的 ★ 收藏。收藏项只在【收藏】视图里显示，不参与自动淘汰，"
     L"也不会被「清除」删掉。\n想彻底删除一条收藏：先取消收藏，再按清除。"},
    {L"绑定目标应用",
     L"点工具栏的靶心，再点一下目标应用的窗口，即完成绑定；绑定后粘贴只发给它，靶心变绿。\n"
     L"同一程序开着多个窗口时不会自动绑定，需要重新点选一次——绑错窗口比不绑更糟。\n"
     L"未绑定时，粘贴发给你唤起本窗之前用的那个窗口。"},
    {L"复制模式",
     L"一般复制：只有含制表符的文本按表格逐格拆分，多行纯文本整体记一条。\n"
     L"表格复制：没有制表符的多行文本也逐行拆成条目。\n"
     L"右键选「复制模式」切换；切换只影响之后的复制，已入库的条目不重切。"},
    {L"搜索",
     L"在搜索框输入关键词即时过滤。大小写、全角半角都不敏感；"
     L"表格条目的行列标注虽然不显示，但同样能被搜到。\n"
     L"框里有字时右端出现 ✕，点一下就清空搜索。"},
    {L"清除与复位",
     L"「清除」删掉全部非收藏记录，收藏原样保留。\n"
     L"「复位」清掉已粘贴的灰显标记，并把记录按时间重新排好。"},
    {L"置顶",
     L"点标题栏的图钉让窗口浮在所有窗口之上，再点一下取消；图钉变蓝并带下划线＝已置顶。\n"
     L"程序启动时默认开启置顶，你的选择会被记住，下次启动沿用。"},
};

constexpr int kStepCount = int(sizeof(kSteps) / sizeof(kSteps[0]));

// 页码槽与按钮文字都按"右对齐/居中"排，D2D 的 DrawText 没有对齐选项（对齐属 TextFormat），
// 所以页码用固定宽度的左对齐槽位，按钮另建一个居中格式。
constexpr float kPageSlotW = 44.f;

float ScaleF(UINT dpi) { return float(dpi) / 96.f; }

}  // namespace

bool HelpWindow::Create(HINSTANCE inst, HWND owner, ID2D1Factory* d2d, IDWriteFactory* write) {
  if (hwnd_) return true;
  if (!d2d || !write) return false;
  owner_ = owner;
  d2d_ = d2d;
  write_ = write;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &HelpWindow::Entry;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kHelpClass;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LogError(L"ui", L"帮助窗类注册失败");
    return false;
  }

  const HWND hwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kHelpClass, kHelpTitle, WS_POPUP, 0, 0,
      ScaleInt(kHelpW, 96), ScaleInt(kHelpH, 96), owner, nullptr, inst, this);
  if (!hwnd) {
    LogError(L"ui", L"帮助窗创建失败");
    return false;
  }
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  hwnd_ = hwnd;
  dpi_ = DpiForWindow(hwnd);
  ApplyRoundedCorners(hwnd);
  if (!theme_.InitFonts(write)) {
    LogError(L"ui", L"帮助窗字体资源创建失败");
    return false;
  }
  const HRESULT hrFmt = write_->CreateTextFormat(
      L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, kFontButton, L"zh-CN", fmtCenter_.put());
  if (FAILED(hrFmt) || !fmtCenter_) {
    LogError(L"ui", L"帮助窗按钮文字格式创建失败");
    return false;
  }
  fmtCenter_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  fmtCenter_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  return true;
}

void HelpWindow::Destroy() {
  if (!hwnd_) return;
  if (visible_ && owner_ && IsWindow(owner_)) EnableWindow(owner_, TRUE);
  visible_ = false;
  ReleaseTarget();
  DestroyWindow(hwnd_);
  hwnd_ = nullptr;
}

void HelpWindow::Show() {
  if (!hwnd_) return;
  page_ = 0;
  hover_ = kNone;

  const int w = ScaleInt(kHelpW, dpi_), h = ScaleInt(kHelpH, dpi_);
  RECT anchor{};
  RECT rc{};
  if (owner_ && IsWindow(owner_) && GetWindowRect(owner_, &anchor) && anchor.right > anchor.left) {
    // 放在主窗左侧：主窗常年贴屏幕右缘，压在它上面等于什么都看不见
    const int x = anchor.left - w - ScaleInt(kPad, dpi_);
    const int y = anchor.top + (anchor.bottom - anchor.top - h) / 2;
    rc.left = x < 0 ? anchor.right + ScaleInt(kPad, dpi_) : x;
    rc.top = y < 0 ? 0 : y;
  } else {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    rc.left = work.left + (work.right - work.left - w) / 2;
    rc.top = work.top + (work.bottom - work.top - h) / 2;
  }
  rc.right = rc.left + w;
  rc.bottom = rc.top + h;
  FitRectToDesktop(rc);                       // 落不到主窗左侧（左边没屏）时钳回当前显示器
  if (owner_ && IsWindow(owner_)) EnableWindow(owner_, FALSE);
  visible_ = true;
  SetWindowPos(hwnd_, HWND_TOPMOST, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
               SWP_SHOWWINDOW);
  SetFocus(hwnd_);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void HelpWindow::Close() {
  if (!hwnd_) return;
  visible_ = false;
  ReleaseTarget();
  ShowWindow(hwnd_, SW_HIDE);
  if (owner_ && IsWindow(owner_)) {
    EnableWindow(owner_, TRUE);
    SetFocus(owner_);
  }
}

void HelpWindow::Step(int delta) {
  const int next = page_ + delta;
  if (next < 0 || next >= kStepCount) return;
  page_ = next;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// 返回客户区 DIP 矩形（绘制期一律用 DIP，见 Config.h）
RECT HelpWindow::ButtonRect(Btn b) const {
  const int bottom = kHelpH - kPad;
  const int top = bottom - kHelpBtnH;
  const int right = kHelpW - kPad;
  switch (b) {
    case kClose: return RECT{kPad, top, kPad + kHelpCloseBtnW, bottom};
    case kPrev:
      return RECT{right - 2 * kHelpNavBtnW - kHelpBtnGap, top, right - kHelpNavBtnW - kHelpBtnGap,
                  bottom};
    default: return RECT{right - kHelpNavBtnW, top, right, bottom};
  }
}

int HelpWindow::HitButton(POINT ptClient) const {
  const float s = ScaleF(dpi_);
  const float x = float(ptClient.x) / s;
  const float y = float(ptClient.y) / s;
  for (int i = kClose; i <= kNext; ++i) {
    const bool disabled = (i == kPrev && page_ == 0) || (i == kNext && page_ == kStepCount - 1);
    if (disabled) continue;
    const RECT r = ButtonRect(Btn(i));
    if (x >= float(r.left) && x < float(r.right) && y >= float(r.top) && y < float(r.bottom)) {
      return i;
    }
  }
  return kNone;
}

HRESULT HelpWindow::CreateTarget(HWND hwnd) {
  if (!d2d_ || !hwnd) return E_UNEXPECTED;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
  const D2D1_HWND_RENDER_TARGET_PROPERTIES hw = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(UINT(rc.right), UINT(rc.bottom)));
  HRESULT hr = d2d_->CreateHwndRenderTarget(props, hw, rt_.put());
  if (FAILED(hr) || !rt_) {
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    hr = d2d_->CreateHwndRenderTarget(props, hw, rt_.put());
    if (FAILED(hr) || !rt_) return FAILED(hr) ? hr : E_FAIL;
  }
  rt_->SetDpi(dpi_, dpi_);
  return theme_.CreateBrushes(rt_.get());
}

void HelpWindow::ReleaseTarget() { rt_.reset(); }

void HelpWindow::Paint(HWND hwnd) {
  PAINTSTRUCT ps{};
  BeginPaint(hwnd, &ps);
  if (!rt_ && FAILED(CreateTarget(hwnd))) {
    EndPaint(hwnd, &ps);
    return;
  }
  const float w = float(kHelpW), h = float(kHelpH);
  const Guide& s = kSteps[page_];

  rt_->BeginDraw();
  rt_->FillRectangle(D2D1::RectF(0.f, 0.f, w, h), theme_.WindowBg());
  rt_->FillRectangle(D2D1::RectF(0.f, 0.f, w, float(kHelpTitleH)), theme_.TitleBg());

  const std::wstring page = std::to_wstring(page_ + 1) + L" / " + std::to_wstring(kStepCount);
  rt_->DrawText(kHelpTitle, UINT(wcslen(kHelpTitle)), theme_.Title(),
                D2D1::RectF(kPad, 8.f, w - kPad, float(kHelpTitleH)), theme_.Ink(),
                D2D1_DRAW_TEXT_OPTIONS_NONE);
  rt_->DrawText(page.c_str(), UINT(page.size()), theme_.Meta(),
                D2D1::RectF(w - kPad - kPageSlotW, 11.f, w - kPad, float(kHelpTitleH)),
                theme_.Muted(), D2D1_DRAW_TEXT_OPTIONS_NONE);
  rt_->DrawLine(D2D1::Point2F(0.f, float(kHelpTitleH)), D2D1::Point2F(w, float(kHelpTitleH)),
                theme_.CardStroke(), 1.f);

  rt_->DrawText(s.title, UINT(wcslen(s.title)), theme_.Title(),
                D2D1::RectF(kPad, float(kHelpTitleH) + 14.f, w - kPad,
                            float(kHelpTitleH) + 40.f),
                theme_.Ink(), D2D1_DRAW_TEXT_OPTIONS_NONE);
  rt_->DrawText(s.body, UINT(wcslen(s.body)), theme_.Body(),
                D2D1::RectF(kPad, float(kHelpTitleH) + 46.f, w - kPad, h - float(kHelpBtnH) - kPad),
                theme_.Ink(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

  for (int i = kClose; i <= kNext; ++i) {
    const Btn b = Btn(i);
    const bool disabled = (b == kPrev && page_ == 0) || (b == kNext && page_ == kStepCount - 1);
    const RECT r = ButtonRect(b);
    const D2D1_RECT_F box{float(r.left), float(r.top), float(r.right), float(r.bottom)};
    const bool hot = !disabled && hover_ == i;
    rt_->FillRoundedRectangle(D2D1::RoundedRect(box, float(kCardRadius), float(kCardRadius)),
                              hot ? theme_.BtnBgHover() : theme_.BtnBg());
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(box, float(kCardRadius), float(kCardRadius)),
                              theme_.CardStroke(), 1.f);
    const wchar_t* label = b == kClose ? L"关闭" : (b == kPrev ? L"上一步" : L"下一步");
    rt_->DrawText(label, UINT(wcslen(label)), fmtCenter_.get(), box,
                  disabled ? theme_.Muted() : theme_.Ink(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }

  if (FAILED(rt_->EndDraw())) {
    ReleaseTarget();
    if (SUCCEEDED(CreateTarget(hwnd))) InvalidateRect(hwnd, nullptr, FALSE);
  }
  EndPaint(hwnd, &ps);
}

LRESULT CALLBACK HelpWindow::Entry(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<HelpWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) return self->Handle(hwnd, msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT HelpWindow::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_PAINT:
      Paint(hwnd);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(hwnd, &pt);
      // 无边框窗没有系统标题栏：整条顶部色带留给拖拽，否则模态窗挡住哪都动不了
      return float(pt.y) / ScaleF(dpi_) < float(kHelpTitleH) ? HTCAPTION : HTCLIENT;
    }
    case WM_MOUSEMOVE: {
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int hit = HitButton(pt);
      SetCursor(LoadCursorW(nullptr, hit != kNone ? IDC_HAND : IDC_ARROW));
      if (hit != hover_) {
        hover_ = hit;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      switch (HitButton(pt)) {
        case kClose: Close(); break;
        case kPrev: Step(-1); break;
        case kNext: Step(1); break;
        default: break;
      }
      return 0;
    }
    case WM_KEYDOWN:
      switch (wParam) {
        case VK_LEFT: Step(-1); return 0;
        case VK_RIGHT: Step(1); return 0;
        case VK_ESCAPE: Close(); return 0;
        default: break;
      }
      break;
    case WM_CLOSE:
      Close();
      return 0;
    case WM_DPICHANGED: {
      const RECT* sug = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(hwnd, nullptr, sug->left, sug->top, sug->right - sug->left,
                   sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
      dpi_ = LOWORD(wParam);
      ReleaseTarget();
      if (SUCCEEDED(CreateTarget(hwnd))) InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace sc
