#include "Log.h"
#include "../core/Text.h"
#include <cstdio>
#include <cstring>

namespace sc {
namespace {

constexpr size_t kTailKeepBytes = 512u * 1024u;

std::wstring g_path;
CRITICAL_SECTION g_cs;
bool g_csReady = false;

struct CsGuard {
  CsGuard() { InitializeCriticalSection(&g_cs); g_csReady = true; }
  ~CsGuard() { g_csReady = false; DeleteCriticalSection(&g_cs); }
};
CsGuard g_csGuard;

struct Lock {
  Lock() { if (g_csReady) EnterCriticalSection(&g_cs); }
  ~Lock() { if (g_csReady) LeaveCriticalSection(&g_cs); }
};

void StampLocal(wchar_t (&out)[32]) {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  swprintf(out, 32, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

std::wstring FormatLine(char level, const wchar_t* module, const std::wstring& msg, DWORD gle) {
  wchar_t stamp[32];
  StampLocal(stamp);
  std::wstring line = L"[";
  line += stamp;
  line += L"] [";
  line += wchar_t(level);
  line += L"] [";
  line += module ? module : L"-";
  line += L"] ";
  line += msg;
  if (gle) {
    wchar_t tail[32];
    swprintf(tail, 32, L" (gle=%lu)", gle);
    line += tail;
  }
  line += L"\r\n";
  return line;
}

bool FileSizeOf(const std::wstring& path, ULONGLONG& out) {
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return false;
  out = (ULONGLONG)info.nFileSizeHigh << 32 | info.nFileSizeLow;
  return true;
}

// 尾部保留重写：读最后 512KB → 截断回写。任何一步失败就放弃轮转——
// 宁可日志超长，也不能把日志文件写坏。
void RotateIfNeeded() {
  ULONGLONG size = 0;
  if (!FileSizeOf(g_path, size) || size <= kLogMaxBytes) return;

  HANDLE rf = CreateFileW(g_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (rf == INVALID_HANDLE_VALUE) return;

  std::string tail(kTailKeepBytes, '\0');
  LARGE_INTEGER off;
  off.QuadPart = -(LONGLONG)kTailKeepBytes;
  BOOL ok = SetFilePointerEx(rf, off, nullptr, FILE_END);
  DWORD got = 0;
  if (ok) ok = ReadFile(rf, tail.data(), (DWORD)tail.size(), &got, nullptr);
  CloseHandle(rf);
  if (!ok || got == 0) return;
  tail.resize(got);

  size_t nl = tail.find('\n');                       // 丢掉半截首行
  if (nl != std::string::npos && nl + 1 < tail.size()) tail.erase(0, nl + 1);

  HANDLE wf = CreateFileW(g_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (wf == INVALID_HANDLE_VALUE) return;
  const char* header = "\xEF\xBB\xBF[log rotated, older content discarded]\r\n";
  DWORD written = 0;
  WriteFile(wf, header, (DWORD)strlen(header), &written, nullptr);
  WriteFile(wf, tail.data(), (DWORD)tail.size(), &written, nullptr);
  CloseHandle(wf);
}

void AppendUtf8(const std::string& bytes, bool needsBom) {
  HANDLE f = CreateFileW(g_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  SetFilePointer(f, 0, nullptr, FILE_END);
  DWORD written = 0;
  if (needsBom) WriteFile(f, "\xEF\xBB\xBF", 3, &written, nullptr);   // 记事本按 UTF-8 识别
  WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
  CloseHandle(f);
}

bool IsNewFile() {
  ULONGLONG size = 0;
  return !FileSizeOf(g_path, size) || size == 0;
}

}  // namespace

void LogInit(const std::wstring& dir) {
  Lock lock;
  g_path = dir.empty() ? std::wstring() : dir + L"\\" + kLogFile;
  if (!g_path.empty()) RotateIfNeeded();
}

void LogClose() {
  Lock lock;
  g_path.clear();
}

void LogLine(char level, const wchar_t* module, const std::wstring& msg, DWORD gle) {
  std::wstring line = FormatLine(level, module, msg, gle);
  Lock lock;
  if (g_path.empty()) return;
  const bool bom = IsNewFile();
  AppendUtf8(ToUtf8(line), bom);
  RotateIfNeeded();
}

void LogFatalRaw(const wchar_t* module, const wchar_t* msg) {
  wchar_t stamp[32];
  StampLocal(stamp);
  wchar_t line[512];                        // 兜底路径：全栈缓冲，不申请堆
  swprintf(line, 512, L"[%s] [E] [%s] %s\r\n", stamp, module ? module : L"fatal", msg ? msg : L"");

  Lock lock;
  if (g_path.empty()) return;
  HANDLE f = CreateFileW(g_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  const bool bom = IsNewFile();
  SetFilePointer(f, 0, nullptr, FILE_END);
  char narrow[2048];
  int got = WideCharToMultiByte(CP_UTF8, 0, line, -1, narrow, (int)sizeof(narrow), nullptr, nullptr);
  DWORD written = 0;
  if (bom) WriteFile(f, "\xEF\xBB\xBF", 3, &written, nullptr);
  if (got > 1) WriteFile(f, narrow, (DWORD)(got - 1), &written, nullptr);
  CloseHandle(f);
}

}  // namespace sc
