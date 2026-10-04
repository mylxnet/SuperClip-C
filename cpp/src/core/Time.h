#pragma once
#include "Config.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// 时间：内部统一 UTC FILETIME 存储；序列化写"naive 本地时间"以兼容既有 history.json。
namespace sc {

FILETIME NowUtc();
uint64_t FtToU64(const FILETIME& ft);
FILETIME U64ToFt(uint64_t v);

// → "YYYY-MM-DDTHH:MM:SS.mmm"（本地时区，无偏移后缀）
std::wstring ToLocalIso(const FILETIME& utc);
// 兼容三种形态：无毫秒 / 1~7 位小数 / 带 Z 或 ±HH:MM 偏移
std::optional<FILETIME> ParseIso(std::wstring_view text);
// → "HH:mm:ss"（本地）
std::wstring FormatHms(const FILETIME& utc);

}  // namespace sc
