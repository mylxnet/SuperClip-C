#pragma once
#include <windows.h>      // 小写：Linux/WSL 文件系统区分大小写，mingw 交叉编译才不报错
#include <cstddef>

// SuperClip C++ · 全局常量唯一来源（禁止在别处写魔数）
// 依据：doc/PROJECT.md §1.1
namespace sc {

constexpr int    kMaxItems            = 500;      // FR-04 非收藏上限
constexpr UINT   kDebounceMs          = 300;      // FR-06 搜索防抖
constexpr UINT   kReadRetryMs         = 25;       // 剪贴板读取重试间隔
constexpr int    kReadRetryMax        = 6;        // 最多 6 次
constexpr int    kWriteRetryMax       = 3;        // §7：写盘被占用 3×25ms，失败则条目不变灰
constexpr UINT   kFocusWaitMs         = 60;       // 前台切换后等待焦点稳定（M4）
constexpr UINT   kPasteGuardMs        = 1000;     // C4 粘贴防护兜底
constexpr UINT   kPickTimeoutMs       = 8000;     // T2 点选卡死自动取消（M4）
constexpr size_t kMaxContentChars     = 262144;   // T3 单条截断
constexpr size_t kPreviewMeasureChars = 2000;     // 预览排版仅测前 2000 字符
constexpr size_t kMaxTipChars         = 2000;     // 悬浮气泡最多显示 2000 字，超出补 …
constexpr DWORD  kLogMaxBytes         = 1u << 20; // error.log 上限

// 窗口逻辑尺寸（96dpi 基准，渲染期缩放）
constexpr int kWinW = 380, kWinH = 600, kMinW = 320, kMinH = 420;
constexpr int kTitleH = 36, kToolbarH = 76, kStatusH = 22;
constexpr int kBtn = 24, kStarW = 22, kTimeW = 58, kIndexW = 26;
// kContentMaxH = 主内容单行高度上限（13px 中文一行 ≈ 16.5 DIP，留 18 余量）；
// 超出即截断加 …，完整内容由悬浮气泡给出（设计方案 §7）。
// 行高不含来源标注：该标注已不上屏（2026-10-04 用户决议），仅参与搜索。
constexpr int kRowPad = 8, kContentMaxH = 18;

// 跨绘制后端共用的颜色：卡片/按钮描边与气泡边框走 GDI+D2D 两条路径，色值只能有一份
constexpr UINT kBorderGrayHex = 0xC9CFD8;   // 灰色边框（用户 2026-10-04 指定，替代近黑 #272C36）
constexpr UINT kInkGrayHex = 0x272C36;      // 正文墨色，气泡文字复用 Theme 的契约色

// UI 度量（DIP）。D2D 目标按窗口 DPI 调 SetDpi，绘制期一律用这些逻辑值，
// 只有真子窗（EDIT）与窗口外框需要乘 dpi/96 换成物理像素。
constexpr int kPad = 12;                 // 窗口左右内边距
constexpr int kSearchH = 26, kToolBtnH = 28, kToolPady = 8, kToolGap = 8;
constexpr int kRowGap = 6;               // 卡片间距
constexpr int kCardRadius = 6;
constexpr int kTitleBtnGap = 6;          // 标题栏三按钮间距
constexpr int kTitleIconDip = 18;        // 标题栏左侧应用图标边长
constexpr int kTitleIconGap = 6;         // 图标与模式文字间距
constexpr int kModeTextW = 136;          // 标题栏模式文字可点击宽度（DIP）；区域外仍用于拖拽
constexpr int kSearchClearDip = 16;      // 搜索框右侧清除叉号的可点边长
constexpr int kSearchClearInset = 6;     // 叉号距搜索框右内缘
constexpr int kTipOffsetChars = 3;       // 悬浮气泡相对条目左缘右移的字符数
constexpr float kFontTitle = 14.f;       // 标题栏模式文字
constexpr float kFontBody = 13.f;        // 条目主内容
constexpr float kFontMeta = 11.5f;       // 时间戳 / 序号 / 状态栏
constexpr float kFontButton = 12.5f;     // 工具栏按钮
constexpr float kFontStar = 15.f;        // ★/☆ 字形
constexpr int kMaxLayoutCache = 512;     // TextLayout 缓存上限，超限整体清空（正常不触发）
constexpr int kBtnFilterW = 60, kBtnClearW = 56, kBtnResetW = 56, kBtnPickW = 72;
constexpr int kTipMaxW = 320, kTipMaxH = 240;   // 悬浮气泡尺寸上限（DIP）
constexpr UINT kHoverTipMs = 400;               // 鼠标停多久后浮现全文

// 帮助窗（技术方案 §6.8，步骤 11）：无边框模态，9 步静态引导，上一步/下一步/关闭三按钮
constexpr int kHelpW = 420, kHelpH = 300;
constexpr int kHelpTitleH = 40;                 // 顶部标题带（同时是拖拽区）
constexpr int kHelpBtnH = 30, kHelpNavBtnW = 76, kHelpCloseBtnW = 56, kHelpBtnGap = 8;

// 与 src/res/app.rc 的 FILEVERSION 同步（PackageRelease.bat 以 app.rc 为准，并校验本行）
inline constexpr wchar_t kVersionText[]  = L"v2.1.2";
// 署名固定（agent.md 四.3：界面上版本号写在署名之前）
inline constexpr wchar_t kAppSignature[] = L"by Mr lin";
// 点击署名交给**系统默认浏览器**打开的仓库地址。程序自身仍零网络代码：不链 wininet/winhttp、
// 不调 socket，只是 ShellExecuteW(L"open") 一次外部唤起（AC-8 边界见 doc/DESIGN.md ADR）。
inline constexpr wchar_t kProjectUrl[]   = L"https://github.com/mylxnet/SuperClip-C";
// 状态栏右侧「vX.Y.Z  by Mr lin」预留宽度（DIP），左栏文字到此为止，避免重叠
constexpr float kStatusRightW = 116.f;

// DIP → 物理像素（96dpi 基准）。绘制期一律用 DIP，只有真子窗（搜索框）与帮助窗边框要换算。
// 必须是**运行期**函数：MulDiv 不是 constexpr，放 constexpr 里整个 TU 编译不过。
// 未进 Config 的旧代码仍各自用 MulDiv 或本文件外的 float ScaleF()，两种写法都四舍五入，等价。
inline int ScaleInt(int base, UINT dpi) { return MulDiv(base, int(dpi), 96); }

// 标识与窗口类名
inline constexpr wchar_t kMainClass[]    = L"SuperClipMain";
inline constexpr wchar_t kMonitorClass[] = L"SuperClipMonitor";
inline constexpr wchar_t kTrayClass[]    = L"SuperClipTray";
inline constexpr wchar_t kHelpClass[]    = L"SuperClipHelp";
inline constexpr wchar_t kWindowTitle[]  = L"SuperClip";
inline constexpr wchar_t kHelpTitle[]    = L"使用帮助";
inline constexpr wchar_t kTrayTip[]      = L"SuperClip 超级剪贴板";
inline constexpr wchar_t kSearchHint[]   = L"搜索…";
// T1：Global\ 需 SeCreateGlobalPrivilege，标准用户创建会失败 → 会话级单实例
inline constexpr wchar_t kMutexName[]    = L"Local\\SuperClip_SingleInstance_9F3A2B1C";
inline constexpr wchar_t kDataDirName[]  = L"SuperClip";
inline constexpr wchar_t kHistoryFile[]  = L"history.json";
inline constexpr wchar_t kSettingsFile[] = L"settings.json";
inline constexpr wchar_t kLogFile[]      = L"error.log";

// 自投递消息（WM_APP 基址，避免与系统消息冲突）
constexpr UINT WM_APP_TRAY       = WM_APP + 1;   // 0x8001 托盘回调（技术方案 §5.4）
constexpr UINT WM_APP_CLIP_READY = WM_APP + 2;   // 读取成功 → 投递文本指针
constexpr UINT WM_APP_PASTE_DONE = WM_APP + 3;   // M4
constexpr UINT WM_APP_PICK_DONE  = WM_APP + 4;   // M4
constexpr UINT WM_APP_SEARCH_ENTER = WM_APP + 5;  // 搜索框回车 → 焦点交回列表（§6.7）
constexpr UINT WM_APP_RAISE_TOPMOST = WM_APP + 6; // 0x8006 激活后补发置顶（C13，见 MainWindow WM_ACTIVATE）

// 定时器 ID（WM_TIMER.wParam 分派，全进程唯一）
constexpr UINT_PTR ID_SEARCH      = 1;
constexpr UINT_PTR ID_READ        = 2;
constexpr UINT_PTR ID_FOCUS_WAIT  = 3;
constexpr UINT_PTR ID_PASTE_GUARD = 4;
constexpr UINT_PTR ID_PICK        = 5;
constexpr UINT_PTR ID_HOVER_TIP   = 6;       // 悬停延迟后浮现全文气泡
constexpr UINT_PTR ID_STATUS_HINT = 7;       // 收藏视图切换提示的自动消隐
constexpr UINT     kStatusHintMs  = 3000;

constexpr int kHotkeyId = 1;                     // FR-16

// SetSystemCursor 的标准光标索引：SDK 写作 OCR_NORMAL = MAKEINTRESOURCE(32512)，而该函数第二
// 参是 DWORD，故直接取序号 32512（= IDC_ARROW）。mingw-w64 的 winuser.h 未导出 OCR_* 宏。
constexpr DWORD kOcrNormal = 32512;

// 资源与菜单标识
constexpr WORD kIconIdApp   = 101;               // app.rc 里的主图标
constexpr UINT kTrayOpen    = 1;                 // 托盘右键「打开」
constexpr UINT kTrayExit    = 2;                 // 托盘右键「退出」

// 消息常量（部分头文件在旧 SDK 未定义，固定值兜底）
constexpr UINT kMsgClipboardUpdate = 0x031D;     // WM_CLIPBOARDUPDATE
constexpr UINT kMsgHotkey          = 0x0312;     // WM_HOTKEY
constexpr UINT kMsgWtsSessionChange = 0x02B1;    // WM_WTSSESSION_CHANGE（需 WTSRegisterSessionNotification）
constexpr WPARAM kWtsSessionLock   = 1;          // WTS_SESSION_LOCK：点选期间锁屏必须复位光标
constexpr UINT   kVkOem3           = 0xC0;       // ` ~ 键

}  // namespace sc
