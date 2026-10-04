#include "ListRenderer.h"
#include "../core/ClipItem.h"
#include "../core/Text.h"
#include "../core/Time.h"
#include "../util/Log.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace sc {
namespace {

constexpr float kColGap = 4.f;
constexpr float kIndexColW = static_cast<float>(kIndexW);
constexpr float kTimeColW = static_cast<float>(kTimeW);
constexpr float kStarColW = static_cast<float>(kStarW);
constexpr float kRowPadF = static_cast<float>(kRowPad);
constexpr float kRowGapF = static_cast<float>(kRowGap);
constexpr float kMaxContentH = static_cast<float>(kContentMaxH);   // = 单行高度上限
constexpr float kRadius = static_cast<float>(kCardRadius);
constexpr float kPadF = static_cast<float>(kPad);
// 单行窗口内不可能容纳超过该字符数（13px 中文一行 ≈ 250px 行宽 → 约 19 字，
// 拉丁最窄 ≈ 6px/字 → 约 40 字），二分上界取 400 已有足够余量，又避免超长条目测到 2000 字。
constexpr size_t kTrimSearchChars = 400;

D2D1_RECT_F Rect(float l, float t, float w, float h) {
  return D2D1_RECT_F{l, t, l + w, t + h};
}

// 一次排版取高度。DWRITE_TEXT_METRICS 的 height 在 MSVC 与 mingw 两版头里
// 偏移都是 16，且两版结构同为 40 字节，可安全读取（其余字段两版错位，不使用）。
float LayoutHeight(IDWriteFactory* factory, IDWriteTextFormat* format,
                   const std::wstring& text, float width) {
  if (!factory || !format || text.empty()) return 0.f;
  Com<IDWriteTextLayout> layout;
  if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
                                       width, 0.f, layout.put())) ||
      !layout) {
    return 0.f;
  }
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return m.height;
}

// 截到"高度不超过 limit（= 单行）"的最长前缀并加省略号。
// 不用 DWRITE_TRIMMING：本机实测（Win11 26100 的 dwrite.dll）LINE_BY_LINE 一律
// E_INVALIDARG，CHARACTER/WORD 无视 count 固定塌成单行 —— 都拿不到可控行数（§0.1 C9）。
std::wstring TrimToLines(IDWriteFactory* factory, IDWriteTextFormat* format,
                         const std::wstring& text, float width, float limit) {
  size_t hi = std::min(text.size(), static_cast<size_t>(kTrimSearchChars));
  if (hi == 0) return text;
  size_t lo = 0;
  while (lo < hi) {
    const size_t mid = (lo + hi + 1) / 2;
    std::wstring candidate = text.substr(0, mid);
    if ((candidate.back() & 0xFC00) == 0xD800) candidate.pop_back();   // 不切断代理对
    candidate.push_back(L'…');
    if (LayoutHeight(factory, format, candidate, width) <= limit) lo = mid;
    else hi = mid - 1;
  }
  std::wstring out = text.substr(0, lo);
  if (!out.empty() && (out.back() & 0xFC00) == 0xD800) out.pop_back();
  out.push_back(L'…');
  return out;
}

// 1px 线/框对齐像素中心，避免半像素发虚（技术方案 §6.6）
D2D1_RECT_F Sharpen(D2D1_RECT_F r) {
  r.left = std::floor(r.left) + 0.5f;
  r.top = std::floor(r.top) + 0.5f;
  r.right = std::floor(r.right) + 0.5f;
  r.bottom = std::floor(r.bottom) + 0.5f;
  return r;
}

IDWriteTextFormat* MakeTimeFormat(IDWriteFactory* factory) {
  IDWriteTextFormat* fmt = nullptr;
  if (FAILED(factory->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                       kFontMeta, L"zh-CN", &fmt)) ||
      !fmt) {
    return nullptr;
  }
  fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
  fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  return fmt;
}

}  // namespace

ListRenderer::ListRenderer(IDWriteFactory* factory) : factory_(factory) {
  if (factory_) fmtTime_.attach(MakeTimeFormat(factory));
}

const ListRenderer::LayoutCache& ListRenderer::Measure(const ClipItem& item, float bodyWidth,
                                                       const Theme& theme) {
  auto it = cache_.find(&item);
  if (it != cache_.end()) return it->second;

  if (cache_.size() >= static_cast<size_t>(kMaxLayoutCache)) cache_.clear();   // 正常不触发

  LayoutCache entry;
  std::wstring preview = PreviewText(item.content);
  float h = LayoutHeight(factory_, theme.Body(), preview, bodyWidth);
  if (h > kMaxContentH) {
    // 单行放不下：截成"一行 + …"，完整内容交给悬浮气泡（设计方案 §7）
    preview = TrimToLines(factory_, theme.Body(), preview, bodyWidth, kMaxContentH);
    h = LayoutHeight(factory_, theme.Body(), preview, bodyWidth);
    entry.truncated = true;
  }
  entry.previewH = std::min(h, kMaxContentH);

  IDWriteTextLayout* raw = nullptr;
  if (factory_ && theme.Body() &&
      SUCCEEDED(factory_->CreateTextLayout(preview.c_str(), static_cast<UINT32>(preview.size()),
                                           theme.Body(), bodyWidth, kMaxContentH, &raw)) &&
      raw) {
    // 单行：不换行 + 布局框高等于行高，配合绘制期的 CLIP 兜住任何越列溢出
    raw->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    entry.preview.attach(raw);
  }
  return cache_.emplace(&item, std::move(entry)).first->second;
}

