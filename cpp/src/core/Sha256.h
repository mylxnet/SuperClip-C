#pragma once
#include "Config.h"
#include <bcrypt.h>
#include <string>
#include <string_view>

// SHA-256（本地 BCrypt，不联网）。空内容约定返回空串、不做计算，
// 与原 .NET 版 StorageService.ComputeHash 行为一致（§9.3 用例 2）。
namespace sc {

std::wstring HexSha256Utf8(std::wstring_view content);

}  // namespace sc
