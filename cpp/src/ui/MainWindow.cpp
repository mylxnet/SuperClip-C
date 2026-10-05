#include "MainWindow.h"
#include "../app/AppContext.h"
#include "../native/SystemInfo.h"
#include "../util/Log.h"
#include <windowsx.h>
#include <shellapi.h>       // 单击署名打开仓库页：ShellExecuteW("open")，不引任何网络库
#include <wtsapi32.h>     // 会话锁屏通知：点选期间锁屏必须复位光标
#include <cmath>

namespace sc {
namespace {

// _WIN32_WINNT=0x0601 时系统头未定义，取固定值并在运行期按消息处理（§4.4）
constexpr UINT kMsgDpiChanged = 0x02E0;

constexpr float kTitleF = static_cast<float>(kTitleH);
constexpr float kToolbarF = static_cast<float>(kToolbarH);
constexpr float kStatusF = static_cast<float>(kStatusH);
constexpr float kPadF = static_cast<float>(kPad);
constexpr float kBtnF = static_cast<float>(kBtn);
constexpr float kSearchF = static_cast<float>(kSearchH);
constexpr float kToolBtnF = static_cast<float>(kToolBtnH);
constexpr float kToolPadF = static_cast<float>(kToolPady);
constexpr float kToolGapF = static_cast<float>(kToolGap);
constexpr float kTitleBtnGapF = static_cast<float>(kTitleBtnGap);
constexpr float kTitleIconF = static_cast<float>(kTitleIconDip);
constexpr float kTitleIconGapF = static_cast<float>(kTitleIconGap);
constexpr float kModeLeft = kPadF + kTitleIconF + kTitleIconGapF;   // 模式文字左缘（图标右侧）
constexpr float kListTop = kTitleF + kToolbarF;

// 设备丢失错误码（避免依赖 DXGI 头，值取自 winerror/dxgi）
constexpr HRESULT kDeviceRemoved = static_cast<HRESULT>(0x887A0005L);
constexpr HRESULT kDeviceReset = static_cast<HRESULT>(0x887A0001L);
constexpr HRESULT kDeviceHung = static_cast<HRESULT>(0x887A0002L);
constexpr HRESULT kDriverInternal = static_cast<HRESULT>(0x887A0004L);
constexpr HRESULT kRecreateTarget = static_cast<HRESULT>(0x88FC000CL);   // D2DERR_RECREATE_TARGET

bool IsDeviceLost(HRESULT hr) {
  return hr == kDeviceRemoved || hr == kDeviceReset || hr == kDeviceHung ||
         hr == kDriverInternal || hr == kRecreateTarget;
}

// 用 DWrite 量一段文字的实际宽度（DIP）。署名可点区必须和像素对齐，不能按字数估。
// 必须走 IDWriteFactory：mingw 的 ID2D1RenderTarget 没有 CreateTextLayout 包装（真机有，头没同步），
// 而排版与 DPI 无关，用工厂建出的 layout 可直接喂给任何渲染目标。
float MeasureTextW(IDWriteFactory* wf, const wchar_t* text, IDWriteTextFormat* fmt) {
  if (!wf || !fmt || !text || !*text) return 0.f;
  Com<IDWriteTextLayout> layout;
  if (FAILED(wf->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)), fmt, 10000.f, 10000.f,
                                  layout.put())))
    return 0.f;
  DWRITE_TEXT_METRICS m{};
  return SUCCEEDED(layout->GetMetrics(&m)) ? m.width : 0.f;
}

// 图标像素 → 预乘 BGRA 缓冲。
// mingw 的 d2d1.h 没有 ID2D1RenderTarget::CreateBitmapFromHICON（MS 头有，mingw 未同步），
// 这里只用已链接的 user32/gdi32 取像素，再交给 CreateBitmap 拷贝：不新增任何导入表项。
void ApplyAndMask(HDC dc, HBITMAP mask, std::vector<UINT8>& px, int w, int h) {
  if (!dc || !mask) return;
  BITMAP mb{};
  if (GetObjectW(mask, sizeof(mb), &mb) != sizeof(mb) || mb.bmWidth < w || mb.bmHeight < h) return;
  const int stride = ((w + 31) / 32) * 4;      // 1bpp 扫描行按 4 字节对齐
  std::vector<UINT8> row(static_cast<size_t>(stride));
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;                  // 负值＝自上而下
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 1;
  bmi.bmiHeader.biCompression = BI_RGB;
  for (int y = 0; y < h; ++y) {                 // 先全不透明，读到蒙版再打洞，读失败只会多留像素
    if (GetDIBits(dc, mask, static_cast<UINT>(y), 1, row.data(), &bmi, DIB_RGB_COLORS) != 1) break;
    for (int x = 0; x < w; ++x) {
      if ((row[x >> 3] >> (7 - (x & 7))) & 1) {  // AND 蒙版位 1＝透明，1bpp DIB 高位在左
        UINT8* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        p[0] = p[1] = p[2] = p[3] = 0;
      }
    }
  }
}

bool IconToPremultipliedBGRA(HICON icon, std::vector<UINT8>& px, int& w, int& h) {
  ICONINFO ii{};
  if (!GetIconInfo(icon, &ii)) return false;
  bool ok = false;
  BITMAP cb{};
  if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(cb), &cb) == sizeof(cb) && cb.bmWidth > 0 &&
      cb.bmHeight > 0 && cb.bmBitsPixel == 32) {
    w = cb.bmWidth;
    h = cb.bmHeight;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    px.assign(static_cast<size_t>(w) * h * 4, 0);
    HDC dc = GetDC(nullptr);
    const int got = dc ? GetDIBits(dc, ii.hbmColor, 0, static_cast<UINT>(h), px.data(), &bmi,
                                   DIB_RGB_COLORS)
                       : 0;
    bool allAlphaZero = true;
    for (size_t i = 3; i < px.size(); i += 4)
      if (px[i]) {
        allAlphaZero = false;
        break;
      }
    if (got == h && allAlphaZero) {
      // GDI 把第 4 字节当填充位清零的老行为：按蒙版重建，半透明边会被硬切，好过整块黑底
      for (size_t i = 3; i < px.size(); i += 4) px[i] = 0xFF;
      ApplyAndMask(dc, ii.hbmMask, px, w, h);
    }
    if (dc) ReleaseDC(nullptr, dc);
    ok = (got == h);
    if (!ok) px.clear();
  }
  if (ii.hbmColor) DeleteObject(ii.hbmColor);
  if (ii.hbmMask) DeleteObject(ii.hbmMask);
  return ok;
}

D2D1_RECT_F Sharpen(D2D1_RECT_F r) {
  return D2D1_RECT_F{std::floor(r.left) + .5f, std::floor(r.top) + .5f, std::floor(r.right) + .5f,
                     std::floor(r.bottom) + .5f};
}

void Stroke(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* b, float x0, float y0, float x1, float y1,
            float width = 1.2f) {
  if (b) rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), b, width);
}

HFONT MakeEditFont(UINT dpi) {
  return CreateFontW(-ScaleInt(13, int(dpi)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     FF_DONTCARE, L"Microsoft YaHei UI");
}

// 占位提示自绘（设计方案 §7）：EDIT 子控件的绘制永远盖在父窗口之上，只能在它自己的
// WM_PAINT 之后补画。EM_SETCUEBANNER 依赖 comctl6 视觉样式，无 manifest 时不保证生效。
WNDPROC g_editPrev = nullptr;
bool g_editHasText = false;
UINT g_editDpi = 96;

void DrawEditPlaceholder(HWND edit) {
  HDC dc = GetDC(edit);
  if (!dc) return;
  RECT rc{};
  GetClientRect(edit, &rc);
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(0x89, 0x91, 0xA0));
  const int x = ScaleInt(4, g_editDpi);
  const int h = rc.bottom - rc.top;
  TEXTMETRICW tm{};
  const int cy = (GetTextMetricsW(dc, &tm) && h > tm.tmHeight) ? (h - tm.tmHeight) / 2 : 0;
  TextOutW(dc, x, cy, kSearchHint, static_cast<int>(wcslen(kSearchHint)));
  if (oldFont) SelectObject(dc, oldFont);
  ReleaseDC(edit, dc);
}

