#pragma once
#include "../core/Config.h"
#include <windows.h>
#include <functional>
#include <string>

namespace sc {

// 技术方案 §5.2：写剪贴板 → 夺回目标前台 → 模拟 Ctrl+V 的阶段机。
// 定时器 ID_FOCUS_WAIT 挂在 owner（主窗）上，由主窗 WM_TIMER 转发 OnTimerId；
// 全程不阻塞消息循环（除写盘重试的 3×25ms，上限低于一帧的两倍）。
class PasteService {
 public:
  enum class Stage { Idle, Write, WaitForeground };

  // 立即返回；结束时调用 onDone(ok)。ok==false 时调用方不得把条目置灰。
  void Start(HWND owner, HWND target, std::wstring text);
  bool busy() const { return stage_ != Stage::Idle; }
  void OnTimerId(UINT_PTR id);
  void Cancel();  // 退出清理链：停在 Idle，绝不再按键

  std::function<void(bool ok)> onDone;

 private:
  bool WriteClipboard(const std::wstring& text);
  bool BringTargetToFront(HWND target);
  UINT InjectCtrlV();   // 同批注入 Ctrl↓→Alt↑→V↓→V↑→Ctrl↑，返回 SendInput 事件数
  void Finish(bool ok);

  Stage stage_ = Stage::Idle;
  HWND owner_ = nullptr;
  HWND target_ = nullptr;
  std::wstring injected_;   // 本次批次顺序，供日志自证 Alt↑ 排在 Ctrl↓ 之后
};

}  // namespace sc
