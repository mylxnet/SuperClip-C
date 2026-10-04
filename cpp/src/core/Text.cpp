#include "Text.h"
#include <algorithm>

namespace sc {

std::string ToUtf8(std::wstring_view s) {
  if (s.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                    nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring FromUtf8(std::string_view s) {
  if (s.empty()) return {};
  // 快路径：整串合法 UTF-8
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n > 0) {
    std::wstring out(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                            static_cast<int>(s.size()), out.data(), n) == n) {
      return out;
    }
  }
  // 慢路径：逐码元解码，坏字节替换 U+FFFD，绝不抛异常
  std::wstring out;
  out.reserve(s.size());
  wchar_t buf[4];
  size_t i = 0;
  while (i < s.size()) {
    bool ok = false;
    for (int len = 4; len >= 1; --len) {
      if (static_cast<size_t>(len) > s.size() - i) continue;
      const int got = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          s.data() + i, len, buf, 4);
      if (got > 0) {
        out.append(buf, static_cast<size_t>(got));
        i += static_cast<size_t>(len);
        ok = true;
        break;
      }
    }
    if (!ok) {
      out.push_back(L'\uFFFD');
      ++i;
    }
  }
  return out;
}

std::wstring FoldKey(std::wstring_view s) {
  std::wstring out;
  out.reserve(s.size());
  for (const wchar_t c : s) {
    wchar_t f = c;
    if (c >= 0xFF01 && c <= 0xFF5E) f = static_cast<wchar_t>(c - 0xFEE0);  // 全角 ASCII
    else if (c == 0x3000)           f = L' ';                              // 全角空格
    else if (c >= L'A' && c <= L'Z') f = static_cast<wchar_t>(c + 32);     // ASCII 大写
    out.push_back(f);
  }
  return out;
}

bool ContainsFolded(std::wstring_view hay, std::wstring_view needle) {
  if (needle.empty()) return true;
  if (hay.size() < needle.size()) return false;
  return std::wstring_view(hay).find(needle) != std::wstring_view::npos;
}

std::wstring NormalizeNewlines(std::wstring_view text) {
  std::wstring out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const wchar_t c = text[i];
    if (c == L'\r') {
      out.push_back(L'\n');
      if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;   // \r\n → 单个 \n
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::vector<std::wstring> SplitBy(std::wstring_view text, wchar_t delim) {
  std::vector<std::wstring> parts;
  size_t start = 0;
  while (true) {
    const size_t at = text.find(delim, start);
    if (at == std::wstring_view::npos) {
      parts.emplace_back(text.substr(start));
      break;
    }
    parts.emplace_back(text.substr(start, at - start));
    start = at + 1;
  }
  return parts;
}

std::wstring TrimNewlines(std::wstring_view text) {
  size_t b = 0, e = text.size();
  while (b < e && text[b] == L'\n') ++b;
  while (e > b && text[e - 1] == L'\n') --e;
  return std::wstring(text.substr(b, e - b));
}

std::wstring PreviewText(std::wstring_view content) {
  std::wstring out;
  out.reserve(content.size() + 8);
  size_t taken = 0;
  for (const wchar_t c : content) {
    if (taken >= kPreviewMeasureChars) {
      out.append(L"…");
      break;
    }
    if (c == L'\t') {
      out.append(L"    ");
    } else if (c == L'\r') {
      out.push_back(L'\n');
    } else if (c != L'\n' || !out.empty()) {
      out.push_back(c);   // 首尾换行由后续排版处理，中间换行保留
    }
    if (c != L'\r') ++taken;
  }
  return out;
}

std::wstring BasenameOf(std::wstring_view path) {
  const size_t at = path.find_last_of(L"\\/");
  return std::wstring(at == std::wstring_view::npos ? path : path.substr(at + 1));
}

std::wstring ToLowerAscii(std::wstring_view s) {
  std::wstring out(s);
  for (wchar_t& c : out) {
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
  }
  return out;
}

}  // namespace sc
