#include "MainWindow.h"
#include "../app/AppContext.h"
#include "../native/SystemInfo.h"
#include "../util/Log.h"
#include <windowsx.h>
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

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  const LRESULT r = g_editPrev ? CallWindowProcW(g_editPrev, hwnd, msg, wParam, lParam)
                               : DefWindowProcW(hwnd, msg, wParam, lParam);
  switch (msg) {
    case WM_PAINT:
      if (GetWindowTextLengthW(hwnd) == 0) DrawEditPlaceholder(hwnd);
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

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = &MainWindow::Entry;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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
                             hit.zone == HitZone::RowStar;
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
    case WM_APP_PASTE_DONE:
      OnPasteDone(wParam != 0);
      return 0;
    case WM_APP_PICK_DONE:
      if (ctx_) ctx_->OnPickMessage(reinterpret_cast<HWND>(wParam));   // §5.5：钩子投来的根窗口
      return 0;
    case kMsgWtsSessionChange:
      if (wParam == kWtsSessionLock && ctx_) ctx_->OnSessionLock();    // 锁屏：光标绝不残留
      return 0;
    case WM_ENDSESSION:
      if (wParam && ctx_) ctx_->OnEndSession();                        // 注销/关机：同一取消链
      return 0;
    case kMsgHotkey:
      if (wParam == DWORD(kHotkeyId) && ctx_) ctx_->ToggleMainWindow();
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

void MainWindow::ReleaseTargetLevel() { rt_.reset(); }

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
}

void MainWindow::UnregisterHotkeys(HWND hwnd) {
  if (!hotkeyRegistered_) return;
  UnregisterHotKey(hwnd, kHotkeyId);
  hotkeyRegistered_ = false;
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
    // 仅模式文字本身可点：宽度之外的标题栏留给 WM_NCHITTEST 当拖拽区（zone 保持 None）
    if (out.zone == HitZone::None && x < kPadF + float(kModeTextW)) out.zone = HitZone::ModeText;
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

void MainWindow::DrawTitleBar(ID2D1RenderTarget* rt, float w) {
  rt->FillRectangle(D2D1::RectF(0.f, 0.f, w, kTitleF), theme_.TitleBg());
  rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

  const bool quick = ctx_ && ctx_->store().pasteMode() == PasteMode::Quick;
  const std::wstring mode = quick ? L"剪贴板 - 快速模式" : L"剪贴板 - 普通模式";
  rt->DrawText(mode.c_str(), static_cast<UINT32>(mode.size()), theme_.Title(),
               D2D1::RectF(kPadF, 0.f, w - 3.f * kBtnF - 4.f * kTitleBtnGapF, kTitleF),
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
      rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy - r * .18f), r * .34f, r * .34f), ink,
                      1.2f);
      Stroke(rt, ink, cx, cy + r * .16f, cx, cy + r * .55f);
      Stroke(rt, ink, cx - r * .38f, cy + r * .55f, cx + r * .38f, cy + r * .55f);
      if (topmost_) {
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
               D2D1::RectF(kPadF, bar.top, w - 80.f, h), theme_.Muted(),
               D2D1_DRAW_TEXT_OPTIONS_NONE);
  rt->DrawText(kVersionText, static_cast<UINT32>(wcslen(kVersionText)), theme_.Meta(),
               D2D1::RectF(w - kPadF - 68.f, bar.top, w - kPadF, h), theme_.Muted(),
               D2D1_DRAW_TEXT_OPTIONS_NONE);
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
    if (!store.Selected() && !store.Display().empty()) store.Select(store.Display().front());
  } else {
    store.Select(nullptr);   // 普通模式无强制选中
  }
  ctx_->SavePasteMode(next);   // §8.3：模式变更即落盘
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// FR-09/10/11：粘贴链路入口。全异步——Start 立即返回，结果经 WM_APP_PASTE_DONE 回来。
void MainWindow::DoPaste(const ClipItem* item, bool moveToEnd) {
  if (!ctx_ || !hwnd_ || !item) return;
  if (paste_.busy()) return;                       // 上一次未完成：静默丢弃，不叠阶段机
  const std::wstring content = item->content;      // 原样粘贴：不折叠、不 trim、不补换行
  ctx_->BeginSelfPaste(content);                   // C4 双层防护：先置标志再写盘
  pendingPasteItem_ = item;
  pasteMoveToEnd_ = moveToEnd;
  paste_.onDone = [this](bool ok) {
    if (hwnd_) PostMessageW(hwnd_, WM_APP_PASTE_DONE, ok ? WPARAM(1) : WPARAM(0), 0);
  };
  paste_.Start(hwnd_, ctx_->PasteTarget(), content);
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
  } else {
    ShowAndFocus();
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
  InvalidateRect(hwnd_, nullptr, FALSE);
}

}  // namespace sc
