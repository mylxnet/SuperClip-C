// SuperClip 步骤 8 的隔离粘贴宿主（不参与发布构建，只用于实机走查）
//
// 为什么要自建：AC-2/6 的"粘到目标光标处"必须有可机读的证据，而往真实应用粘贴会
// 碰到用户的实际数据。本宿主是一个多行 EDIT：预置 "HEAD\r\nTAIL" 并把插入点放在
// HEAD 之后，粘贴成功即得到 "HEAD<token>\r\nTAIL" —— 位置与内容都能断言。
//
// 编译（WSL 交叉编译，与主程序同一工具链）：
//   cd /mnt/e/qcode/superclip/cpp && x86_64-w64-mingw32-g++ -std=c++20 -O1 -municode
//     -DUNICODE -D_UNICODE -DNOMINMAX -static -Wl,--subsystem,windows
//     -o build-mingw/PasteTarget.exe tools/PasteTarget.cpp -lshell32 -luser32 -lgdi32 -lkernel32
// --subsystem,windows 是必需的：控制台子系统会让 OS 额外建一个 ConsoleWindowClass 顶层窗，
// 步骤 10 的"按进程名找回绑定窗口"于是数到 2 个候选而拒绝绑定（2026-10-04 实机踩到）。
// 用法：PasteTarget.exe <dump路径> [min]        min = 启动即最小化（验 T4 SW_RESTORE）
// 交互：SendMessageW(hwnd, WM_APP+7, 0, 0) → 把当前 EDIT 文本以 UTF-8 写入 dump 路径，
//       返回字符数；关窗时同样落一次盘。
//       SendMessageW(hwnd, WM_APP+9, 0, 0) → 返回本进程收到的鼠标按键消息数（步骤 9 用来
//       证明 T2：点选的那一次点击被钩子吞掉，目标窗口既没选中单元格也没被激活）。
#include <windows.h>
#include <string>

namespace {

constexpr UINT kMsgDump = WM_APP + 7;
constexpr UINT kMsgFocusEdit = WM_APP + 8;
constexpr UINT kMsgClicks = WM_APP + 9;
constexpr wchar_t kClass[] = L"SuperClipPasteTarget";
constexpr wchar_t kTitle[] = L"SuperClipPasteTarget";
constexpr int kEditId = 100;

HWND g_edit = nullptr;
std::wstring g_dumpPath;
int g_clicks = 0;

void Dump() {
  if (g_dumpPath.empty()) return;
  const int len = GetWindowTextLengthW(g_edit);
  std::wstring text(size_t(len) + 1, L'\0');
  const int got = GetWindowTextW(g_edit, text.data(), int(text.size()));
  if (got < 0) return;
  text.resize(size_t(got));
  std::string utf8;
  utf8.resize(text.size() * 4);
  const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
                                    utf8.data(), int(utf8.size()), nullptr, nullptr);
  utf8.resize(n < 0 ? 0 : size_t(n));
  HANDLE h = CreateFileW(g_dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(h, utf8.data(), DWORD(utf8.size()), &written, nullptr);
  CloseHandle(h);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      const HMODULE inst = GetModuleHandleW(nullptr);
      g_edit = CreateWindowExW(0, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN |
                                   ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL,
                               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(INT_PTR(kEditId)),
                               inst, nullptr);
      HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
      if (font) SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
      // 基线文本：插入点固定在 HEAD 之后，粘贴成功即得 "HEAD<token>\r\nTAIL"，
      // 位置与内容都能断言（空文本时 EM_SETSEL 会被夹到 0，所以必须先设文本）
      SetWindowTextW(g_edit, L"HEAD\r\nTAIL");
      SendMessageW(g_edit, EM_SETSEL, 4, 4);
      SetFocus(g_edit);   // 真实编辑器的输入控件在窗口生命周期内始终持有焦点，
                          // 线程会记住它，跨进程激活回来时自动恢复
      return 0;
    }
    case WM_ACTIVATE:
      // 真实编辑器（记事本/Excel）在窗口被激活时会把键盘焦点交回输入控件。
      // 注意不能在 WM_ACTIVATE 处理里直接 SetFocus：系统随后会把焦点安在顶层窗口上，
      // 所以延后一条消息再设。裸窗口不做这件事，跨进程激活回来时 Ctrl+V 就无处可去。
      if (LOWORD(wp) != WA_INACTIVE && g_edit) PostMessageW(hwnd, kMsgFocusEdit, 0, 0);
      return DefWindowProcW(hwnd, msg, wp, lp);
    case kMsgFocusEdit:
      if (g_edit) SetFocus(g_edit);
      return 0;
    case kMsgDump:
      Dump();
      return LRESULT(GetWindowTextLengthW(g_edit));
    case kMsgClicks:
      return g_clicks;
    case WM_SIZE:
      MoveWindow(g_edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
      return 0;
    case WM_CLOSE:
      Dump();
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      Dump();
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  // 命令行形如：PasteTarget.exe <dump路径> [min]
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const bool startMin = argc > 2 && std::wstring(argv[2]) == L"min";
  g_dumpPath = argc > 1 ? argv[1] : L"";
  if (argv) LocalFree(argv);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kClass;
  if (!RegisterClassExW(&wc)) return 1;
  const HWND hwnd = CreateWindowExW(0, kClass, kTitle, WS_OVERLAPPEDWINDOW, 200, 200, 520, 260,
                                    nullptr, nullptr, inst, nullptr);
  if (!hwnd) return 1;
  ShowWindow(hwnd, startMin ? SW_SHOWMINIMIZED : SW_SHOW);
  UpdateWindow(hwnd);

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) {
    // 子窗的点击同样先过本线程队列，所以这里统计全进程的鼠标按键；
    // 步骤 9 断言"点选那一次不增加计数"，即 T2：点击被低层钩子吞掉
    if (m.message == WM_LBUTTONDOWN || m.message == WM_LBUTTONUP || m.message == WM_NCLBUTTONDOWN) {
      ++g_clicks;
    }
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return int(m.wParam);
}
