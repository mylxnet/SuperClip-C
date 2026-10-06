#include "Store.h"
#include "Text.h"
#include "Time.h"
#include "../util/Log.h"
#include <algorithm>
#include <iterator>

namespace sc {
namespace {

bool NewerFirst(const std::unique_ptr<ClipItem>& a, const std::unique_ptr<ClipItem>& b) {
  return FtToU64(a->createdUtc) > FtToU64(b->createdUtc);
}

}  // namespace

size_t Store::Boundary() const {
  // 不变式保证收藏全部在前，故收藏数量即分界
  return size_t(std::count_if(items_.begin(), items_.end(),
                              [](const auto& p) { return p->isFavorite; }));
}

ClipItem* Store::Mutable(const ClipItem* item) {
  for (auto& p : items_) {
    if (p.get() == item) return p.get();
  }
  return nullptr;
}

size_t Store::IndexOf(const ClipItem* item) const {
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].get() == item) return i;
  }
  return npos;
}

void Store::RemoveAt(size_t idx) {
  if (idx >= items_.size()) return;
  if (selected_ == items_[idx].get()) selected_ = nullptr;
  items_.erase(items_.begin() + idx);
}

bool Store::TakeFavoriteIfDuplicate(const std::wstring& hash) {
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i]->hash == hash) {
      const bool wasFavorite = items_[i]->isFavorite;
      RemoveAt(i);                                  // FR-02：旧条移除，收藏态迁移到新条
      return wasFavorite;
    }
  }
  return false;
}

void Store::Emit(StoreEventKind kind, const ClipItem* primary) {
  if (onEvent) onEvent(StoreEvent{kind, primary});
}

bool Store::Corrupted() const { return storage_.Corrupted(); }

void Store::LoadFromDisk() {
  items_ = storage_.Load();
  ApplyOrder();
  EnforceLimit();
  storage_.Save(items_);                            // 把复位后的规范化顺序写回
  selected_ = nullptr;
  RebuildDisplay();
  Emit(StoreEventKind::FullReplaced);
}

void Store::AddFromClipboard(std::wstring_view text) {
  std::wstring content(text);
  if (content.size() > kMaxContentChars) {          // T3
    content.resize(kMaxContentChars);
    LogWarn(L"store", L"剪贴板内容超出上限，已截断");
  }
  if (content.empty()) return;

  // 同一批（尤其表格多格）共用一个时间戳，避免 Reset 时因毫秒差被打乱
  const FILETIME stamp = NowUtc();
  std::vector<std::unique_ptr<ClipItem>> fresh;

  if (IsTable(content, copyMode_)) {
    for (const Cell& cell : Parse(content)) {
      auto item = MakeItem(cell.content, ClipType::TableCell, cell.row, cell.col, stamp);
      item->isFavorite = TakeFavoriteIfDuplicate(item->hash);
      fresh.push_back(std::move(item));
    }
  } else {
    auto item = MakeItem(std::move(content), ClipType::Text, 0, 0, stamp);
    item->isFavorite = TakeFavoriteIfDuplicate(item->hash);
    fresh.push_back(std::move(item));
  }
  if (fresh.empty()) return;

  const size_t at = Boundary();                     // 非收藏区最前 = 最新位
  items_.insert(items_.begin() + long(at),          // 整块插入：逐条头插会整体倒序
                std::make_move_iterator(fresh.begin()),
                std::make_move_iterator(fresh.end()));
  // 表格逐格去重可能带回 FR-02 迁移来的收藏项（isFavorite 由 TakeFavoriteIfDuplicate 赋回），
  // 而 batch 内部次序是单元格顺序，收藏项未必排在最前 → 它在非收藏区中间时，
  // [收藏区|非收藏区] 不变式被破坏，ClearAll 按"收藏数量"抹尾会误删收藏（违反 C12）。
  // 本行与点★同语义（ToggleFavorite 亦走 ApplyOrder）恢复分区；整批皆非收藏时是幂等 no-op。
  ApplyOrder();
  EnforceLimit();
  storage_.Save(items_);
  RebuildDisplay();
  Emit(StoreEventKind::ItemsInserted);
}

void Store::EnforceLimit() {
  const size_t nonFavorite = items_.size() - Boundary();
  if (nonFavorite <= size_t(kMaxItems)) return;
  // C7：非收藏区末尾 = 最旧 → 批量删除超额尾部区间
  const size_t excess = nonFavorite - size_t(kMaxItems);
  const ClipItem* selected = selected_;
  items_.erase(items_.end() - long(excess), items_.end());
  if (selected && !Mutable(selected)) selected_ = nullptr;   // 只有选中项真被删掉才复位
  LogInfo(L"store", L"超出上限，淘汰最旧 " + std::to_wstring(excess) + L" 条");
}

void Store::ApplyOrder() {
  std::stable_partition(items_.begin(), items_.end(),
                        [](const auto& p) { return p->isFavorite; });   // FR-08：各区相对顺序不变
}

// 点★只翻转收藏态并按分区重排集合，**不**重算显示视图（2026-10-06 用户决议）：
// 该条目留在屏幕上原位、星标立刻变化；等下一次任何 RebuildDisplay()（切视图/搜索/
// 复制新内容/粘贴沉底/清除/复位/重启）才按新状态把它移出【全部】等页面。
// 代价是这段时间内 display_ 与 filter_ 允许暂时不自洽（点★前已在屏上的那条不会被撤走）。
void Store::ToggleFavorite(const ClipItem* item) {
  ClipItem* mut = Mutable(item);
  if (!mut) return;
  mut->isFavorite = !mut->isFavorite;
  ApplyOrder();
  storage_.Save(items_);
  Emit(StoreEventKind::FlagsChanged, mut);
}