// 清除叉号（v2.1.0）：EDIT 的绘制永远盖在父窗之上，所以只能画在 EDIT 自己里、
// 命中也在子类里吃掉。清空文本会触发 EN_CHANGE，父窗既有的 300ms 防抖负责把列表还原。
RECT ClearBtnRect(HWND edit) {
  RECT rc{};
  GetClientRect(edit, &rc);
  const int side = ScaleInt(kSearchClearDip, g_editDpi);
  const int inset = ScaleInt(kSearchClearInset, g_editDpi);
  const int cy = (rc.top + rc.bottom) / 2;
  return RECT{rc.right - inset - side, cy - side / 2, rc.right - inset, cy + side / 2};
}

void ApplyEditRightMargin(HWND edit, bool hasText) {
  const int inset = ScaleInt(kSearchClearInset, g_editDpi);
  const int side = ScaleInt(kSearchClearDip, g_editDpi);
  const LPARAM m = MAKELPARAM(0, hasText ? inset + side : 0);
  SendMessageW(edit, EM_SETMARGINS, EC_RIGHTMARGIN, m);
}

void DrawEditClear(HWND edit) {
  HDC dc = GetDC(edit);
  if (!dc) return;
  const RECT r = ClearBtnRect(edit);
  const int pad = ScaleInt(3, g_editDpi);
  HPEN pen = CreatePen(PS_SOLID, ScaleInt(2, g_editDpi), RGB(0x6B, 0x74, 0x85));
  HGDIOBJ oldPen = pen ? SelectObject(dc, pen) : nullptr;
  if (pen) {
    MoveToEx(dc, r.left + pad, r.top + pad, nullptr);
    LineTo(dc, r.right - pad, r.bottom - pad);
    MoveToEx(dc, r.right - pad, r.top + pad, nullptr);
    LineTo(dc, r.left + pad, r.bottom - pad);
  }
  if (oldPen) SelectObject(dc, oldPen);
  if (pen) DeleteObject(pen);
  ReleaseDC(edit, dc);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const LRESULT r = g_editPrev ? CallWindowProcW(g_editPrev, hwnd, msg, wParam, lParam)
                               : DefWindowProcW(hwnd, msg, wParam, lParam);
  switch (msg) {
    case WM_PAINT:
      if (GetWindowTextLengthW(hwnd) == 0) DrawEditPlaceholder(hwnd);
      else DrawEditClear(hwnd);
      break;
    case WM_SETFONT:
      ApplyEditRightMargin(hwnd, GetWindowTextLengthW(hwnd) > 0);   // DPI 换字体后按新尺寸让位
      break;
    case WM_LBUTTONUP: {
      if (GetWindowTextLengthW(hwnd) == 0) break;
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const RECT box = ClearBtnRect(hwnd);
      if (PtInRect(&box, pt)) {
        SetWindowTextW(hwnd, L"");           // 触发 EN_CHANGE → 父窗防抖重算
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
      }
      break;
    }
    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT && GetWindowTextLengthW(hwnd) > 0) {
        POINT cur{};
        if (GetCursorPos(&cur)) {
          ScreenToClient(hwnd, &cur);
          const RECT box = ClearBtnRect(hwnd);
          if (PtInRect(&box, cur)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
          }
        }
      }
      break;
    case WM_CHAR:
    case WM_SETTEXT:
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR: {
      // 搜索框内回车 = 让焦回列表（EDIT 默认吞掉 VK_RETURN，父窗收不到 WM_KEYDOWN）
      if (msg == WM_CHAR && wParam == VK_RETURN) {
        HWND parent = GetParent(hwnd);
        if (parent) PostMessageW(parent, WM_APP_SEARCH_ENTER, 0, 0);
      }
      const bool has = GetWindowTextLengthW(hwnd) > 0;
      if (has != g_editHasText) {
        g_editHasText = has;
        ApplyEditRightMargin(hwnd, has);     // 先让位再重画，否则打字会钻到叉号底下
        InvalidateRect(hwnd, nullptr, TRUE);
      }
      break;
    }
    default:
      break;
  }
  return r;
}

}  // namespace

bool MainWindow::Create(HINSTANCE inst, AppContext& ctx) {
  ctx_ = &ctx;
  inst_ = inst;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = &MainWindow::Entry;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  // 任务栏 / Alt-Tab 用大图标，小图标按系统小图标尺寸取；取不到就留空由系统回退（§5.4 回退链）。
  // LR_SHARED：句柄由系统持有，不需要（也不允许）自己 DestroyIcon。
  wc.hIcon = static_cast<HICON>(
      LoadImageW(inst, MAKEINTRESOURCEW(kIconIdApp), IMAGE_ICON,
                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                 LR_DEFAULTCOLOR | LR_SHARED));
  wc.hIconSm = static_cast<HICON>(
      LoadImageW(inst, MAKEINTRESOURCEW(kIconIdApp), IMAGE_ICON,
                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                 LR_DEFAULTCOLOR | LR_SHARED));
  wc.lpszClassName = kMainClass;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LogError(L"ui", L"主窗口类注册失败");
    return false;
  }

  dpi_ = DpiForWindow(nullptr);
  const HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, kMainClass, kWindowTitle, WS_POPUP, 0, 0,
                                    ScaleInt(kWinW, dpi_), ScaleInt(kWinH, dpi_), nullptr, nullptr,
                                    inst, this);
  if (!hwnd) {
    LogError(L"ui", L"主窗口创建失败");
    return false;
  }
  hwnd_ = hwnd;
  ApplyRoundedCorners(hwnd_);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  UpdateWindow(hwnd_);
  // C13：置顶请求在「本窗不在前台」时会被系统丢掉（SetWindowPos 返回 TRUE 但 WS_EX_TOPMOST
  // 没落上，2026-10-05 实机坐实），所以显示后再断言一次，并在 WM_ACTIVATE 里兜底。
  SetWindowPos(hwnd_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  return true;
}

void MainWindow::Destroy() {
  if (!hwnd_) return;
  const HWND hwnd = hwnd_;
  hwnd_ = nullptr;
  edit_ = nullptr;                       // 子窗随父窗销毁，这里只断引用
  help_.Destroy();                       // owner 是本窗，必须先于 DestroyWindow
  paste_.Cancel();                       // 销毁前绝不再发按键
  DestroyWindow(hwnd);
}

