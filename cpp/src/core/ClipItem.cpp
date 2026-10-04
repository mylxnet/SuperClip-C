#include "ClipItem.h"
#include "../native/Uuid.h"
#include "Sha256.h"
#include "Text.h"
#include "Time.h"

namespace sc {

std::wstring ClipItem::SourceLabel() const {
  if (type != ClipType::TableCell || sourceRow <= 0 || sourceCol <= 0) return {};
  wchar_t buf[48];
  swprintf(buf, static_cast<size_t>(std::size(buf)), L"来自表格：第 %d 行 第 %d 列",
           sourceRow, sourceCol);
  return buf;
}

static void ComputeDerived(ClipItem& item) {
  item.hash = HexSha256Utf8(item.content);
  item.foldContent = FoldKey(item.content);
  item.foldLabel = FoldKey(item.SourceLabel());
}

std::unique_ptr<ClipItem> MakeItem(std::wstring content, ClipType type,
                                   int row, int col, FILETIME created) {
  auto item = std::make_unique<ClipItem>();
  item->id = NewGuidString();
  item->content = std::move(content);
  item->type = type;
  item->sourceRow = row;
  item->sourceCol = col;
  item->createdUtc = (FtToU64(created) == 0) ? NowUtc() : created;
  ComputeDerived(*item);
  return item;
}

void RefreshFoldKeys(ClipItem& item) {
  item.foldContent = FoldKey(item.content);
  item.foldLabel = FoldKey(item.SourceLabel());
  // 磁盘数据的 hash 若缺失则补算，保证去重键始终可用
  if (item.hash.empty() && !item.content.empty()) item.hash = HexSha256Utf8(item.content);
}

}  // namespace sc
