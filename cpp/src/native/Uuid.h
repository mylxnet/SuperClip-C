#pragma once
#include "../core/Config.h"
#include <iterator>
#include <string>

// GUID（CoCreateGuid）→ 小写无花括号字符串，匹配 C# Guid.ToString() 的输出形态。
namespace sc {

inline std::wstring NewGuidString() {
  GUID g{};
  if (FAILED(CoCreateGuid(&g))) return L"";
  wchar_t buf[40]{};
  if (StringFromGUID2(g, buf, static_cast<int>(std::size(buf))) == 0) return L"";
  std::wstring s(buf);
  if (!s.empty() && s.front() == L'{') s.erase(s.begin());
  if (!s.empty() && s.back() == L'}') s.pop_back();
  for (wchar_t& c : s) {
    if (c >= L'A' && c <= L'F') c = static_cast<wchar_t>(c + 32);
  }
  return s;
}

}  // namespace sc