LRESULT CALLBACK MainWindow::Entry(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    if (cs) {
      auto* self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
      if (self) self->hwnd_ = hwnd;       // 创建期即需有效句柄（RegisterHotkey）
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
  }
  auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) return self->Handle(hwnd, msg, wParam, lParam);
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      const HRESULT hrFactory = D2D1CreateFactory(
          D2D1_FACTORY_TYPE_SINGLE_THREADED, SC_IID(ID2D1Factory), nullptr,
          reinterpret_cast<void**>(d2dFactory_.put()));
      const HRESULT hrWrite = DWriteCreateFactory(
          DWRITE_FACTORY_TYPE_SHARED, SC_IID(IDWriteFactory),
          reinterpret_cast<IUnknown**>(writeFactory_.put()));
      if (FAILED(hrFactory) || FAILED(hrWrite) || !theme_.InitFonts(writeFactory_.get())) {
        LogError(L"ui", L"D2D/DWrite 初始化失败：需 Win7 SP1 及以上（含平台更新）");
        return -1;                                   // CreateWindowExW 失败 → 上层退出
      }
      list_ = std::make_unique<ListRenderer>(writeFactory_.get());
      if (FAILED(CreateRenderTarget(hwnd))) {
        LogError(L"ui", L"渲染目标创建失败（硬件与 WARP 均不可用）");
        return -1;
      }

      editFont_ = MakeEditFont(dpi_);
      editBgBrush_ = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
      edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 10, 10,
                              hwnd, nullptr,
                              reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                              nullptr);
      if (edit_) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
        g_editDpi = dpi_;
        g_editHasText = false;
        g_editPrev =
            reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit_, GWLP_WNDPROC,
                                                        reinterpret_cast<LONG_PTR>(&EditProc)));
        if (!g_editPrev) {
          g_editPrev = nullptr;   // 子类化失败就不接管绘制，避免 CallWindowProc(NULL)
          LogWarn(L"ui", L"搜索框子类化失败，占位提示不显示");
        }
      }
      RestoreOrDock(hwnd);
      LayoutSearchBox(hwnd);
      RegisterHotkeys(hwnd);
      if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION)) {
        LogWarn(L"ui", L"会话通知注册失败，锁屏时无法自动取消点选（gle=" +
                           std::to_wstring(GetLastError()) + L"）");
      }
      tip_.Create(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), hwnd);
      // 帮助窗建好但保持隐藏（步骤 11）。建不起来只关掉「使用帮助」一项，不影响主流程。
      if (!help_.Create(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), hwnd,
                        d2dFactory_.get(), writeFactory_.get())) {
        LogWarn(L"ui", L"帮助窗不可用");
      }
      RebuildRows();
      return 0;
    }
    case WM_SIZE:
      if (rt_) {
        rt_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        LayoutSearchBox(hwnd);
        RebuildRows();
        CancelRowTip(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_ACTIVATE:
      // C13 兜底：本窗不在前台时，系统会丢掉置顶请求（SetWindowPos 返回 TRUE 但 ex-style 没变）。
      // 不能在这条消息里直接改 z-order（系统处理完 WM_ACTIVATE 还会动一次），所以延后一条消息。
      if (LOWORD(wParam) != WA_INACTIVE && topmost_ &&
          !(GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        PostMessageW(hwnd, WM_APP_RAISE_TOPMOST, 0, 0);
      }
      return 0;
    case kMsgDpiChanged: {
      const auto* suggested = reinterpret_cast<const RECT*>(lParam);
      dpi_ = LOWORD(wParam);
      if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER);
      }
      if (rt_) rt_->SetDpi(dpi_, dpi_);              // 布局一律按 DIP，只需换目标 DPI
      if (editFont_) DeleteObject(editFont_);
      editFont_ = MakeEditFont(dpi_);
      if (edit_) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
        g_editDpi = dpi_;
        InvalidateRect(edit_, nullptr, TRUE);          // 占位提示按新 DPI 重画
      }
      LayoutSearchBox(hwnd);
      RebuildRows();
      CancelRowTip(hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
      mmi->ptMinTrackSize.x = ScaleInt(kMinW, dpi_);
      mmi->ptMinTrackSize.y = ScaleInt(kMinH, dpi_);
      return 0;
    }
    case WM_PAINT:
      OnPaint(hwnd);
      return 0;
    case WM_ERASEBKGND:
      return 1;                                        // D2D 全量覆盖，防闪白
    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(hwnd, &pt);
      const float yDip = float(pt.y) / ScaleF();
      if (yDip >= 0.f && yDip < kTitleF) {
        // 标题栏只有"非按钮、非模式文字"的空白区用于拖拽：模式文字须可点击（§5.3），
        // 若让它走 HTCAPTION 就永远收不到 WM_LBUTTONUP
        const HitResult hit = HitTest(pt);
        return hit.zone == HitZone::None ? HTCAPTION : HTCLIENT;
      }
      return HTCLIENT;
    }
    case WM_MOUSEMOVE: {
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const HitResult hit = HitTest(pt);
      const ClipItem* item =
          (hit.zone == HitZone::RowBody || hit.zone == HitZone::RowStar) ? hit.item : nullptr;
      if (item != hover_) {
        hover_ = item;
        // 换行即撤销旧气泡并重新计时；气泡只在悬停足够久后才浮现，避免扫过一串条目时乱闪
        CancelRowTip(hwnd);
        if (hover_) SetTimer(hwnd, ID_HOVER_TIP, kHoverTipMs, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      const bool clickable = hit.zone == HitZone::ModeText || hit.zone == HitZone::BtnFilter ||
                             hit.zone == HitZone::BtnClear || hit.zone == HitZone::BtnReset ||
                             hit.zone == HitZone::BtnPick || hit.zone == HitZone::BtnMinimize ||
                             hit.zone == HitZone::BtnTopmost || hit.zone == HitZone::BtnClose ||
                             hit.zone == HitZone::BtnSignature || hit.zone == HitZone::RowStar;
      SetCursor(LoadCursorW(nullptr, clickable ? IDC_HAND : IDC_ARROW));   // §6.1 命中按钮改手型
      if (!trackingLeave_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        trackingLeave_ = TrackMouseEvent(&tme) != FALSE;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      trackingLeave_ = false;
      CancelRowTip(hwnd);
      if (hover_) {
        hover_ = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_MOUSEWHEEL: {
      if (!list_) return 0;
      UINT lines = 3;
      SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
      if (lines == WHEEL_PAGESCROLL) lines = 3;
      float unit = 44.f;
      for (const Row& row : list_->rows()) {
        if (row.top + row.height > scrollY_) { unit = row.height; break; }
      }
      scrollY_ -= (GET_WHEEL_DELTA_WPARAM(wParam) / float(WHEEL_DELTA)) * float(lines) * unit;
      ClampScroll(ClientH() - kListTop - kStatusF);
      CancelRowTip(hwnd);
      // 滚轮不产生 WM_MOUSEMOVE：静止的光标下方已经换了行，重算悬停项，
      // 否则高亮与气泡都会指向滚走前的那条
      POINT cur{};
      if (GetCursorPos(&cur)) {
        ScreenToClient(hwnd, &cur);
        const HitResult hit = HitTest(cur);
        const ClipItem* item =
            (hit.zone == HitZone::RowBody || hit.zone == HitZone::RowStar) ? hit.item : nullptr;
        if (item != hover_) {
          hover_ = item;
          if (hover_) SetTimer(hwnd, ID_HOVER_TIP, kHoverTipMs, nullptr);
        }
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_CONTEXTMENU: {
      // 步骤 11（技术方案 §6.1）：点选期间不弹菜单。右键落在搜索 EDIT 上时消息根本不会到这里
      // （子窗的 DefWindowProc 自己弹原生编辑菜单），所以"搜索框放行原生菜单"天然成立。
      if (!ctx_ || ctx_->picking()) return 0;
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      if (pt.x == -1 && pt.y == -1) {              // 键盘 Menu 键：无坐标，取列表区左上
        RECT rc{};
        GetClientRect(hwnd, &rc);
        pt.x = rc.left + ScaleInt(kPad, dpi_);
        pt.y = rc.top + ScaleInt(int(kListTop) + 8, dpi_);
        ClientToScreen(hwnd, &pt);
      }
      ShowMainMenu(hwnd, pt);
      return 0;
    }
    case WM_LBUTTONUP: {
      CancelRowTip(hwnd);
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const HitResult hit = HitTest(pt);
      if (!ctx_) return 0;
      Store& store = ctx_->store();
      switch (hit.zone) {
        case HitZone::BtnMinimize:
          ShowWindow(hwnd, SW_HIDE);                    // FR-15①：收起，监听继续
          if (ctx_) ctx_->SyncRelayHook();              // C15：窗一收，接力作用域即不成立
          break;
        case HitZone::BtnTopmost:
          SetTopmost(hwnd, !topmost_);                  // FR-15②
          break;
        case HitZone::BtnClose:
          ctx_->Exit();                                 // FR-15③：✕ 即彻底退出
          break;
        case HitZone::ModeText:
          TogglePasteMode();                            // §5.3：点文字切普通/快速
          break;
        case HitZone::BtnFilter:
          ShowFilterMenu(hwnd);                         // FR-07
          break;
        case HitZone::BtnClear: {
          scrollY_ = 0.f;
          const size_t removed = store.ClearAll();    // C12：收藏不参与清除（契约无确认框）
          // 收藏在【全部】视图本就不可见，不提示会看起来"按了没反应"
          const size_t kept = store.TotalCount();     // 清除后集合只剩收藏区
          ctx_->ShowStatusHint(removed
                                   ? L"已清除 " + std::to_wstring(removed) + L" 条，收藏 " +
                                         std::to_wstring(kept) + L" 条永久保留"
                                   : L"没有可清除的条目（收藏永久保留）");
          break;
        }
        case HitZone::BtnReset:
          store.Reset();                                // FR-14：按时间复位 + 清灰显
          break;
        case HitZone::RowBody:
          SetFocus(hwnd);                               // §6.7：点行后按键归列表
          focus_ = FocusOwner::List;
          store.Select(hit.item);                       // AC-3：单击只切换选中，不粘贴
          break;
        case HitZone::RowStar: {
          store.ToggleFavorite(hit.item);               // FR-08
          // 收藏条目只在【收藏】视图显示（2026-10-04 用户决议），切换后它会立刻离开当前列表，
          // 不给去向提示就像被删了一样
          if (ctx_) {
            ctx_->ShowStatusHint(hit.item->isFavorite ? L"已收藏（切换到【收藏】可见）"
                                                      : L"已取消收藏（切换到【全部】可见）");
          }
          break;
        }
        case HitZone::BtnPick:
          ctx_->TogglePick();                         // §5.5：点选中再按一次 = 取消
          break;
        case HitZone::BtnSignature:
          OpenProjectPage();                          // 单击署名 = 交给默认浏览器开仓库页
          break;
        case HitZone::Status:
          break;                                      // 底栏其余部分只吃掉，不穿透到条目
        default:
          break;
      }
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      // FR-09：普通模式双击 = 粘贴（位置不变）；快速模式双击仍只算单击选中
      const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const HitResult hit = HitTest(pt);
      if (!ctx_ || hit.zone != HitZone::RowBody || !hit.item) return 0;
      if (ctx_->store().pasteMode() != PasteMode::Normal) return 0;
      DoPaste(hit.item, false);
      return 0;
    }
    case WM_KEYDOWN: {
      if (!ctx_) return 0;
      Store& store = ctx_->store();
      if (wParam == VK_SPACE && focus_ == FocusOwner::List) {
        // FR-10：快速模式空格粘贴选中项 → 灰显 + 沉底 + 跳下一条未粘贴
        if (store.pasteMode() != PasteMode::Quick) return 0;
        const ClipItem* sel = store.Selected();
        if (!sel) return 0;
        DoPaste(sel, true);
        return 0;
      }
      if (wParam == VK_ESCAPE) {
        if (ctx_->picking()) {                        // §5.5：点选期间 Esc = 取消并复位光标
          ctx_->CancelPick();
          return 0;
        }
        // 契约只定义"点选中→取消"；无选中时不隐藏（设计方案 §6.7 未定义关闭行为）
        if (store.Selected()) store.Select(nullptr);
        return 0;
      }
      // 键盘唤菜单：VK_APPS 必须交给 DefWindowProc，由它合成 WM_CONTEXTMENU（lParam = -1,-1，
      // 即"无鼠标坐标"分支）。Shift+F10 走的是 VK_F10，但 F10 单独按下会让系统进菜单模式，
      // 本窗没有菜单栏、后果不可见即不可测，故不放行（2026-10-04 决议：只验过的才上线）。
      // 其余按键本窗自己消化，不落给 DefWindowProc。
      if (wParam == VK_APPS) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
      }
      return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
      SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
      return reinterpret_cast<LRESULT>(editBgBrush_ ? editBgBrush_ : GetStockObject(WHITE_BRUSH));
    case WM_TIMER:
      if (wParam == ID_PASTE_GUARD && ctx_) ctx_->OnPasteGuardTimer();
      if (wParam == ID_PICK && ctx_) ctx_->OnPickTimeout();   // T2：8s 内没点任何东西就取消
      if (wParam == ID_STATUS_HINT && ctx_) ctx_->OnStatusHintTimer();
      if (wParam == ID_FOCUS_WAIT) paste_.OnTimerId(ID_FOCUS_WAIT);   // §5.2：60ms 后按键
      if (wParam == ID_HOVER_TIP) {
        KillTimer(hwnd, ID_HOVER_TIP);      // 一次性：浮现后不再重复
        ShowRowTip(hwnd);
      }
      if (wParam == ID_SEARCH) {
        KillTimer(hwnd, ID_SEARCH);         // FR-06：重启式防抖，每次输入都重新计时
        ApplySearchFromEdit();
      }
      return 0;
    case WM_COMMAND: {
      if (reinterpret_cast<HWND>(lParam) != edit_) return 0;
      const UINT code = HIWORD(wParam);
      if (code == EN_CHANGE) {
        KillTimer(hwnd, ID_SEARCH);
        SetTimer(hwnd, ID_SEARCH, kDebounceMs, nullptr);
      } else if (code == EN_SETFOCUS) {
        focus_ = FocusOwner::SearchEdit;
      } else if (code == EN_KILLFOCUS) {
        focus_ = FocusOwner::List;
      }
      return 0;
    }
    case WM_APP_SEARCH_ENTER:
      focus_ = FocusOwner::List;
      SetFocus(hwnd);
      return 0;
    case WM_APP_RAISE_TOPMOST:                                        // C13：激活后补置顶
      if (topmost_ && !(GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }
      return 0;
    case WM_APP_PASTE_DONE:
      OnPasteDone(wParam != 0);
      return 0;
    case WM_APP_PICK_DONE:
      if (ctx_) ctx_->OnPickMessage(reinterpret_cast<HWND>(wParam));   // §5.5：钩子投来的根窗口
      return 0;
    case WM_APP_RELAY_TRIGGER:
      // C15：接力钩子投来的根窗口。钩子本身只投递，剪贴板与按键全在这条主线程消息里做。
      if (ctx_) ctx_->OnRelayTrigger(reinterpret_cast<HWND>(wParam));
      return 0;
    case kMsgWtsSessionChange:
      if (wParam == kWtsSessionLock && ctx_) ctx_->OnSessionLock();    // 锁屏：光标绝不残留
      else if (wParam == kWtsSessionUnlock && ctx_) ctx_->OnSessionUnlock();
      return 0;
    case WM_ENDSESSION:
      if (wParam && ctx_) ctx_->OnEndSession();                        // 注销/关机：同一取消链
      return 0;
    case kMsgHotkey:
      // 兜底触发键的 target 必须在这里当场取前台窗：稍晚一步主窗就可能自己上来了。
      if (wParam == DWORD(kHotkeyId) && ctx_) ctx_->ToggleMainWindow();
      else if (wParam == DWORD(kHotkeyIdRelay) && ctx_)
        ctx_->OnRelayHotkey(GetForegroundWindow());
      return 0;
    case WM_CLOSE:
      if (ctx_) ctx_->Exit();
      return 0;
    case WM_DESTROY:
      UnregisterHotkeys(hwnd);
      tip_.Destroy();
      list_.reset();
      ReleaseTargetLevel();
      d2dFactory_.reset();
      writeFactory_.reset();
      if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
      edit_ = nullptr;
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

HRESULT MainWindow::CreateRenderTarget(HWND hwnd) {
  if (!d2dFactory_ || !hwnd) return E_UNEXPECTED;
  RECT rc{};
  GetClientRect(hwnd, &rc);
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
  const D2D1_HWND_RENDER_TARGET_PROPERTIES hw = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(UINT(rc.right), UINT(rc.bottom)));
  HRESULT hr = d2dFactory_->CreateHwndRenderTarget(props, hw, rt_.put());
  if (FAILED(hr) || !rt_) {
    LogWarn(L"ui", L"硬件渲染目标失败，改用 WARP 软件渲染");
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    hr = d2dFactory_->CreateHwndRenderTarget(props, hw, rt_.put());
    if (FAILED(hr) || !rt_) return FAILED(hr) ? hr : E_FAIL;
  }
  rt_->SetDpi(dpi_, dpi_);
  const HRESULT hrBrush = theme_.CreateBrushes(rt_.get());
  if (FAILED(hrBrush)) {
    rt_.reset();
    return hrBrush;
  }
  return S_OK;
}

void MainWindow::ReleaseTargetLevel() {
  titleIcon_.reset();          // 位图属目标级资源，必须跟着旧渲染目标一起弃
  titleIconDpi_ = 0;
  rt_.reset();
}

void MainWindow::RebuildRows() {
  if (!list_ || !ctx_) return;
  list_->Rebuild(ctx_->store(), theme_, ClientW() - 2 * kPadF);
  ClampScroll(ClientH() - kListTop - kStatusF);
}

void MainWindow::LayoutSearchBox(HWND hwnd) {
  if (!edit_) return;
  const float scale = ScaleF();
  const int x = int(std::lround(kPadF * scale));
  const int y = int(std::lround((kTitleF + kToolPadF) * scale));
  const int w = int(std::lround((ClientW() - 2 * kPadF) * scale));
  const int h = int(std::lround(kSearchF * scale));
  SetWindowPos(edit_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  (void)hwnd;
}

float MainWindow::ClientW() const {
  if (!rt_) return float(kWinW);
  const D2D1_SIZE_F size = rt_->GetSize();
  return size.width > 1.f ? size.width : float(kWinW);
}

float MainWindow::ClientH() const {
  if (!rt_) return float(kWinH);
  const D2D1_SIZE_F size = rt_->GetSize();
  return size.height > 1.f ? size.height : float(kWinH);
}

void MainWindow::ClampScroll(float viewportH) {
  const float max = list_ ? std::max(0.f, list_->ContentHeight() - viewportH) : 0.f;
  scrollY_ = std::min(std::max(0.f, scrollY_), max);
}

void MainWindow::RegisterHotkeys(HWND hwnd) {
  if (hotkeyRegistered_) return;
  hotkeyRegistered_ = RegisterHotKey(hwnd, kHotkeyId, MOD_CONTROL, kVkOem3) != FALSE;
  if (!hotkeyRegistered_) LogWarn(L"ui", L"Ctrl+` 热键被占用，呼出不可用（可点击托盘）");
  // C15 兜底触发键：接力主路径是 Alt+左键（钩子），这把热键留给"钩子装不上"或
  // "该程序的 Alt+左键有自己的动作"的场合。MOD_NOREPEAT 防按住连发——按住不放能贴走十几条。
  // Win7 不支持 MOD_NOREPEAT（MSDN），注册失败就退回不带它：宁可能连发，也不要整条功能没。
  // v2.3.3：键位由 Ctrl+Alt+空格 改成 Alt+`（用户 2026-10-05 决议）。空格那把要占
  // MOD_CONTROL|MOD_ALT 两个修饰键、按着别扭；Alt+空格虽然是单修饰键但它是 Windows 全局
  // 「窗口系统菜单」键，RegisterHotKey 会把它从所有程序手里静默抢走，代价不对等。
  // Alt+` 与呼出键 Ctrl+` 同键位、只差修饰键，不冲突任何系统或输入法快捷键。
  const UINT mods = MOD_ALT;
  relayHotkeyRegistered_ =
      RegisterHotKey(hwnd, kHotkeyIdRelay, mods | kModNoRepeat, kVkOem3) != FALSE;
  if (!relayHotkeyRegistered_) {
    relayHotkeyRegistered_ = RegisterHotKey(hwnd, kHotkeyIdRelay, mods, kVkOem3) != FALSE;
    if (relayHotkeyRegistered_) LogWarn(L"ui", L"MOD_NOREPEAT 不被接受（Win7？），兜底键允许连发");
  }
  if (!relayHotkeyRegistered_) LogWarn(L"ui", L"Alt+` 被占用，接力只能用 Alt+左键");
}

void MainWindow::UnregisterHotkeys(HWND hwnd) {
  if (hotkeyRegistered_) {
    UnregisterHotKey(hwnd, kHotkeyId);
    hotkeyRegistered_ = false;
  }
  if (relayHotkeyRegistered_) {                        // 两把独立注册，也就独立注销
    UnregisterHotKey(hwnd, kHotkeyIdRelay);
    relayHotkeyRegistered_ = false;
  }
}

void MainWindow::DockToWorkArea(HWND hwnd) {
  RECT work{};
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return;
  const int w = ScaleInt(kWinW, dpi_);
  const int h = ScaleInt(kWinH, dpi_);
  const int x = work.right - w;                                  // 贴屏幕右缘
  const int y = work.top + ((work.bottom - work.top - h) / 2);   // 垂直居中
  SetWindowPos(hwnd, topmost_ ? HWND_TOPMOST : HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
}

// 设计方案 §8.3：位置/尺寸/置顶都吃 settings.json。
// 存的是 DIP，这里乘 dpi 还原成物理矩形；矩形已完全不在任何显示器上（拔掉副屏）就回默认停靠。
void MainWindow::RestoreOrDock(HWND hwnd) {
  if (!ctx_) {
    DockToWorkArea(hwnd);
    return;
  }
  const Settings& s = ctx_->settings();
  topmost_ = s.topmost;                     // C13：无文件时默认值就是 true
  RECT rc{ScaleInt(s.left, dpi_), ScaleInt(s.top, dpi_), 0, 0};
  rc.right = rc.left + ScaleInt(s.width, dpi_);
  rc.bottom = rc.top + ScaleInt(s.height, dpi_);
  if (s.hasRect && FitRectToDesktop(rc)) {
    SetWindowPos(hwnd, topmost_ ? HWND_TOPMOST : HWND_TOP, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top, SWP_SHOWWINDOW);
    return;
  }
  DockToWorkArea(hwnd);
}

void MainWindow::SnapshotGeometry(Settings& s) const {
  if (!hwnd_) return;
  RECT rc{};
  if (!GetWindowRect(hwnd_, &rc) || rc.right <= rc.left || rc.bottom <= rc.top) return;
  s.left = MulDiv(rc.left, 96, int(dpi_));
  s.top = MulDiv(rc.top, 96, int(dpi_));
  s.width = MulDiv(rc.right - rc.left, 96, int(dpi_));
  s.height = MulDiv(rc.bottom - rc.top, 96, int(dpi_));
  s.hasRect = true;
}

void MainWindow::SetTopmost(HWND hwnd, bool on) {
  topmost_ = on;
  SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  InvalidateRect(hwnd, nullptr, FALSE);
  if (ctx_) ctx_->SaveTopmost(on);   // §8.3：置顶状态变更即落盘（记住用户关掉的偏好）
}

HitResult MainWindow::HitTest(POINT ptClient) const {
  HitResult out;
  if (!rt_) return out;
  const float x = float(ptClient.x) / ScaleF();
  const float y = float(ptClient.y) / ScaleF();
  const float w = ClientW();

  if (y < kTitleF) {
    const float btnTop = (kTitleF - kBtnF) / 2.f;
    const float closeLeft = w - kPadF - kBtnF;
    const float topLeft = closeLeft - kTitleBtnGapF - kBtnF;
    const float minLeft = topLeft - kTitleBtnGapF - kBtnF;
    if (y >= btnTop && y <= btnTop + kBtnF) {
      if (x >= closeLeft) out.zone = HitZone::BtnClose;
      else if (x >= topLeft) out.zone = HitZone::BtnTopmost;
      else if (x >= minLeft) out.zone = HitZone::BtnMinimize;
    }
    // 仅模式文字本身可点：图标与其余标题栏留给 WM_NCHITTEST 当拖拽区（zone 保持 None）
    if (out.zone == HitZone::None && x >= kModeLeft && x < kModeLeft + float(kModeTextW))
      out.zone = HitZone::ModeText;
    return out;
  }
  if (y < kListTop) {
    const float searchTop = kTitleF + kToolPadF;
    if (y >= searchTop && y <= searchTop + kSearchF) { out.zone = HitZone::SearchBox; return out; }
    const float btnTop = searchTop + kSearchF + kToolPadF;
    if (y < btnTop || y > btnTop + kToolBtnF) return out;
    const int widths[4] = {kBtnFilterW, kBtnClearW, kBtnResetW, kBtnPickW};
    const HitZone zones[4] = {HitZone::BtnFilter, HitZone::BtnClear, HitZone::BtnReset,
                              HitZone::BtnPick};
    float bx = kPadF;
    for (int i = 0; i < 4; ++i) {
      if (x >= bx && x <= bx + widths[i]) { out.zone = zones[i]; return out; }
      bx += widths[i] + kToolGapF;
    }
    return out;
  }
  // 底栏带必须先吃掉：列表视口画到 ClientH()-kStatusF 为止，但下面的命中按 contentY 一路算到底，
  // 不拦的话点底栏署名会落到最下面那条的星标上（v2.1.0 修掉的误触）。
  if (y > ClientH() - kStatusF) {
    const D2D1_RECT_F sig = SignatureRect();
    out.zone = (sig.right > sig.left && x >= sig.left && x <= sig.right) ? HitZone::BtnSignature
                                                                        : HitZone::Status;
    return out;
  }
  if (!list_) return out;
  const float contentY = y - kListTop + scrollY_;
  const auto& rows = list_->rows();
  for (size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    if (contentY < row.top || contentY > row.top + row.height) continue;
    if (x >= row.star.left && x <= row.star.right && contentY >= row.star.top &&
        contentY <= row.star.bottom) {
      out.zone = HitZone::RowStar;
    } else if (x >= row.card.left && x <= row.card.right) {
      out.zone = HitZone::RowBody;
    } else {
      out.zone = HitZone::ListBackground;
    }
    out.item = row.item;
    out.displayIndex = i;
    return out;
  }
  out.zone = HitZone::ListBackground;
  return out;
}

void MainWindow::OnPaint(HWND hwnd) {
  PAINTSTRUCT ps{};
  BeginPaint(hwnd, &ps);
  if (!rt_ || !list_ || !ctx_) {
    EndPaint(hwnd, &ps);
    return;
  }
  if (rt_->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) {     // 遮挡期跳帧不重建（§6.5）
    occluded_ = true;
    EndPaint(hwnd, &ps);
    return;
  }
  if (occluded_) occluded_ = false;

  const float w = ClientW();
  const float h = ClientH();
  rt_->BeginDraw();
  rt_->FillRectangle(D2D1::RectF(0.f, 0.f, w, h), theme_.WindowBg());
  DrawTitleBar(rt_.get(), w);
  DrawToolbar(rt_.get(), w);

  const D2D1_RECT_F viewport{0.f, kListTop, w, h - kStatusF};
  list_->Draw(rt_.get(), theme_, viewport, scrollY_, ctx_->store().Selected(), hover_);
  DrawStatusBar(rt_.get(), w, h);

  const HRESULT hr = rt_->EndDraw();
  if (hr != S_OK) {
    if (IsDeviceLost(hr)) {
      LogWarn(L"ui", L"渲染目标失效，重建后全量重绘");
      ReleaseTargetLevel();
      if (SUCCEEDED(CreateRenderTarget(hwnd))) {
        RebuildRows();
        InvalidateRect(hwnd, nullptr, FALSE);
      } else {
        LogError(L"ui", L"渲染目标重建失败，界面不再更新");
      }
    } else {
      LogWarn(L"ui", L"EndDraw 返回异常");
    }
  }
  EndPaint(hwnd, &ps);
}

void MainWindow::EnsureTitleIcon() {
  if (!rt_ || !inst_) return;
  if (titleIcon_ && titleIconDpi_ == dpi_) return;
  titleIcon_.reset();
  const int px = std::max(1, ScaleInt(kTitleIconDip, dpi_));
  const HICON icon = static_cast<HICON>(
      LoadImageW(inst_, MAKEINTRESOURCEW(kIconIdApp), IMAGE_ICON, px, px,
                 LR_DEFAULTCOLOR | LR_SHARED));
  if (!icon) {
    titleIconDpi_ = 0;
    LogWarn(L"ui", L"标题栏图标载入失败，标题栏只画文字");
    return;
  }
  std::vector<UINT8> bits;
  int w = 0, h = 0;
  const bool got = IconToPremultipliedBGRA(icon, bits, w, h);
  // LR_SHARED 的句柄归系统缓存，绝不能 DestroyIcon
  if (!got || w <= 0 || h <= 0) {
    titleIconDpi_ = 0;
    LogWarn(L"ui", L"标题栏图标像素提取失败，标题栏只画文字");
    return;
  }
  D2D1_BITMAP_PROPERTIES props{};
  props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
  props.dpiX = static_cast<FLOAT>(dpi_);
  props.dpiY = static_cast<FLOAT>(dpi_);        // 与目标同 DPI：画进 18 DIP 框即 1:1 设备像素
  D2D1_SIZE_U size{static_cast<UINT32>(w), static_cast<UINT32>(h)};
  if (FAILED(rt_->CreateBitmap(size, bits.data(), static_cast<UINT32>(w) * 4, props,
                               titleIcon_.put())))
    titleIcon_.reset();
  titleIconDpi_ = titleIcon_ ? dpi_ : 0;      // 失败就每帧重试，别把空位当成"已按此 DPI 备好"
}

void MainWindow::DrawTitleBar(ID2D1RenderTarget* rt, float w) {
  rt->FillRectangle(D2D1::RectF(0.f, 0.f, w, kTitleF), theme_.TitleBg());
  rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

  EnsureTitleIcon();
  const float iconTop = (kTitleF - kTitleIconF) / 2.f;
  if (titleIcon_) {   // 位图按 px = 18×scale 取的，画进 18 DIP 框即 1:1 设备像素，不重采样
    rt->DrawBitmap(titleIcon_.get(),
                   D2D1::RectF(kPadF, iconTop, kPadF + kTitleIconF, iconTop + kTitleIconF));
  }

  const bool quick = ctx_ && ctx_->store().pasteMode() == PasteMode::Quick;
  const std::wstring mode = quick ? L"剪贴板 - 快速模式" : L"剪贴板 - 普通模式";
  rt->DrawText(mode.c_str(), static_cast<UINT32>(mode.size()), theme_.Title(),
               D2D1::RectF(kModeLeft, 0.f, w - 3.f * kBtnF - 4.f * kTitleBtnGapF, kTitleF),
               theme_.Ink(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

  const float btnTop = (kTitleF - kBtnF) / 2.f;
  D2D1_RECT_F box{w - kPadF - kBtnF, btnTop, w - kPadF, btnTop + kBtnF};
  ID2D1SolidColorBrush* ink = theme_.Ink();
  for (int i = 0; i < 3; ++i) {                 // 依次：关闭 → 悬浮 → 收起
    const D2D1_RECT_F sharp = Sharpen(box);
    const float cx = (sharp.left + sharp.right) / 2.f;
    const float cy = (sharp.top + sharp.bottom) / 2.f;
    const float r = kBtnF / 2.f;
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    if (i == 0) {
      Stroke(rt, ink, cx - r * .4f, cy - r * .4f, cx + r * .4f, cy + r * .4f);
      Stroke(rt, ink, cx - r * .4f, cy + r * .4f, cx + r * .4f, cy - r * .4f);
    } else if (i == 1) {
      // 置顶键（v2.1.0 改形）：斜头图钉。实心帽 + 斜针 + 底横杠，比原来的"圆头直针"在小尺寸下好认；
      // 启用态整枚换 accent 色并保留下划线。帮助窗第 9 步的文案与此一致。
      ID2D1SolidColorBrush* c = topmost_ ? theme_.Accent() : ink;
      const float headR = r * .27f;
      const float hx = cx - r * .10f;
      const float hy = cy - r * .22f;
      rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
      rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(hx, hy), headR, headR), c);
      rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      Stroke(rt, c, hx + headR * .55f, hy + headR * .75f, cx + r * .30f, cy + r * .34f, 1.6f);
      Stroke(rt, c, cx - r * .34f, cy + r * .40f, cx + r * .36f, cy + r * .40f, 1.6f);
      if (topmost_) {
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        Stroke(rt, theme_.Accent(), sharp.left + 3.f, sharp.bottom - 2.f, sharp.right - 3.f,
               sharp.bottom - 2.f, 1.6f);
      }
    } else {
      Stroke(rt, ink, cx - r * .42f, cy - r * .1f, cx, cy + r * .32f);
      Stroke(rt, ink, cx, cy + r * .32f, cx + r * .42f, cy - r * .1f);
      Stroke(rt, ink, cx - r * .42f, cy + r * .58f, cx + r * .42f, cy + r * .58f);
    }
    box.left -= kBtnF + kTitleBtnGapF;
    box.right -= kBtnF + kTitleBtnGapF;
  }
}

void MainWindow::DrawToolbar(ID2D1RenderTarget* rt, float w) {
  const float searchTop = kTitleF + kToolPadF;
  const float btnTop = searchTop + kSearchF + kToolPadF;
  const wchar_t* labels[4] = {FilterLabel(), L"清除", L"复位", L"绑定"};   // FR-07：首按钮显示当前过滤值
  const int widths[4] = {kBtnFilterW, kBtnClearW, kBtnResetW, kBtnPickW};

  float x = kPadF;
  rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  for (int i = 0; i < 4; ++i) {
    const D2D1_RECT_F box = Sharpen(D2D1_RECT_F{x, btnTop, x + widths[i], btnTop + kToolBtnF});
    rt->FillRoundedRectangle(D2D1::RoundedRect(box, 4, 4), theme_.BtnBg());
    rt->DrawRoundedRectangle(D2D1::RoundedRect(box, 4, 4), theme_.CardStroke(), 1.f);
    float textLeft = box.left + 2.f;
    if (i == 3) {
      // 靶心：红=未绑定，绿=已绑定（R5：只有 IsWindow 通过才绿）；图标随按钮收窄到 13
      const float cx = box.left + 13.f;
      const float cy = (box.top + box.bottom) / 2.f;
      ID2D1SolidColorBrush* b = (ctx_ && ctx_->bound()) ? theme_.BindOn() : theme_.BindOff();
      rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.f, 4.f), b, 1.2f);
      rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 1.3f, 1.3f), b);
      Stroke(rt, b, cx - 6.5f, cy, cx + 6.5f, cy, 1.f);
      Stroke(rt, b, cx, cy - 6.5f, cx, cy + 6.5f, 1.f);
      textLeft = cx + 9.f;
    }
    // Button 字号已设居中，这里给整条内框即可（绑定按钮从图标右侧起算）
    rt->DrawText(labels[i], static_cast<UINT32>(wcslen(labels[i])), theme_.Button(),
                 D2D1::RectF(textLeft, box.top + 1.f, box.right - 2.f, box.bottom - 1.f),
                 theme_.Ink(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    x += widths[i] + kToolGapF;
  }
  (void)w;
}

void MainWindow::DrawStatusBar(ID2D1RenderTarget* rt, float w, float h) {
  const D2D1_RECT_F bar{0.f, h - kStatusF, w, h};
  rt->FillRectangle(bar, theme_.StatusBg());
  rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  // 左栏三态：降级提示 > 已绑定：进程名 > 未绑定（设计方案 §6.5 状态指示）
  std::wstring left = L"未绑定";
  if (ctx_) {
    if (!ctx_->statusHint().empty()) {
      left = ctx_->statusHint();
    } else if (ctx_->bound()) {
      left = ctx_->boundProcessName().empty() ? L"已绑定（进程名未知）"
                                              : L"已绑定：" + ctx_->boundProcessName();
    }
  }
  rt->DrawText(left.c_str(), static_cast<UINT32>(left.size()), theme_.Meta(),
               D2D1::RectF(kPadF, bar.top, w - kPadF - kStatusRightW, h), theme_.Muted(),
               D2D1_DRAW_TEXT_OPTIONS_NONE);
  // 署名规则：版本号在前、署名在后（agent.md 四.3）
  const std::wstring right = std::wstring(kVersionText) + L"  " + kAppSignature;
  rt->DrawText(right.c_str(), static_cast<UINT32>(right.size()), theme_.MetaRight(),
               D2D1::RectF(w - kPadF - kStatusRightW, bar.top, w - kPadF, h), theme_.Muted(),
               D2D1_DRAW_TEXT_OPTIONS_NONE);
}

// 署名是右侧那串的最后一段，整串按右对齐画到 w-kPadF 为止，所以它的右缘就是 w-kPadF。
D2D1_RECT_F MainWindow::SignatureRect() const {
  if (!rt_) return D2D1_RECT_F{};
  const float sigW = MeasureTextW(writeFactory_.get(), kAppSignature, theme_.Meta());
  if (sigW <= 0.f) return D2D1_RECT_F{};
  const float right = ClientW() - kPadF;
  return D2D1_RECT_F{right - sigW, ClientH() - kStatusF, right, ClientH()};
}

// 把 URL 交给系统默认浏览器。程序自身不链 wininet/winhttp、不调 socket（AC-8 边界见 DESIGN ADR）。
void MainWindow::OpenProjectPage() {
  const INT_PTR res = reinterpret_cast<INT_PTR>(
      ShellExecuteW(hwnd_, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL));
  if (res <= 32) {
    LogWarn(L"ui", L"ShellExecute 打开仓库页失败，code=" + std::to_wstring(res));
    if (ctx_) ctx_->ShowStatusHint(L"没唤起浏览器，地址：" + std::wstring(kProjectUrl));
  }
}

// 悬停到期：仅对"被截断"的行浮现全文（设计方案 §7）
void MainWindow::ShowRowTip(HWND hwnd) {
  if (!list_ || !hwnd) return;
  const Row* row = list_->FindRow(hover_);
  if (!row || !row->truncated) return;
  const float scale = ScaleF();
  const LONG top = std::lround((kListTop + row->card.top - scrollY_) * scale);
  const LONG bottom = std::lround((kListTop + row->card.bottom - scrollY_) * scale);
  POINT tl{std::lround(row->card.left * scale), top};
  POINT br{std::lround(row->card.right * scale), bottom};
  ClientToScreen(hwnd, &tl);
  ClientToScreen(hwnd, &br);
  tip_.Show(row->item->content, RECT{tl.x, tl.y, br.x, br.y}, dpi_);
}

void MainWindow::CancelRowTip(HWND hwnd) {
  KillTimer(hwnd, ID_HOVER_TIP);
  tip_.Hide();
}

// FR-06：防抖到点后把编辑框内容一次性下发给 Store（Store 已折叠大小写与全半角）
void MainWindow::ApplySearchFromEdit() {
  if (!edit_ || !ctx_) return;
  const int len = GetWindowTextLengthW(edit_);
  std::wstring text(size_t(len) + 1u, L'\0');
  const int got = GetWindowTextW(edit_, text.data(), static_cast<int>(text.size()));
  if (got < 0) text.clear();
  else text.resize(size_t(got));
  Store& store = ctx_->store();
  if (text == store.keyword()) return;              // 剪掉退格回来、或粘贴同值：不重建列表
  scrollY_ = 0.f;                                   // 命中集变了，旧滚动量指向不存在的行
  store.ApplySearch(std::move(text));
}

const wchar_t* MainWindow::FilterLabel() const {
  if (!ctx_) return L"全部 ▾";
  switch (ctx_->store().filter()) {
    case FilterType::Text: return L"文本 ▾";
    case FilterType::TableCell: return L"表格 ▾";
    case FilterType::Favorite: return L"收藏 ▾";
    case FilterType::All: break;
  }
  return L"全部 ▾";
}

// FR-07：自绘按钮弹原生菜单（技术方案 ADR：不用 COMBOBOX，免主题割裂）
void MainWindow::ShowFilterMenu(HWND hwnd) {
  if (!ctx_) return;
  struct Entry {
    const wchar_t* label;
    FilterType filter;
  };
  const Entry entries[4] = {
      {L"全部", FilterType::All},
      {L"文本", FilterType::Text},
      {L"表格单元格", FilterType::TableCell},
      {L"收藏", FilterType::Favorite}};
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  for (UINT i = 0; i < 4; ++i) AppendMenuW(menu, MF_STRING, i + 1, entries[i].label);
  const FilterType cur = ctx_->store().filter();
  for (UINT i = 0; i < 4; ++i) {
    if (entries[i].filter == cur) CheckMenuItem(menu, i + 1, MF_CHECKED);
  }

  const float scale = ScaleF();
  const LONG x = std::lround(kPadF * scale);
  const LONG y = std::lround((kTitleF + kToolPadF + kSearchF + kToolPadF + kToolBtnF) * scale);
  POINT at{x, y};
  ClientToScreen(hwnd, &at);

  // 弹出前必须让本窗成为前台，否则点击窗口外时菜单不消失（本窗是 WS_POPUP 非激活态）
  SetForegroundWindow(hwnd);
  const INT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                   at.x, at.y, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);   // 标准收尾：把菜单遗留的输入状态交回本窗
  DestroyMenu(menu);
  if (cmd < 1 || cmd > 4) return;      // 取消：不改过滤
  scrollY_ = 0.f;
  const FilterType picked = entries[UINT(cmd) - 1].filter;
  ctx_->store().SetFilter(picked);
  ctx_->SaveFilterType(picked);   // §8.3：筛选变更即落盘
}

// 步骤 11（§6.8）：右键主窗弹三项原生菜单 —— 粘贴模式 / 复制模式 / 使用帮助。
// 置顶与清除/复位不走这里：前者已有标题栏按钮，后者是带确认链的动作，误触代价不对等。
void MainWindow::ShowMainMenu(HWND hwnd, POINT ptScreen) {
  if (!ctx_) return;
  Store& store = ctx_->store();
  const bool quick = store.pasteMode() == PasteMode::Quick;
  const bool singleCol = store.copyMode() == CopyMode::TableSingleColumn;
  constexpr UINT kIdMode = 1, kIdCopy = 2, kIdHelp = 3;
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, kIdMode,
              quick ? L"粘贴模式：快速（点此切到普通）" : L"粘贴模式：普通（点此切到快速）");
  AppendMenuW(menu, MF_STRING, kIdCopy,
              singleCol ? L"复制模式：表格（点此切到一般）" : L"复制模式：一般（点此切到表格）");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kIdHelp, L"使用帮助");

  SetForegroundWindow(hwnd);
  const INT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                   ptScreen.x, ptScreen.y, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
  switch (UINT(cmd)) {
    case kIdMode:
      TogglePasteMode();
      break;
    case kIdCopy: {
      const CopyMode next = singleCol ? CopyMode::Normal : CopyMode::TableSingleColumn;
      store.SetCopyMode(next);
      ctx_->SaveCopyMode(next);          // §8.3：复制模式变更即落盘
      break;
    }
    case kIdHelp:
      help_.Show();
      break;
    default:
      break;                              // 取消：什么都不改
  }
}

// §5.3：点击标题栏模式文字切换普通/快速；FR-10 的进入即选中第一条在此落地
void MainWindow::TogglePasteMode() {
  if (!ctx_ || !hwnd_) return;
  Store& store = ctx_->store();
  const PasteMode next = store.pasteMode() == PasteMode::Quick ? PasteMode::Normal : PasteMode::Quick;
  store.SetPasteMode(next);
  if (next == PasteMode::Quick) {
    store.AnchorQuickSelection();          // FR-10：进入即选中第一行（规则本体在 Store）
  } else {
    store.Select(nullptr);   // 普通模式无强制选中
  }
  ctx_->SavePasteMode(next);   // §8.3：模式变更即落盘
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// FR-09/10/11：粘贴链路入口。全异步——Start 立即返回，结果经 WM_APP_PASTE_DONE 回来。
// targetOverride 只给 C15 接力用：那一下的目标由触发点决定，不走绑定与唤起前窗口。
void MainWindow::DoPaste(const ClipItem* item, bool moveToEnd, HWND targetOverride) {
  if (!ctx_ || !hwnd_ || !item) return;
  if (paste_.busy()) return;                       // 上一次未完成：静默丢弃，不叠阶段机
  const std::wstring content = item->content;      // 原样粘贴：不折叠、不 trim、不补换行
  ctx_->BeginSelfPaste(content);                   // C4 双层防护：先置标志再写盘
  pendingPasteItem_ = item;
  pasteMoveToEnd_ = moveToEnd;
  paste_.onDone = [this](bool ok) {
    if (hwnd_) PostMessageW(hwnd_, WM_APP_PASTE_DONE, ok ? WPARAM(1) : WPARAM(0), 0);
  };
  paste_.Start(hwnd_, targetOverride ? targetOverride : ctx_->PasteTarget(), content);
}

// C15：接力永远 moveToEnd=true —— 沉底是让下一条上位的那一步，与普通模式双击（原位）不同。
void MainWindow::PasteForRelay(const ClipItem* item, HWND target) {
  DoPaste(item, true, target);
}

void MainWindow::OnPasteDone(bool ok) {
  if (!ctx_) return;
  const ClipItem* item = pendingPasteItem_;
  const bool moveToEnd = pasteMoveToEnd_;
  pendingPasteItem_ = nullptr;
  if (!item) { ctx_->ClearSelfPaste(); return; }
  // §7：写盘失败就不置灰——内容没送到目标，不能标记"已用"
  if (ok) ctx_->store().PasteDone(item, moveToEnd);   // C8 沉底 + 快速模式跳下一条
  ctx_->ArmPasteGuard();                                // 自身 WM_CLIPBOARDUPDATE 会即时清除，此为兜底
}

void MainWindow::ToggleVisibility() {
  if (!hwnd_) return;
  tip_.Hide();
  if (IsVisible()) {
    ShowWindow(hwnd_, SW_HIDE);                        // 收起后监听与托盘继续工作（FR-01/15）
    if (ctx_) ctx_->SyncRelayHook();                   // C15：看不见列表 = 接力不成立
  } else {
    ShowAndFocus();                                    // 内部已同步接力
  }
}

void MainWindow::ShowAndFocus() {
  if (!hwnd_) return;
  tip_.Hide();
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
  UpdateWindow(hwnd_);
  SetFocus(hwnd_);                                     // 焦点归列表（§6.7）
  focus_ = FocusOwner::List;
  if (ctx_) ctx_->SyncRelayHook();                     // C15：列表回到屏上，接力按模式重新裁决
}

bool MainWindow::IsVisible() const {
  return hwnd_ && IsWindowVisible(hwnd_) != FALSE;
}

void MainWindow::OnStoreChanged(const StoreEvent&) {
  if (!hwnd_) return;
  RebuildRows();
  CancelRowTip(hwnd_);        // 行几何已变，旧气泡的位置与内容都不可信
  // 悬停项可能已被移除/筛掉：指针不可再解引用，也顺带重新武装浮现计时
  const Row* hr = (list_ && hover_) ? list_->FindRow(hover_) : nullptr;
  if (hover_ && !hr) hover_ = nullptr;
  if (hr && hr->truncated) SetTimer(hwnd_, ID_HOVER_TIP, kHoverTipMs, nullptr);
  if (ctx_ && ctx_->store().pasteMode() == PasteMode::Quick) ScrollSelectionIntoView();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// 快速模式的选中位由 Store 钉在第一行，空格贴的就是它；高亮若在视口外就必须滚进来，
// 否则用户看不到"现在空格会贴哪条"。用户自己点选的行本来就可见，这里等于不动。
void MainWindow::ScrollSelectionIntoView() {
  if (!list_ || !ctx_) return;
  const Row* row = list_->FindRow(ctx_->store().Selected());
  if (!row) return;
  const float viewH = ClientH() - kStatusF - kListTop;
  if (row->card.top < scrollY_) scrollY_ = row->card.top;
  else if (row->card.bottom > scrollY_ + viewH) scrollY_ = row->card.bottom - viewH;
  ClampScroll(viewH);
}

}  // namespace sc
