#include "TrayService.h"
#include "../util/Log.h"

namespace sc {

bool TrayService::Create(HINSTANCE inst) {
  inst_ = inst;
  if (!win_.Create(inst, kTrayClass, kTrayClass,
                   [this](HWND hwnd, UINT msg, WPARAM w, LPARAM l) { return Handle(hwnd, msg, w, l); })) {
    LogError(L"tray", L"托盘窗口创建失败");
    return false;
  }

  nid_ = {};
  nid_.cbSize = sizeof(nid_);
  nid_.hWnd = win_.get();
  nid_.uID = 1;
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid_.uCallbackMessage = WM_APP_TRAY;
  nid_.hIcon = static_cast<HICON>(
      LoadImageW(inst, MAKEINTRESOURCEW(kIconIdApp), IMAGE_ICON,
                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                 LR_DEFAULTCOLOR | LR_SHARED));
  if (!nid_.hIcon) {
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);          // §5.4 回退链：图标资源缺失时用系统默认
    LogWarn(L"tray", L"应用图标缺失，使用系统默认图标");
  }
  wcscpy_s(nid_.szTip, kTrayTip);

  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  return Add();
}

bool TrayService::Add() {
  if (!win_.ok()) return false;
  if (added_) Shell_NotifyIconW(NIM_DELETE, &nid_);            // 重挂前先摘，避免重复条目
  const BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid_);
  added_ = ok != FALSE;
  if (!added_) LogWarn(L"tray", L"NIM_ADD 失败，托盘不可用（窗口与热键不受影响）");
  return added_;
}

void TrayService::Destroy() {
  if (added_) {
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
  }
  win_.Destroy();
}

void TrayService::ShowBalloon(const std::wstring& title, const std::wstring& text) {
  if (!added_) {
    LogWarn(L"tray", L"托盘未挂上，接力提示无法气泡显示");
    return;
  }
  NOTIFYICONDATAW info = nid_;
  info.uFlags = NIF_INFO;
  wcsncpy_s(info.szInfoTitle, title.c_str(), _TRUNCATE);
  wcsncpy_s(info.szInfo, text.c_str(), _TRUNCATE);
  info.dwInfoFlags = NIIF_INFO;
  if (!Shell_NotifyIconW(NIM_MODIFY, &info)) LogWarn(L"tray", L"气泡显示失败（功能不受影响）");
}

void TrayService::HandleTrayClick(HWND hwnd, LPARAM lParam) {
  const UINT event = LOWORD(lParam);
  if (event == WM_LBUTTONDBLCLK) {
    if (onOpen) onOpen();
    return;
  }
  if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) ShowContextMenu(hwnd);
}

void TrayService::ShowContextMenu(HWND hwnd) {
  MenuGuard menu;
  if (!menu.ok()) return;
  AppendMenuW(menu.get(), MF_STRING, kTrayOpen, L"打开 SuperClip");
  AppendMenuW(menu.get(), MF_STRING, kTrayExit, L"退出");

  POINT pt{};
  if (!GetCursorPos(&pt)) pt = POINT{0, 0};

  // 弹菜单前先把自身置前台，否则点击菜单外区域不消失（标准托盘范式）
  SetForegroundWindow(hwnd);
  const UINT cmd = TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                    pt.x, pt.y, hwnd, nullptr);
  if (cmd == kTrayOpen) { if (onOpen) onOpen(); }
  else if (cmd == kTrayExit) { if (onExit) onExit(); }
}

LRESULT TrayService::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (taskbarCreated_ && msg == taskbarCreated_) {
    added_ = false;                            // Explorer 重启：图标已被系统丢弃，重挂
    LogInfo(L"tray", L"收到 TaskbarCreated，重挂托盘图标");
    Add();
    return 0;
  }
  switch (msg) {
    case WM_APP_TRAY:
      HandleTrayClick(hwnd, lParam);
      return 0;
    case WM_DESTROY:
      if (added_) { Shell_NotifyIconW(NIM_DELETE, &nid_); added_ = false; }
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace sc
