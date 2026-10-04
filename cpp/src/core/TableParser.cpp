#include "TableParser.h"
#include "Text.h"

namespace sc {

bool IsTable(std::wstring_view text, CopyMode mode) {
  if (text.find(L'\t') != std::wstring_view::npos) return true;      // 含制表符恒为表格
  if (mode == CopyMode::TableSingleColumn &&
      text.find(L'\n') != std::wstring_view::npos) {                 // 多行单列模式
    return TrimNewlines(text).find(L'\n') != std::wstring::npos;
  }
  return false;
}

std::vector<Cell> Parse(std::wstring_view text) {
  std::vector<Cell> cells;
  const std::wstring normalized = TrimNewlines(NormalizeNewlines(text));
  if (normalized.empty()) return cells;

  const std::vector<std::wstring> rows = SplitBy(normalized, L'\n');
  for (size_t r = 0; r < rows.size(); ++r) {
    const std::wstring& row = rows[r];
    if (row.empty()) continue;                     // 空行跳过，行号不前移
    if (row.find(L'\t') == std::wstring::npos) {   // 单列行：整行一个单元格
      cells.push_back(Cell{row, static_cast<int>(r) + 1, 1});
      continue;
    }
    const std::vector<std::wstring> cols = SplitBy(row, L'\t');
    for (size_t c = 0; c < cols.size(); ++c) {
      if (cols[c].empty()) continue;               // 空单元格跳过，列号不前移
      cells.push_back(Cell{cols[c], static_cast<int>(r) + 1, static_cast<int>(c) + 1});
    }
  }
  return cells;
}

}  // namespace sc
