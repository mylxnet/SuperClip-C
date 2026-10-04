#pragma once
#include "../core/Store.h"
#include "../native/ComPtr.h"
#include "Theme.h"
#include <d2d1.h>
#include <dwrite.h>
#include <unordered_map>
#include <vector>

namespace sc {

// 一行的预存几何：数据变化时重算，每帧不重算（技术方案 §6.2）
struct Row {
  const ClipItem* item = nullptr;
  float top = 0.f;              // 内容坐标系（未滚动）
  float height = 0.f;
  D2D1_RECT_F card{};           // 卡片外框
  D2D1_RECT_F body{};           // 命中区：整行（除星标）
  D2D1_RECT_F star{};           // 命中区：收藏切换（独占最右一列）
  bool truncated = false;       // 主内容被压成单行并加 …：仅这些行需要悬浮全文
};

// 行测量 + TextLayout 缓存 + 绘制 + 滚动（技术方案 §6.4/§6.6）
class ListRenderer {
 public:
  explicit ListRenderer(IDWriteFactory* factory);

  // 宽度变化、Store 结构变化时调用（测量与行几何在此一次性算好）
  void Rebuild(const Store& store, const Theme& theme, float contentWidth);
  float ContentHeight() const { return contentH_; }
  const std::vector<Row>& rows() const { return rows_; }
  const Row* FindRow(const ClipItem* item) const;

  void Draw(ID2D1RenderTarget* rt, const Theme& theme, const D2D1_RECT_F& viewport,
            float scrollY, const ClipItem* selected, const ClipItem* hover);

 private:
  struct LayoutCache {
    Com<IDWriteTextLayout> preview;
    float previewH = 0.f;
    bool truncated = false;
  };
  const LayoutCache& Measure(const ClipItem& item, float bodyWidth, const Theme& theme);
  const LayoutCache* Cached(const ClipItem& item) const;

  IDWriteFactory* factory_ = nullptr;
  Com<IDWriteTextFormat> fmtTime_;        // 右对齐时间列
  std::unordered_map<const ClipItem*, LayoutCache> cache_;
  std::vector<Row> rows_;
  float contentWidth_ = 0.f;
  float contentH_ = 0.f;
};

}  // namespace sc
