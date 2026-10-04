#include "Settings.h"
#include "Json.h"
#include "../util/Log.h"
#include <cmath>
#include <optional>

// settings.json 体积恒为几十字节，所以这里的 IO 比 StorageService 更简单：
// 一次读完、一次写完，不判 64MB 上限也不分块（两份实现各按自己的尺寸假设写，
// 强行合并反而要让小文件背上大文件的防御分支）。原子替换的三级降级同 §8.4。
namespace sc {
namespace {

constexpr int kCoordLimit = 1000000;   // 坐标量级上限：超过就是坏数据，位置按「未落盘」处理
constexpr int kMaxSize = 8192;         // 单方向尺寸上限（DIP），正常 380×600

// 只认 JSON 数字；超出 limit 的量级视为坏数据（连坐标带尺寸都拒收荒谬值）
std::optional<int> ToInt(const JsonValue& v, int limit) {
  if (v.kind() != JsonValue::Kind::Number) return std::nullopt;
  const double d = v.asNumber();
  if (!std::isfinite(d) || d < -double(limit) || d > double(limit)) return std::nullopt;
  return int(std::lround(d));
}

bool ReadSmallFile(const std::wstring& path, std::string& out) {
  out.clear();
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;      // 不存在（首次运行）是正常状态
  char buf[8192];
  DWORD read = 0;
  const BOOL ok = ReadFile(f, buf, DWORD(sizeof(buf) - 1), &read, nullptr);
  CloseHandle(f);
  if (!ok || read == 0) return false;
  out.assign(buf, buf + read);
  if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
      static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
    out.erase(0, 3);                                // 容忍带 BOM 的旧文件
  return true;
}

bool WriteSmallFile(const std::wstring& path, const std::string& bytes) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) &&
                  written == bytes.size();
  const BOOL flushed = FlushFileBuffers(f);
  CloseHandle(f);
  return ok && flushed;
}

JsonValue ToJson(const Settings& s) {
  JsonValue obj = JsonValue::MakeObject();
  // 键序与 .NET 版一致，便于两版交替写同一份文件时做逐字节对比
  obj.set(L"Left", JsonValue::MakeNumber(s.left));
  obj.set(L"Top", JsonValue::MakeNumber(s.top));
  obj.set(L"Width", JsonValue::MakeNumber(s.width));
  obj.set(L"Height", JsonValue::MakeNumber(s.height));
  obj.set(L"BoundProcessName",
          s.boundProcessName.empty() ? JsonValue{} : JsonValue::MakeString(s.boundProcessName));
  obj.set(L"Topmost", JsonValue::MakeBool(s.topmost));
  obj.set(L"PasteMode", JsonValue::MakeNumber(s.pasteMode));
  obj.set(L"SplitSingleColumn", JsonValue::MakeBool(s.splitSingleColumn));
  obj.set(L"FilterType", JsonValue::MakeNumber(s.filterType));
  return obj;
}

}  // namespace

Settings LoadSettings(const std::wstring& path) {
  Settings s;   // 默认值即设计方案 §8.3 的默认列
  if (path.empty()) return s;

  std::string bytes;
  if (!ReadSmallFile(path, bytes)) return s;

  JsonValue root;
  std::wstring err;
  if (!JsonParse(bytes, root, err) || !root.isObject()) {
    LogError(L"settings", L"settings.json 解析失败，本次按默认值运行: " + err);
    return s;
  }

  const std::optional<int> left = ToInt(root.at(L"Left"), kCoordLimit);
  const std::optional<int> top = ToInt(root.at(L"Top"), kCoordLimit);
  if (root.has(L"Left") && root.has(L"Top") && left && top) {
    s.left = *left;
    s.top = *top;
    s.hasRect = true;
  } else if (root.has(L"Left") || root.has(L"Top")) {
    LogWarn(L"settings", L"settings.json 的 Left/Top 不可用，按默认停靠");
  }

  const std::optional<int> width = ToInt(root.at(L"Width"), kMaxSize);
  if (width && *width >= kMinW && *width <= kMaxSize) s.width = *width;
  else if (root.has(L"Width")) LogWarn(L"settings", L"settings.json 的 Width 越界，用默认 380");

  const std::optional<int> height = ToInt(root.at(L"Height"), kMaxSize);
  if (height && *height >= kMinH && *height <= kMaxSize) s.height = *height;
  else if (root.has(L"Height")) LogWarn(L"settings", L"settings.json 的 Height 越界，用默认 600");

  s.boundProcessName = root.at(L"BoundProcessName").asString();   // null/缺失 → 空串
  s.topmost = root.at(L"Topmost").asBool(true);                   // C13：缺省即置顶

  // 枚举序号只认文档里的取值，越界一律回默认（防止旧版/手改文件把 UI 带进未知分支）
  const std::optional<int> mode = ToInt(root.at(L"PasteMode"), kMaxSize);
  s.pasteMode = (mode && (*mode == 1)) ? 1 : 0;

  const std::optional<int> filter = ToInt(root.at(L"FilterType"), kMaxSize);
  s.filterType = (filter && *filter >= 0 && *filter <= 3) ? *filter : 0;

  s.splitSingleColumn = root.at(L"SplitSingleColumn").asBool(false);
  return s;
}

bool SaveSettings(const std::wstring& path, const Settings& s) {
  if (path.empty()) return false;
  const std::wstring tmp = path + L".tmp";
  const std::string bytes = JsonWrite(ToJson(s));
  if (!WriteSmallFile(tmp, bytes)) {
    LogWarn(L"settings", L"settings.json.tmp 写入失败");
    DeleteFileW(tmp.c_str());
    return false;
  }
  if (ReplaceFileW(path.c_str(), tmp.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
    return true;
  if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  DeleteFileW(path.c_str());
  if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;

  LogWarn(L"settings", L"settings.json 原子替换失败，内存态继续可用");
  DeleteFileW(tmp.c_str());
  return false;
}

}  // namespace sc
