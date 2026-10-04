#pragma once
#include "Config.h"
#include <string>
#include <string_view>
#include <vector>

// 文本工具：全程以 UTF-16 wstring 为工作编码，仅落盘转 UTF-8。
// 折叠规则（技术方案 §4.2）：全角→半角 + ASCII 大小写；明确不对西里尔/希腊字母折叠。
namespace sc {

std::string ToUtf8(std::wstring_view s);
std::wstring  FromUtf8(std::string_view s);          // 非法字节 → U+FFFD，不抛

std::wstring FoldKey(std::wstring_view s);           // 用于搜索的派生键
bool ContainsFolded(std::wstring_view hayFolded, std::wstring_view needleFolded);

std::wstring NormalizeNewlines(std::wstring_view text);   // \r\n 与 \r → \n
std::vector<std::wstring> SplitBy(std::wstring_view text, wchar_t delim);
std::wstring TrimNewlines(std::wstring_view text);        // 仅去首尾换行

std::wstring PreviewText(std::wstring_view content);      // 渲染前：制表符→空格、截断
std::wstring BasenameOf(std::wstring_view path);
std::wstring ToLowerAscii(std::wstring_view s);

}  // namespace sc
