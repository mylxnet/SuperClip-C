#pragma once
#include "Config.h"
#include <string>
#include <string_view>
#include <vector>

namespace sc {

// 复制模式开关（原 SplitSingleColumn）：由 UI 层写入服务，不持久化到 history。
enum class CopyMode { Normal = 0, TableSingleColumn = 1 };

struct Cell {
  std::wstring content;
  int row = 0;   // 1 起
  int col = 0;   // 1 起
};

// FR-05：含 \t 恒为表格；否则仅当 TableSingleColumn 且含换行才是表格。
bool IsTable(std::wstring_view text, CopyMode mode);

// 按「行→列」拆单元格；空单元格跳过但列号不前移，空行跳过但行号不前移。
std::vector<Cell> Parse(std::wstring_view text);

}  // namespace sc
