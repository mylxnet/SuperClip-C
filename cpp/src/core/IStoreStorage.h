#pragma once
#include "ClipItem.h"
#include <memory>
#include <vector>

namespace sc {

// core 层不依赖 services：持久化以本接口注入（生产 = StorageService，单测 = 临时目录）
class IStoreStorage {
 public:
  virtual ~IStoreStorage() = default;
  virtual bool EnsureDir() = 0;
  virtual std::vector<std::unique_ptr<ClipItem>> Load() = 0;
  virtual bool Save(const std::vector<std::unique_ptr<ClipItem>>& items) = 0;
  virtual bool Corrupted() const = 0;
};

}  // namespace sc
