#pragma once
#include "../core/Config.h"
#include <windows.h>

namespace sc {

// winuser.h 未定义字母键虚拟码，取固定值（技术方案 §5.2 注明的 VK_V = 0x56）
constexpr WORD kVkV = 0x56;

// 技术方案 §5.2 步骤 3 原文是「只送虚拟键码，不送 scancode」，**v2.3.2 起偏离该条**：
// 2026-10-05 实测 WPS 表格在「单元格仅选中」（焦点控件 EXCEL7＝网格）时吞掉 wScan=0 的注入
// Ctrl+V，而「单元格处于编辑态」（焦点控件 EXCEL6＝编辑框）时能贴进去（同一晚 23:25 与 23:38
// 两段日志的焦点控件差异即现场自证）。补上 MapVirtualKey 翻译出的扫描码，让注入事件在消息层面
// 与物理按键不可区分。用到的三个键（Ctrl/Alt/V）都不是扩展键，无需 KEYEVENTF_EXTENDEDKEY。
// 提权目标会静默丢弃（N5），返回值仅用于日志。
inline WORD ScanOf(WORD vk) { return WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)); }

inline UINT SendKey(WORD vk, bool up) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.wScan = ScanOf(vk);
  in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
  return SendInput(1, &in, sizeof(INPUT));
}

// 抬修饰键必须和 Ctrl+V 在**同一个 SendInput 批次**里：批次是原子投递的，物理按键插不进
// 中间。C15 接力的手势是「按住 Alt 点」，而 Ctrl+V 是 60ms 后由定时器注入的（§5.2 步骤 3），
// 分成两批就等于把 Alt 留给目标 —— Excel/WPS 里 Alt+Ctrl+V 是「选择性粘贴」，界面毫无反应。
//
// 而且 **Ctrl↓ 必须排在 Alt↑ 之前**（v2.3.2）：Excel/WPS 表格在「Alt 单独按下又单独抬起」时
// 进入 keytip（菜单快捷键提示）态；此时单元格**只是被选中**（非编辑态）走的是表格快捷键表，
// 紧随其后的 Ctrl+V 被当成菜单加速键吞掉 —— v2.3.1 的实测现象正是「先进编辑态才贴得进去，
// 只选中单元格就贴不进去」。Ctrl 已按下时抬 Alt 是和弦、不构成独立 Alt 点按，keytip 态不触发；
// 而 V↓ 落下时 Alt 已抬起，目标收到的是纯 Ctrl+V。
//
// `Alt↑` **无条件**排进批次，不按键态判断：到注入时再读 GetAsyncKeyState，读到的很可能是
// "我们自己抬起的结果"，而用户手指其实还压着（按住不放还有 typematic 重发把键态重新压下去）
// ——据此判断会漏发，批次退回 4 事件＝等于没修。重复的 keyup 幂等。
inline UINT SendCtrlV(bool releaseCtrl) {
  INPUT in[6]{};
  UINT count = 0;
  const auto push = [&](WORD vk, bool up) {
    in[count].type = INPUT_KEYBOARD;
    in[count].ki.wVk = vk;
    in[count].ki.wScan = ScanOf(vk);
    in[count].ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    ++count;
  };
  if (releaseCtrl) push(VK_CONTROL, true);
  push(VK_CONTROL, false);
  push(VK_MENU, true);
  push(kVkV, false);
  push(kVkV, true);
  push(VK_CONTROL, true);
  return SendInput(count, in, sizeof(INPUT));
}

// 用户正按着 Ctrl 时，先抬起来，否则目标收到的是裸 v（§5.2 表第三行）
inline bool IsCtrlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

// 注入那一刻的复查只能读异步态：WM_TIMER 不带键盘状态快照，GetKeyState 读到的是上一条
// 输入消息时刻的旧值。
inline bool IsCtrlDownAsync() { return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0; }

// C15 接力：Alt+左键触发时用户手指还压在 Alt 上，而 Excel 里 Alt+Ctrl+V 是「选择性粘贴」。
// 这里必须用 GetAsyncKeyState：接力那一下走的是钩子投递来的 WM_APP 消息，
// GetKeyState 读的是"本条消息自带的键盘状态"，在鼠标消息后填的是上一个按键的值，不可信。
inline bool IsAltDownAsync() { return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; }

}  // namespace sc
