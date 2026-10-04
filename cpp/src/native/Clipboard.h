#pragma once
#include "../core/Config.h"
#include <string>

namespace sc {

// 技术方案 §3.4：区分"文本读到了"与"根本不是文本"——非文本不重试，省掉 6×25ms。
enum class ClipRead { Ok, NotText, Empty, Busy };

ClipRead ReadClipboardText(HWND owner, std::wstring& out);

// M4 用：写入并移交所有权。失败时不触碰系统剪贴板内容。
bool WriteClipboardText(HWND owner, const std::wstring& text);

}  // namespace sc