const ListRenderer::LayoutCache* ListRenderer::Cached(const ClipItem& item) const {
  const auto it = cache_.find(&item);
  return it == cache_.end() ? nullptr : &it->second;
}

void ListRenderer::Rebuild(const Store& store, const Theme& theme, float contentWidth) {
  // 宽度变了就必须重测：缓存里的截断点是按旧行宽算的，沿用会让单行条目要么提前截断、
  // 要么超出列宽（命中区/时间列被压字）
  if (std::fabs(contentWidth - contentWidth_) > 0.5f) cache_.clear();
  contentWidth_ = contentWidth;
  rows_.clear();
  contentH_ = 0.f;

  const float bodyX = kPadF + kRowPadF + kIndexColW + kColGap;
  const float starLeft = kPadF + contentWidth - kRowPadF - kStarColW;
  const float timeLeft = starLeft - kColGap - kTimeColW;
  const float bodyWidth = std::max(40.f, timeLeft - kColGap - bodyX);

  std::unordered_set<const ClipItem*> alive;
  float y = kRowPadF;
  for (const ClipItem* item : store.Display()) {
    alive.insert(item);
    const LayoutCache& cache = Measure(*item, bodyWidth, theme);
    const float contentH = std::min(cache.previewH, kMaxContentH);
    // 卡片内上下留白对称；行距（pitch）另加 kRowGap。
    // 来源标注不再上屏（2026-10-04 用户决议），行高一律只由主内容决定。
    const float cardH = kRowPadF + contentH + kRowPadF;
    const float height = cardH + kRowGapF;

    Row row;
    row.item = item;
    row.truncated = cache.truncated;
    row.top = y;
    row.height = height;
    row.card = Rect(kPadF, y, contentWidth, cardH);
    // 星标独占最右一列（2026-10-04 用户指定）：整列都是命中区，字形在列内垂直居中
    row.star = Rect(starLeft, y, kStarColW, cardH);
    row.body = Rect(bodyX, y + kRowPadF, timeLeft - kColGap - bodyX, contentH);
    rows_.push_back(row);
    y += height;
  }
  contentH_ = y;

  // 已消失条目的缓存必须删除：指针值可能被新条目复用
  for (auto it = cache_.begin(); it != cache_.end();) {
    if (!alive.count(it->first)) it = cache_.erase(it);
    else ++it;
  }
}

void ListRenderer::Draw(ID2D1RenderTarget* rt, const Theme& theme, const D2D1_RECT_F& viewport,
                        float scrollY, const ClipItem* selected, const ClipItem* hover) {
  if (!rt || !fmtTime_) return;
  const float viewH = viewport.bottom - viewport.top;

  rt->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
  D2D1_MATRIX_3X2_F old;
  rt->GetTransform(&old);
  // 行内坐标是"内容相对"，此处一次性平移到位：视口顶端 + 反向滚动量
  rt->SetTransform(D2D1::Matrix3x2F::Translation(0.f, viewport.top - scrollY));
  rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& row = rows_[i];
    if (row.top + row.height < scrollY || row.top > scrollY + viewH) continue;   // 视口外跳过
    const ClipItem* item = row.item;
    const LayoutCache* cache = Cached(*item);

    const D2D1_RECT_F card = Sharpen(row.card);
    rt->FillRoundedRectangle(D2D1::RoundedRect(card, kRadius, kRadius),
                             item->isFavorite ? theme.FavoriteBg() : theme.CardBg());
    if (hover && hover == item) {
      rt->FillRoundedRectangle(D2D1::RoundedRect(card, kRadius, kRadius), theme.HoverBg());
    }
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(card, kRadius, kRadius),
                             (selected && selected == item) ? theme.SelectedStroke()
                                                            : theme.CardStroke(),
                             1.f);

    ID2D1SolidColorBrush* ink = item->isPasted ? theme.InkPasted() : theme.Ink();
    ID2D1SolidColorBrush* meta = item->isPasted ? theme.MutedPasted() : theme.Muted();

    const std::wstring index = std::to_wstring(i + 1);            // FR-17：显示位置 +1
    rt->DrawText(index.c_str(), static_cast<UINT32>(index.size()), theme.Meta(),
                 Rect(row.card.left + kRowPadF, row.body.top, kIndexColW, 18.f), meta,
                 D2D1_DRAW_TEXT_OPTIONS_NONE);

    if (cache && cache->preview) {
      // CLIP：布局框宽 = 正文列宽，即使测量有偏差也不会压到时间列上
      rt->DrawTextLayout(D2D1_POINT_2F{row.body.left, row.body.top}, cache->preview.get(), ink,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    const std::wstring time = FormatHms(item->createdUtc);
    rt->DrawText(time.c_str(), static_cast<UINT32>(time.size()), fmtTime_.get(),
                 Rect(row.star.left - kColGap - kTimeColW, row.body.top, kTimeColW, 18.f), meta,
                 D2D1_DRAW_TEXT_OPTIONS_NONE);

    const wchar_t star = item->isFavorite ? L'\u2605' : L'\u2606';
    const float starTop = row.star.top + (row.star.bottom - row.star.top - kStarColW) * 0.5f;
    rt->DrawText(&star, 1, theme.Star(), Rect(row.star.left, starTop, kStarColW, kStarColW),
                 item->isFavorite ? theme.StarOn() : meta, D2D1_DRAW_TEXT_OPTIONS_NONE);
  }

  rt->SetTransform(old);
  rt->PopAxisAlignedClip();
}

const Row* ListRenderer::FindRow(const ClipItem* item) const {
  for (const Row& row : rows_) {
    if (row.item == item) return &row;
  }
  return nullptr;
}

}  // namespace sc
