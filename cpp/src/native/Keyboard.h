#pragma once
#include "../core/Config.h"
#include <windows.h>

namespace sc {

// winuser.h 未定义字母键虚拟码，取固定值（技术方案 §5.2 注明的 VK_V = 0x56）
constexpr WORD kVkV = 0x56;

// 技术方案 §5.2 步骤 3：只送虚拟键码，不送 scancode（SendInput 自行翻译，与原
// keybd_event 行为等价）。提权目标会静默丢弃（N5），返回值仅用于日志。
inline UINT SendKey(WORD vk, bool up) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
  return SendInput(1, &in, sizeof(INPUT));
}

inline UINT SendCtrlV() {
  INPUT in[4]{};
  for (int i = 0; i < 4; ++i) in[i].type = INPUT_KEYBOARD;
  in[0].ki.wVk = VK_CONTROL;
  in[1].ki.wVk = kVkV;
  in[2].ki.wVk = kVkV;
  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
  in[3].ki.wVk = VK_CONTROL;
  in[3].ki.dwFlags = KEYEVENTF_KEYUP;
  return SendInput(4, in, sizeof(INPUT));
}

// 用户正按着 Ctrl 时，先抬起来，否则目标收到的是裸 v（§5.2 表第三行）
inline bool IsCtrlDown() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

}  // namespace sc
