#include "Time.h"
#include <cwchar>

namespace sc {

FILETIME NowUtc() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  return ft;
}

uint64_t FtToU64(const FILETIME& ft) {
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

FILETIME U64ToFt(uint64_t v) {
  FILETIME ft{};
  ft.dwLowDateTime = static_cast<DWORD>(v & 0xFFFFFFFFull);
  ft.dwHighDateTime = static_cast<DWORD>(v >> 32);
  return ft;
}

static int MsOf(const FILETIME& ft) {
  return static_cast<int>((FtToU64(ft) % 10000000ull) / 10000ull);   // 100ns → ms
}

std::wstring ToLocalIso(const FILETIME& utc) {
  SYSTEMTIME st{};
  SYSTEMTIME local{};
  if (!FileTimeToSystemTime(&utc, &st)) return {};
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &st, &local)) local = st;
  wchar_t buf[32];
  swprintf(buf, 32, L"%04u-%02u-%02uT%02u:%02u:%02u.%03u",
           local.wYear, local.wMonth, local.wDay,
           local.wHour, local.wMinute, local.wSecond, MsOf(utc));
  return buf;
}

std::wstring FormatHms(const FILETIME& utc) {
  SYSTEMTIME st{};
  SYSTEMTIME local{};
  if (!FileTimeToSystemTime(&utc, &st)) return L"--:--:--";
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &st, &local)) local = st;
  wchar_t buf[16];
  swprintf(buf, 16, L"%02u:%02u:%02u", local.wHour, local.wMinute, local.wSecond);
  return buf;
}

// ---- 解析辅助 ----
static bool ReadDigits(std::wstring_view s, size_t& i, size_t count, long& out) {
  if (i + count > s.size()) return false;
  long v = 0;
  for (size_t k = 0; k < count; ++k) {
    const wchar_t c = s[i + k];
    if (c < L'0' || c > L'9') return false;
    v = v * 10 + (c - L'0');
  }
  i += count;
  out = v;
  return true;
}

std::optional<FILETIME> ParseIso(std::wstring_view text) {
  if (text.size() < 19) return std::nullopt;
  size_t i = 0;
  long y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  if (!ReadDigits(text, i, 4, y) || text[i] != L'-') return std::nullopt;
  ++i;
  if (!ReadDigits(text, i, 2, mo) || text[i] != L'-') return std::nullopt;
  ++i;
  if (!ReadDigits(text, i, 2, d) || text[i] != L'T') return std::nullopt;
  ++i;
  if (!ReadDigits(text, i, 2, h) || text[i] != L':') return std::nullopt;
  ++i;
  if (!ReadDigits(text, i, 2, mi) || text[i] != L':') return std::nullopt;
  ++i;
  if (!ReadDigits(text, i, 2, se)) return std::nullopt;

  long frac100ns = 0;
  if (i < text.size() && text[i] == L'.') {
    ++i;
    size_t start = i;
    long digits = 0, v = 0;
    while (i < text.size() && digits < 7 && text[i] >= L'0' && text[i] <= L'9') {
      v = v * 10 + (text[i] - L'0');
      ++i;
      ++digits;
    }
    if (digits == 0 || i - start > 7) return std::nullopt;
    for (long k = digits; k < 7; ++k) v *= 10;              // 右侧补零到 100ns
    frac100ns = v;
  }

  // 偏移：Z / +HH:MM / -HH:MM / +HHMM
  bool hasOffset = false, offsetMinus = false, utcInput = false;
  long offsetMinutes = 0;
  if (i < text.size()) {
    if (text[i] == L'Z' || text[i] == L'z') {
      utcInput = true;
      ++i;
    } else if (text[i] == L'+' || text[i] == L'-') {
      offsetMinus = (text[i] == L'-');
      ++i;
      long oh = 0, om = 0;
      if (!ReadDigits(text, i, 2, oh)) return std::nullopt;
      if (i < text.size() && text[i] == L':') ++i;
      if (i < text.size()) {
        if (!ReadDigits(text, i, 2, om)) return std::nullopt;
      }
      hasOffset = true;
      offsetMinutes = oh * 60 + om;
    } else {
      return std::nullopt;                                   // 尾随垃圾视为损坏
    }
  }

  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(y);
  st.wMonth = static_cast<WORD>(mo);
  st.wDay = static_cast<WORD>(d);
  st.wHour = static_cast<WORD>(h);
  st.wMinute = static_cast<WORD>(mi);
  st.wSecond = static_cast<WORD>(se);
  st.wMilliseconds = 0;                                      // 小数部分单独按 100ns 加

  FILETIME base{};
  if (!SystemTimeToFileTime(&st, &base)) return std::nullopt;  // 非法月/日在此被拒
  uint64_t utc = FtToU64(base) + static_cast<uint64_t>(frac100ns);

  if (utcInput) {
    return U64ToFt(utc);                                     // 已是 UTC
  }
  if (hasOffset) {
    const int64_t delta = static_cast<int64_t>(offsetMinutes) * 60ll * 10000000ll;
    const int64_t v = static_cast<int64_t>(utc) - (offsetMinus ? -delta : delta);
    if (v < 0) return std::nullopt;
    return U64ToFt(static_cast<uint64_t>(v));
  }
  // naive 本地时间 → UTC（中国区无夏令时，歧义为零）
  SYSTEMTIME utcSt{};
  FILETIME local{};
  local.dwLowDateTime = static_cast<DWORD>(utc & 0xFFFFFFFFu);
  local.dwHighDateTime = static_cast<DWORD>(utc >> 32);
  if (!FileTimeToSystemTime(&local, &utcSt)) return std::nullopt;
  SYSTEMTIME outSt{};
  if (!TzSpecificLocalTimeToSystemTime(nullptr, &utcSt, &outSt)) return std::nullopt;
  FILETIME outFt{};
  if (!SystemTimeToFileTime(&outSt, &outFt)) return std::nullopt;
  return outFt;
}

}  // namespace sc
