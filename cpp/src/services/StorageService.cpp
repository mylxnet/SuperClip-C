#include "StorageService.h"
#include "../core/Json.h"
#include "../core/Text.h"
#include "../core/Time.h"
#include "../util/Log.h"
#include <algorithm>
#include <filesystem>

namespace sc {
namespace {

bool ReadWholeFile(const std::wstring& path, std::string& out) {
  out.clear();
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;       // 不存在（首次运行）是正常状态

  LARGE_INTEGER size{};
  const bool okSize = GetFileSizeEx(f, &size) != FALSE;
  bool ok = okSize && size.QuadPart <= 64ll * 1024 * 1024;   // 上限保护：正常 <1MB
  if (okSize && size.QuadPart > 64ll * 1024 * 1024) LogError(L"storage", L"history.json 超出预期大小，放弃读取");
  if (ok && size.QuadPart > 0) {
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t done = 0;
    while (done < out.size()) {
      DWORD read = 0;
      const DWORD want = DWORD(std::min<size_t>(out.size() - done, 1u << 20));
      if (!ReadFile(f, out.data() + done, want, &read, nullptr) || read == 0) { ok = false; break; }
      done += read;
    }
    out.resize(done);
  }
  CloseHandle(f);
  if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
      static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
    out.erase(0, 3);                                 // 容忍带 BOM 的旧文件
  return ok;
}

bool WriteWholeFile(const std::wstring& path, const std::string& bytes) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  size_t done = 0;
  bool ok = true;
  while (done < bytes.size()) {
    const DWORD want = DWORD(std::min<size_t>(bytes.size() - done, 1u << 20));
    if (!WriteFile(f, bytes.data() + done, want, &written, nullptr) || written == 0) { ok = false; break; }
    done += written;
  }
  if (ok) ok = FlushFileBuffers(f) != FALSE;
  CloseHandle(f);
  return ok;
}

JsonValue ToJson(const ClipItem& item) {
  JsonValue obj = JsonValue::MakeObject();
  obj.set(L"Id", JsonValue::MakeString(item.id));
  obj.set(L"Content", JsonValue::MakeString(item.content));
  obj.set(L"Type", JsonValue::MakeNumber(double(static_cast<int>(item.type))));
  // .NET 侧是 int?，0 号写作 null 以保持语义
  obj.set(L"SourceRow", item.sourceRow > 0 ? JsonValue::MakeNumber(item.sourceRow) : JsonValue{});
  obj.set(L"SourceCol", item.sourceCol > 0 ? JsonValue::MakeNumber(item.sourceCol) : JsonValue{});
  obj.set(L"Timestamp", JsonValue::MakeString(ToLocalIso(item.createdUtc)));
  obj.set(L"Hash", JsonValue::MakeString(item.hash));
  obj.set(L"IsFavorite", JsonValue::MakeBool(item.isFavorite));
  obj.set(L"IsPasted", JsonValue::MakeBool(item.isPasted));
  return obj;
}

std::unique_ptr<ClipItem> FromJson(const JsonValue& obj) {
  if (!obj.isObject()) return nullptr;
  std::wstring content = obj.at(L"Content").asString();
  if (content.size() > kMaxContentChars) {           // T3 上限对读侧同样生效（哈希按截断后内容重算）
    content.resize(kMaxContentChars);
    LogWarn(L"storage", L"history.json 有条目超出长度上限，已截断");
  }
  const double rawType = obj.at(L"Type").asNumber(0);
  ClipType type = ClipType::Text;
  if (rawType == 1.0) type = ClipType::TableCell;
  else if (rawType != 0.0) LogWarn(L"storage", L"history.json 含未知 Type，按文本处理");

  const int row = int(obj.at(L"SourceRow").asNumber(0));
  const int col = int(obj.at(L"SourceCol").asNumber(0));

  FILETIME created{};
  const auto parsed = ParseIso(obj.at(L"Timestamp").asString());
  created = parsed.value_or(NowUtc());

  auto item = MakeItem(content, type, row, col, created);
  const std::wstring& id = obj.at(L"Id").asString();
  if (!id.empty()) item->id = id;                    // 保留原主键，重启后条目可追溯
  item->isFavorite = obj.at(L"IsFavorite").asBool(false);
  item->isPasted = obj.at(L"IsPasted").asBool(false);
  RefreshFoldKeys(*item);
  return item;
}

}  // namespace

StorageService::StorageService(std::wstring path) : path_(std::move(path)) {
  tmpPath_ = path_ + L".tmp";
}

bool StorageService::EnsureDir() {
  if (path_.empty()) return false;
  std::error_code ec;
  const auto parent = std::filesystem::path(path_).parent_path();
  if (parent.empty()) return true;
  std::filesystem::create_directories(parent, ec);
  return !ec || std::filesystem::exists(parent, ec);
}

std::vector<std::unique_ptr<ClipItem>> StorageService::Load() {
  std::vector<std::unique_ptr<ClipItem>> items;
  if (path_.empty()) return items;

  std::string bytes;
  if (!ReadWholeFile(path_, bytes)) return items;    // 不存在/空 → 空列表（首次运行）
  if (bytes.empty()) return items;

  JsonValue root;
  std::wstring err;
  if (!JsonParse(bytes, root, err) || !root.isArray()) {
    corrupted_ = true;
    LogError(L"storage", L"history.json 解析失败，历史已重置: " + err);
    return items;
  }
  size_t skipped = 0;
  for (const auto& obj : root.items()) {
    auto item = FromJson(obj);
    if (item) items.push_back(std::move(item));
    else ++skipped;                                  // 单条损坏只跳过该条，尽量保数据
  }
  if (skipped) LogWarn(L"storage", L"history.json 跳过 " + std::to_wstring(skipped) + L" 条损坏记录");
  return items;
}

bool StorageService::Save(const std::vector<std::unique_ptr<ClipItem>>& items) {
  if (path_.empty()) return false;

  JsonValue arr = JsonValue::MakeArray();
  for (const auto& item : items) {
    if (item) arr.push(ToJson(*item));
  }
  const std::string bytes = JsonWrite(arr);
  if (!WriteWholeFile(tmpPath_, bytes)) {
    LogWarn(L"storage", L"history.json.tmp 写入失败");
    DeleteFileW(tmpPath_.c_str());
    return false;
  }

  // 三级降级：ReplaceFileW → MoveFileExW(replace) → 删除后 move（§5.3）
  if (ReplaceFileW(path_.c_str(), tmpPath_.c_str(), nullptr,
                   REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
    return true;
  if (MoveFileExW(tmpPath_.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  DeleteFileW(path_.c_str());
  if (MoveFileExW(tmpPath_.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;

  LogWarn(L"storage", L"history.json 原子替换失败，内存态继续可用");
  DeleteFileW(tmpPath_.c_str());
  return false;
}

}  // namespace sc