void Store::MoveToBack(const ClipItem* item) {
  const size_t idx = IndexOf(item);
  if (idx == npos) return;
  const bool favorite = items_[idx]->isFavorite;
  auto ptr = std::move(items_[idx]);
  items_.erase(items_.begin() + long(idx));
  // C8：收藏项沉到收藏区末尾（保持分区不变式），非收藏项沉到整体末尾
  const size_t target = favorite ? Boundary() : items_.size();
  items_.insert(items_.begin() + long(target), std::move(ptr));
}

void Store::PasteDone(const ClipItem* item, bool moveToEnd) {
  ClipItem* mut = Mutable(item);
  if (!mut) return;
  mut->isPasted = true;
  if (moveToEnd) MoveToBack(mut);
  storage_.Save(items_);
  RebuildDisplay();
  Emit(StoreEventKind::FlagsChanged, mut);
  if (pasteMode_ == PasteMode::Quick) {             // 连续空格连贴：自动跳到下一条未粘贴
    for (const ClipItem* d : display_) {
      if (!d->isPasted) { Select(d); return; }
    }
  }
}

void Store::SaveToDisk() { storage_.Save(items_); }

size_t Store::ClearAll() {
  // 2026-10-04 用户决议（§0.1 C12）：收藏条目永久保存，「清除」只清非收藏区。
  // 想彻底删掉某条收藏，先取消收藏（它回到普通视图）再清除。
  const size_t fav = Boundary();
  const size_t removed = items_.size() - fav;
  if (selected_ && !selected_->isFavorite) selected_ = nullptr;   // 先摘指针，erase 后不可再解引用
  items_.erase(items_.begin() + long(fav), items_.end());
  storage_.Save(items_);
  RebuildDisplay();                                               // 幸存的收藏在【收藏】视图仍可见
  Emit(StoreEventKind::FullReplaced);
  return removed;
}

void Store::Reset() {
  for (auto& p : items_) p->isPasted = false;
  // C5：收藏区与非收藏区各自按时间降序；stable_sort → 同一时间戳保持原相对顺序
  const size_t fav = Boundary();
  std::stable_sort(items_.begin(), items_.begin() + long(fav), NewerFirst);
  std::stable_sort(items_.begin() + long(fav), items_.end(), NewerFirst);
  storage_.Save(items_);
  RebuildDisplay();
  Emit(StoreEventKind::FullReplaced);
}

// 2026-10-06 用户决议：同值重复选择也当作一次"刷新"（不再提前返回）——点★暂留的条目
// 要靠用户主动刷新才移出，若点当前过滤项毫无反应，观感就是"点了没用"。
void Store::SetFilter(FilterType filter) {
  filter_ = filter;
  RebuildDisplay();
  Emit(StoreEventKind::FullReplaced);
}

void Store::ApplySearch(std::wstring keyword) {
  keyword_ = std::move(keyword);
  foldedKeyword_ = FoldKey(keyword_);
  RebuildDisplay();
  Emit(StoreEventKind::FullReplaced);
}

void Store::RebuildDisplay() {
  display_.clear();
  for (auto& p : items_) {
    const ClipItem& item = *p;
    // 2026-10-04 用户决议：收藏条目只在【收藏】视图出现，其余视图一律不显示
    // （原 FR-08"置顶分组显示"作废；数组的 [收藏区|非收藏区] 不变式仍保留，淘汰与持久化依赖它）
    if (filter_ != FilterType::Favorite && item.isFavorite) continue;
    switch (filter_) {
      case FilterType::Text:     if (item.type != ClipType::Text) continue; break;
      case FilterType::TableCell:if (item.type != ClipType::TableCell) continue; break;
      case FilterType::Favorite: if (!item.isFavorite) continue; break;
      case FilterType::All:      break;
    }
    // 来源标注不上屏，但仍是搜索命中字段（2026-10-04 用户决议："不显示，但仍参与搜索"）
    if (!foldedKeyword_.empty() &&
        !ContainsFolded(item.foldContent, foldedKeyword_) &&
        !ContainsFolded(item.foldLabel, foldedKeyword_))
      continue;
    display_.push_back(p.get());
  }
  // 原选中项若仍在视图内则保持选中（契约 §4.6 原地同步语义）
  if (selected_ && std::find(display_.begin(), display_.end(), selected_) == display_.end()) {
    selected_ = nullptr;
  }
  AnchorQuickSelection();
}

// 快速模式：选中位钉在显示区第一行（用户 2026-10-05 决议——复制后直接按空格就贴最新那条，
// 不必每次先点一下条目）。普通模式一律不动：那边双击即粘贴即收起，钉它只会让灰显标记乱跳。
// 与 FR-10 原有的"粘完跳下一条未粘贴"不冲突：已粘贴的会沉底，第一行本就是下一条未粘贴。
void Store::AnchorQuickSelection() {
  if (pasteMode_ != PasteMode::Quick || display_.empty()) return;
  if (selected_ == display_.front()) return;
  Select(display_.front());
}

std::vector<const ClipItem*> Store::Collection() const {
  std::vector<const ClipItem*> out;
  out.reserve(items_.size());
  for (const auto& p : items_) out.push_back(p.get());
  return out;
}

void Store::Select(const ClipItem* item) {
  if (item && !Mutable(item)) return;               // 不属于本集合的指针一律忽略
  if (selected_ == item) return;
  selected_ = item;
  Emit(StoreEventKind::SelectionChanged, item);
}

std::optional<size_t> Store::IndexOfDisplay(const ClipItem* item) const {
  const auto it = std::find(display_.begin(), display_.end(), item);
  if (it == display_.end()) return std::nullopt;
  return size_t(std::distance(display_.begin(), it));
}

}  // namespace sc
