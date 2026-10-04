#pragma once
#include "../core/IStoreStorage.h"

namespace sc {

// 技术方案 §5.3。history.json 与 .NET 版字段级兼容：
// 顺序 Id,Content,Type,SourceRow,SourceCol,Timestamp,Hash,IsFavorite,IsPasted；
// SourceRow/Col 为 0 时写 null（对应 C# int?）。
// path 由外部注入（生产走 AppDirs::HistoryPath()，单测用临时目录）。
class StorageService : public IStoreStorage {
 public:
  explicit StorageService(std::wstring path);

  bool EnsureDir() override;
  std::vector<std::unique_ptr<ClipItem>> Load() override;                    // 失败 → 空列表
  bool Save(const std::vector<std::unique_ptr<ClipItem>>& items) override;   // tmp + ReplaceFileW
  bool Corrupted() const override { return corrupted_; }

 private:
  std::wstring path_;
  std::wstring tmpPath_;
  bool corrupted_ = false;
};

}  // namespace sc
