#pragma once
#include "ClipItem.h"
#include "IStoreStorage.h"
#include "TableParser.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace sc {

enum class FilterType { All = 0, Text = 1, TableCell = 2, Favorite = 3 };
enum class PasteMode { Normal = 0, Quick = 1 };

// 轻量 observer（替代 [ObservableProperty]）：UI 按 kind 决定失效范围。
enum class StoreEventKind { FullReplaced, ItemsInserted, ItemRemoved, FlagsChanged, SelectionChanged };
struct StoreEvent {
  StoreEventKind kind = StoreEventKind::FullReplaced;
  const ClipItem* primary = nullptr;      // 可为空（整表变化）
};

// 列表状态机：唯一业务状态持有者。顺序不变式见技术方案 §3.3
//   items_ = [收藏区(最新在前) | 非收藏区(最新在前)]
class Store {
 public:
  explicit Store(IStoreStorage& storage) : storage_(storage) {}

  void LoadFromDisk();
  void AddFromClipboard(std::wstring_view text);
  void ToggleFavorite(const ClipItem* item);
  void PasteDone(const ClipItem* item, bool moveToEnd);
  // 清除：只删非收藏区，收藏条目永久保留（§0.1 C12）；返回实际删除条数供状态栏提示
  size_t ClearAll();
  void Reset();
  void SetFilter(FilterType filter);
  void ApplySearch(std::wstring keyword);      // UI 侧已做 300ms 防抖
  void Select(const ClipItem* item);
  // C14（2026-10-05 用户决议）：快速模式把选中位钉到显示区第一行（复制/绑定/过滤后无需先点条目，空格即贴最新那条）。
  // 普通模式调用它什么都不做。列表重排末尾会自动调一次；绑定完成那一路由 AppContext 显式调用。
  void AnchorQuickSelection();
  // C15 填表接力（v2.3.0 起为无开关接力，2026-10-05 用户决议）：接力贴的是**所见即所贴**——
  // 屏幕上的实时第一行，过滤态/搜索态都按当前显示区算，不要求停在【全部】视图或清空搜索框。
  // 粘过的会沉底（C8），下一条自动上位（C14），所以不需要额外指针。
  // 第一行已是灰条 = 没有未粘贴的条目，返回 nullptr 让调用方只提示、绝不回头重贴。
  const ClipItem* RelayNext() const {
    if (display_.empty()) return nullptr;
    const ClipItem* front = display_.front();
    return front->isPasted ? nullptr : front;
  }
  void SaveToDisk();                           // 退出链用（变更时已自动保存）
  void SetCopyMode(CopyMode mode) { copyMode_ = mode; }
  void SetPasteMode(PasteMode mode) { pasteMode_ = mode; }

  CopyMode copyMode() const { return copyMode_; }
  PasteMode pasteMode() const { return pasteMode_; }
  FilterType filter() const { return filter_; }
  const std::wstring& keyword() const { return keyword_; }
  bool Corrupted() const;

  // 显示视图。注意：点★（ToggleFavorite）只改集合顺序、不重算它——被收藏/取消收藏的那条
  // 会原位保留到下一次列表刷新为止，故此处可能短暂包含与当前 filter_ 不符的条目。
  const std::vector<const ClipItem*>& Display() const { return display_; }
  // 集合原始顺序 [收藏区 | 非收藏区]，不受过滤/搜索影响：分区不变式与持久化顺序的观测口
  std::vector<const ClipItem*> Collection() const;
  const ClipItem* Selected() const { return selected_; }
  size_t TotalCount() const { return items_.size(); }
  std::optional<size_t> IndexOfDisplay(const ClipItem* item) const;   // FR-17 序号 = index+1

  std::function<void(const StoreEvent&)> onEvent;

 private:
  static constexpr size_t npos = size_t(-1);

  size_t Boundary() const;                     // 第一个非收藏项下标 = 收藏数量
  void EnforceLimit();                         // C7 末位淘汰
  void ApplyOrder();                           // stable_partition：收藏在前
  void RebuildDisplay();
  void Emit(StoreEventKind kind, const ClipItem* primary = nullptr);
  ClipItem* Mutable(const ClipItem*);          // 集合内可变异句柄（Store 是唯一变异入口）
  size_t IndexOf(const ClipItem* item) const;
  void RemoveAt(size_t idx);
  // 去重：同 hash 先移除旧项并带走其 isFavorite（§4.1）
  bool TakeFavoriteIfDuplicate(const std::wstring& hash);
  void MoveToBack(const ClipItem* item);       // C8：收藏项沉到收藏区末尾

  std::vector<std::unique_ptr<ClipItem>> items_;
  std::vector<const ClipItem*> display_;
  std::wstring keyword_;
  std::wstring foldedKeyword_;
  const ClipItem* selected_ = nullptr;
  FilterType filter_ = FilterType::All;
  CopyMode copyMode_ = CopyMode::Normal;
  PasteMode pasteMode_ = PasteMode::Normal;
  IStoreStorage& storage_;
};

}  // namespace sc
