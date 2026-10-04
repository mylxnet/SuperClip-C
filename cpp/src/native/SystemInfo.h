#pragma once
#include "../core/Config.h"

// 技术方案 §4.4：Win7 上直接引用 Win10 符号会在**加载期**失败（exe 双击无反应）。
// 下列能力全部 GetProcAddress 探测 + 回退链；_WIN32_WINNT 固定 0x0601，
// 新符号在本文件内自行声明，禁止靠提升宏"省事"。
namespace sc {

// 进程启动最早处调用一次（建任何窗口之前），仅用于日志
enum class DpiAwareness { PerMonitorV2, System, Unaware };
DpiAwareness InitDpiAwareness();

// 回退链：user32!GetDpiForWindow（1607+）→ shcore!GetDpiForMonitor（8.1+）→ GetDeviceCaps
UINT DpiForWindow(HWND hwnd);

// Win11 22H2+ 圆角；旧系统静默无操作
void ApplyRoundedCorners(HWND hwnd);

// §12 兼容性矩阵：高对比度主题下 UI 走系统色
bool IsHighContrast();

// 步骤 10 位置恢复：把落盘过的窗口矩形（物理像素）裁决到当前桌面上。
// 返回 false = 该矩形已完全不在任何显示器上（拔掉副屏等），调用方回默认停靠；
// 返回 true 时 rc 尺寸不变、位置已平移进那台显示器的工作区（不会漂到看不见的地方）。
bool FitRectToDesktop(RECT& rc);

}  // namespace sc
