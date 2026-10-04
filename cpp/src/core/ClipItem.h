#pragma once
#include "Config.h"
#include <memory>
#include <string>

namespace sc {

enum class ClipType : int { Text = 0, TableCell = 1 };   // 数值序列化，兼容既有 history.json

struct ClipItem {
  std::wstring id;             // GUID 小写无花括号
  std::wstring content;        // 原文（T3 已在入列前截断）
  std::wstring hash;           // SHA-256 小写 hex；空内容 → 空串
  std::wstring foldContent;    // 派生：折叠键，入列时算一次，不持久化
  std::wstring foldLabel;      // 派生：SourceLabel 的折叠键（仅供搜索，标注本身不上屏）
  ClipType type = ClipType::Text;
  int sourceRow = 0;           // 1 起；0 = 无（普通文本）
  int sourceCol = 0;
  FILETIME createdUtc{};       // 统一 UTC 存储
  bool isFavorite = false;     // 可变 → 触发 UI
  bool isPasted = false;       // 可变 → 触发 UI

  // "来自表格：第 X 行 第 Y 列" | L""（计算属性）。2026-10-04 用户决议：行内不再显示，
  // 但仍作为搜索命中字段（Store 的 foldLabel），故必须保留。
  std::wstring SourceLabel() const;
};

// 唯一构造入口：一次算好 hash 与两个折叠键，保证派生字段与内容一致。
// created == 0 时取当前时间（单测传入固定时间以确定性验证排序/复位）。
// 约定：id/content/hash/createdUtc/type/row/col 一经工厂写入即不再改动（无 setter 语义）。
std::unique_ptr<ClipItem> MakeItem(std::wstring content, ClipType type,
                                   int row = 0, int col = 0, FILETIME created = {});

// 从磁盘加载后重算派生键（content 已定，不改 id/时间/哈希）
void RefreshFoldKeys(ClipItem& item);

}  // namespace sc
