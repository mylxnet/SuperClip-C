#pragma once
#include "../core/Config.h"
#include <windows.h>
#include <functional>
#include <utility>

// 技术方案 §4.1：所有句柄走 RAII，杜绝 N3（长驻泄漏）。
// 规则：不在 RAII 中的 HWND 必须出现在 AppContext::Shutdown 的固定顺序表里。
namespace sc {

// 任何路径都要执行的收尾（光标复位、临时文件删除等）
class ScopeExit {
 public:
  explicit ScopeExit(std::function<void()> fn) : fn_(std::move(fn)) {}
  ~ScopeExit() { if (fn_) fn_(); }
  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;
  void dismiss() { fn_ = nullptr; }
 private:
  std::function<void()> fn_;
};

class ClipbrdLock {
 public:
  explicit ClipbrdLock(HWND owner) { ok_ = OpenClipboard(owner) != FALSE; }
  ~ClipbrdLock() { if (ok_) CloseClipboard(); }
  ClipbrdLock(const ClipbrdLock&) = delete;
  ClipbrdLock& operator=(const ClipbrdLock&) = delete;
  bool ok() const { return ok_; }
 private:
  bool ok_ = false;
};

// GMEM_MOVEABLE 块：移交成功（SetClipboardData）后 release()，否则析构 GlobalFree
class GlobalMem {
 public:
  GlobalMem() = default;
  bool Alloc(size_t bytes) {
    Free();
    handle_ = GlobalAlloc(GMEM_MOVEABLE, bytes);
    return handle_ != nullptr;
  }
  ~GlobalMem() { Free(); }
  GlobalMem(const GlobalMem&) = delete;
  GlobalMem& operator=(const GlobalMem&) = delete;
  HGLOBAL get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }
  void release() { handle_ = nullptr; }
  void* lock() const { return handle_ ? GlobalLock(handle_) : nullptr; }
  void unlock() const { if (handle_) GlobalUnlock(handle_); }
 private:
  void Free() { if (handle_) { GlobalFree(handle_); handle_ = nullptr; } }
  HGLOBAL handle_ = nullptr;
};

class MenuGuard {
 public:
  MenuGuard() { menu_ = CreatePopupMenu(); }
  ~MenuGuard() { if (menu_) DestroyMenu(menu_); }
  MenuGuard(const MenuGuard&) = delete;
  MenuGuard& operator=(const MenuGuard&) = delete;
  HMENU get() const { return menu_; }
  bool ok() const { return menu_ != nullptr; }
 private:
  HMENU menu_ = nullptr;
};

class ProcessHandle {
 public:
  explicit ProcessHandle(DWORD pid) {
    handle_ = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  }
  ~ProcessHandle() { if (handle_) CloseHandle(handle_); }
  ProcessHandle(const ProcessHandle&) = delete;
  ProcessHandle& operator=(const ProcessHandle&) = delete;
  HANDLE get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }
 private:
  HANDLE handle_ = nullptr;
};

}  // namespace sc
