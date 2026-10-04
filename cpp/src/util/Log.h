#pragma once
#include "../core/Config.h"
#include <string>

// 依据技术方案 §4.5：追加写、超 kLogMaxBytes 保留尾部 512KB 重写。
// 崩溃兜底路径只用 LogFatalRaw()——固定栈缓冲，不申请堆。
namespace sc {

// dir 为空时日志不落盘（单测/早期阶段）。由 AppContext 在启动时配置一次。
void LogInit(const std::wstring& dir);
void LogClose();

void LogLine(char level, const wchar_t* module, const std::wstring& msg, DWORD gle);

inline void LogInfo(const wchar_t* module, const std::wstring& msg) { LogLine('I', module, msg, 0); }
inline void LogWarn(const wchar_t* module, const std::wstring& msg) { LogLine('W', module, msg, GetLastError()); }
inline void LogError(const wchar_t* module, const std::wstring& msg) { LogLine('E', module, msg, GetLastError()); }

// 崩溃兜底专用：栈缓冲 + 单次 WriteFile。调用方不得再抛。
void LogFatalRaw(const wchar_t* module, const wchar_t* msg);

}  // namespace sc
