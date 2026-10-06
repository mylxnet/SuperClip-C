#include "Clipboard.h"
#include "WinUtil.h"
#include <cstring>

namespace sc {
namespace {

// 假定调用方已持有 ClipbrdLock（打开期间剪贴板被本进程独占）
ClipRead ReadLocked(std::wstring& out) {
  if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    HANDLE raw = GetClipboardData(CF_UNICODETEXT);
    if (!raw) return ClipRead::Busy;
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(raw));
    if (!text) return ClipRead::Busy;              // 源程序仍占用（Excel 复制瞬间）→ 交给重试
    out.assign(text);
    GlobalUnlock(raw);
    return out.empty() ? ClipRead::Empty : ClipRead::Ok;
  }
  if (IsClipboardFormatAvailable(CF_TEXT)) {
    // 少数老程序只给 ANSI
    HANDLE raw = GetClipboardData(CF_TEXT);
    if (!raw) return ClipRead::Busy;
    const char* text = static_cast<const char*>(GlobalLock(raw));
    if (!text) return ClipRead::Busy;
    // 显式长度：所需宽字符数不含结尾 NUL，缓冲区与需求量精确相等（-1 版本含 NUL，
    // 再按 wchars-1 传缓冲区会整段失败并留下全 \0 的假成功结果）
    size_t len = 0;
    const SIZE_T cap = GlobalSize(raw);
    while (len < cap && text[len] != '\0') ++len;    // 不信生产方一定补了 NUL
    if (len == 0) { GlobalUnlock(raw); return ClipRead::Empty; }
    const int wchars = MultiByteToWideChar(CP_ACP, 0, text, int(len), nullptr, 0);
    if (wchars <= 0) { GlobalUnlock(raw); return ClipRead::Empty; }
    out.resize(size_t(wchars));
    MultiByteToWideChar(CP_ACP, 0, text, int(len), out.data(), wchars);
    GlobalUnlock(raw);
    return out.empty() ? ClipRead::Empty : ClipRead::Ok;
  }
  return ClipRead::NotText;                        // 图片/文件等：契约 §1.3 非目标，直接忽略且不重试
}

}  // namespace

ClipRead ReadClipboardText(HWND owner, std::wstring& out) {
  out.clear();
  ClipbrdLock lock(owner);
  if (!lock.ok()) return ClipRead::Busy;
  return ReadLocked(out);
}

bool WriteClipboardText(HWND owner, const std::wstring& text) {
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  ClipbrdLock lock(owner);
  if (!lock.ok()) return false;

  GlobalMem mem;
  if (!mem.Alloc(bytes)) return false;
  void* dst = mem.lock();
  if (!dst) return false;
  memcpy(dst, text.c_str(), bytes);
  mem.unlock();

  if (!EmptyClipboard()) return false;
  // 移交成功后所有权归系统，不得再 GlobalFree（§5.2）
  if (SetClipboardData(CF_UNICODETEXT, mem.get()) == nullptr) return false;
  mem.release();
  return true;
}

}  // namespace sc
