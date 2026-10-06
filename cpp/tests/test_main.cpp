// SuperClip C++ · M1 逻辑层单测（技术方案 §9.2/§9.3/§9.4/§9.6）
// 自建极简断言框架：原计划用 Catch2，但当前环境离线取不到包 → 内置等价物。
// 运行前提：Windows（BCrypt / 临时目录）。交叉编译产物需在 Windows 或 Wine 下执行。
#include "../src/core/ClipItem.h"
#include "../src/core/Json.h"
#include "../src/core/Sha256.h"
#include "../src/core/Store.h"
#include "../src/core/TableParser.h"
#include "../src/core/Text.h"
#include "../src/core/Time.h"
#include "../src/services/StorageService.h"
#include "../src/core/Settings.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;
int g_cases = 0;                      // 由 Run 自增：用例数只有一处权威，别在打印里再抄一个数字
const char* g_current = "";

// 系统时钟粒度约 15.6ms：需要可区分的 createdUtc 时按此间隔隔开两次入列
constexpr DWORD kClockTickMs = 40;

std::string Describe(const std::wstring& s) { return sc::ToUtf8(s); }
std::string Describe(const std::string& s) { return s; }
std::string Describe(const wchar_t* s) { return sc::ToUtf8(s ? s : L""); }
std::string Describe(bool v) { return v ? "true" : "false"; }

// 数值与指针兜底（非模板重载优先，故字符串/bool 走上面）
template <typename T>
std::string Describe(const T& v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

void Fail(const char* file, int line, const std::string& what) {
  ++g_failures;
  std::printf("  FAIL [%s] %s:%d  %s\n", g_current, file, line, what.c_str());
}

template <typename A, typename B>
void CheckEq(const A& actual, const B& expected, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!(actual == expected)) {
    Fail(file, line, std::string(expr) + " → 实际 " + Describe(actual) + "，期望 " + Describe(expected));
  }
}

#define CHECK(cond) \
  do { ++g_checks; if (!(cond)) Fail(__FILE__, __LINE__, "期望成立: " #cond); } while (0)
#define CHECK_EQ(actual, expected) \
  CheckEq((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)

using TestFn = void (*)();

void Run(const char* name, TestFn fn) {
  g_current = name;
  ++g_cases;
  const int before = g_failures;
  fn();
  std::printf("%s %s\n", g_failures == before ? "  ok  " : "  BAD ", name);
}

// 每个用例独立临时目录，避免相互污染
class TempStore {
 public:
  explicit TempStore(const char* tag) {
    wchar_t base[MAX_PATH]{};
    GetTempPathW(MAX_PATH, base);
    // 只用时钟+线程号会在相邻用例间撞名（同一毫秒内建两个同名目录，先建的那个析构时
    // remove_all 会把后一个刚写的文件连目录一起删掉 → 用例随机红）。加进程内自增序号。
    static unsigned seq = 0;
    SYSTEMTIME st{};
    GetSystemTime(&st);
    wchar_t sub[160];
    swprintf(sub, 160, L"SuperClipTest_%hs_%lu_%lu_%lu_%lu", tag,
             static_cast<unsigned long>(st.wSecond), static_cast<unsigned long>(st.wMilliseconds),
             static_cast<unsigned long>(GetCurrentThreadId()), ++seq);
    dir_ = std::wstring(base) + sub;
    storage_ = std::make_unique<sc::StorageService>(dir_ + L"\\history.json");
    storage_->EnsureDir();
  }
  ~TempStore() {
    storage_.reset();
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  sc::StorageService& storage() { return *storage_; }
  const std::wstring& dir() const { return dir_; }

 private:
  std::wstring dir_;
  std::unique_ptr<sc::StorageService> storage_;
};

// ============ §9.2 TableParser（13 例）============

void T01_SingleRowTwoCols() {
  CHECK(sc::IsTable(L"a\tb", sc::CopyMode::Normal));
  const auto cells = sc::Parse(L"a\tb");
  CHECK_EQ(cells.size(), 2u);
  CHECK_EQ(cells[0].content, L"a"); CHECK_EQ(cells[0].row, 1); CHECK_EQ(cells[0].col, 1);
  CHECK_EQ(cells[1].content, L"b"); CHECK_EQ(cells[1].row, 1); CHECK_EQ(cells[1].col, 2);
}

void T02_TwoRows() {
  const auto cells = sc::Parse(L"a\tb\r\nc\td");
  CHECK_EQ(cells.size(), 4u);
  CHECK_EQ(cells[0].row, 1); CHECK_EQ(cells[0].col, 1);
  CHECK_EQ(cells[1].row, 1); CHECK_EQ(cells[1].col, 2);
  CHECK_EQ(cells[2].row, 2); CHECK_EQ(cells[2].col, 1);
  CHECK_EQ(cells[3].row, 2); CHECK_EQ(cells[3].col, 2);
}

void T03_EmptyCellColKept() {
  const auto cells = sc::Parse(L"a\t\tc");
  CHECK_EQ(cells.size(), 2u);
  CHECK_EQ(cells[0].col, 1);
  CHECK_EQ(cells[1].col, 3);                       // 空单元格跳过但列号不前移
  CHECK_EQ(cells[1].content, L"c");
}

void T04_BlankWrappedTextNotTable() {
  CHECK(!sc::IsTable(L"\n\na\n\n", sc::CopyMode::Normal));
}

void T05_TrailingNewlineTrimmed() {
  CHECK_EQ(sc::Parse(L"a\tb\n").size(), 2u);
}

void T06_BareCarriageReturnAsLineBreak() {
  const auto cells = sc::Parse(L"a\tb\rc\td");
  CHECK_EQ(cells.size(), 4u);
  CHECK_EQ(cells[2].row, 2);
}

void T07_ThreeColumns() {
  const auto cells = sc::Parse(L"1\t2\t3");
  CHECK_EQ(cells.size(), 3u);
  CHECK_EQ(cells[0].col, 1); CHECK_EQ(cells[1].col, 2); CHECK_EQ(cells[2].col, 3);
}

void T08_SingleColumnModeSplitsLines() {
  CHECK(sc::IsTable(L"行1\n行2\n行3", sc::CopyMode::TableSingleColumn));
  const auto cells = sc::Parse(L"行1\n行2\n行3");
  CHECK_EQ(cells.size(), 3u);
  CHECK_EQ(cells[0].row, 1); CHECK_EQ(cells[0].col, 1);
  CHECK_EQ(cells[1].content, L"行2"); CHECK_EQ(cells[1].row, 2); CHECK_EQ(cells[1].col, 1);
  CHECK_EQ(cells[2].row, 3);
}

void T09_NormalModeKeepsMultilineWhole() {
  CHECK(!sc::IsTable(L"行1\n行2\n行3", sc::CopyMode::Normal));
}

void T10_OnlyTabsYieldNoCells() {
  CHECK(sc::IsTable(L"\t\t", sc::CopyMode::Normal));
  CHECK_EQ(sc::Parse(L"\t\t").size(), 0u);
}

void T11_BlankRowRowIndexKept() {
  const auto cells = sc::Parse(L"a\tb\n\nc\td");
  CHECK_EQ(cells.size(), 4u);
  CHECK_EQ(cells[0].row, 1);
  CHECK_EQ(cells[2].row, 3);                       // 空行跳过但行号不前移
}

void T12_PlainTextNotTable() {
  CHECK(!sc::IsTable(L"x", sc::CopyMode::Normal));
  CHECK(!sc::IsTable(L"x", sc::CopyMode::TableSingleColumn));
}

void T13_CellContentNotTrimmed() {
  const auto cells = sc::Parse(L" a \t b ");
  CHECK_EQ(cells.size(), 2u);
  CHECK_EQ(cells[0].content, L" a ");
  CHECK_EQ(cells[1].content, L" b ");
}

// ============ §9.3 哈希与来源标签（8 例）============

void H01_KnownVector() {
  CHECK_EQ(sc::HexSha256Utf8(L"abc"),
           L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void H02_EmptyContentNoHash() {
  CHECK_EQ(sc::HexSha256Utf8(L""), L"");
}

void H03_ChineseStableLowerHex() {
  const std::wstring a = sc::HexSha256Utf8(L"中文测试");
  CHECK_EQ(a.size(), 64u);
  CHECK(a == sc::HexSha256Utf8(L"中文测试"));
  bool allLowerHex = true;
  for (const wchar_t c : a) allLowerHex = allLowerHex && ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'));
  CHECK(allLowerHex);
}

void H04_DifferentContentDifferentHash() {
  CHECK(sc::HexSha256Utf8(L"abc") != sc::HexSha256Utf8(L"abd"));
}

void H05_LongText() {
  const std::wstring big(512u * 1024u, L'x');      // 1 MB 个字符
  const ULONGLONG begin = GetTickCount64();
  const std::wstring hash = sc::HexSha256Utf8(big);
  const ULONGLONG cost = GetTickCount64() - begin;
  CHECK_EQ(hash.size(), 64u);
  CHECK(cost < 500);                               // 阈值放宽（慢盘/调试附加器环境）
}

void L06_TextLabelEmpty() {
  auto item = sc::MakeItem(L"hello", sc::ClipType::Text);
  CHECK_EQ(item->SourceLabel(), L"");
}

void L07_TableCellLabel() {
  auto item = sc::MakeItem(L"v", sc::ClipType::TableCell, 2, 3);
  CHECK_EQ(item->SourceLabel(), L"来自表格：第 2 行 第 3 列");
}

void L08_TableCellWithoutRowCol() {
  auto item = sc::MakeItem(L"v", sc::ClipType::TableCell, 0, 0);
  CHECK_EQ(item->SourceLabel(), L"");
}

// ============ §9.4 Store 不变式（19 例）============

void S01_DedupMigratesFavorite() {
  TempStore tmp("s01");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"abc");
  CHECK_EQ(store.TotalCount(), 1u);
  const sc::ClipItem* first = store.Display()[0];
  store.ToggleFavorite(first);
  store.AddFromClipboard(L"abc");
  CHECK_EQ(store.TotalCount(), 1u);
  const sc::ClipItem* only = store.Collection()[0];
  CHECK_EQ(only->isFavorite, true);                        // 收藏态迁移
  CHECK(only != first);                                    // 旧条移除、新条重建（时间戳更新）
  CHECK(store.Display().empty());   // 2026-10-04 决议：收藏条目不进普通视图
}

void S02_NewItemLandsAfterFavorites() {
  TempStore tmp("s02");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"a");
  store.AddFromClipboard(L"b");
  store.AddFromClipboard(L"c");                            // 集合 [c b a]
  // 2026-10-06 决议：点★当场不动显示视图，所以必须先取指针，不能连着用 Display()[0]
  const sc::ClipItem* c = store.Display()[0];
  const sc::ClipItem* b = store.Display()[1];
  store.ToggleFavorite(c);                                 // c 收藏
  store.ToggleFavorite(b);                                 // b 收藏 → 收藏区 [c b]
  store.AddFromClipboard(L"new");
  CHECK_EQ(store.TotalCount(), 4u);
  const auto col = store.Collection();
  CHECK_EQ(col[0]->content, L"c");
  CHECK_EQ(col[1]->content, L"b");
  CHECK_EQ(col[2]->content, L"new");                       // 插入位 == 收藏数量 2
  CHECK_EQ(col[3]->content, L"a");
  CHECK_EQ(store.Display().size(), 2u);                    // 普通视图只剩非收藏项
  CHECK_EQ(store.Display()[0]->content, L"new");
  CHECK_EQ(store.Display()[1]->content, L"a");
}

void S03_EvictOldestNotNewest() {                          // C7 回归：锁死"不删最新"
  TempStore tmp("s03");
  sc::Store store(tmp.storage());
  for (int i = 0; i <= sc::kMaxItems; ++i) {
    store.AddFromClipboard(L"item" + std::to_wstring(i));  // 共 501 条，item500 最新
  }
  CHECK_EQ(store.TotalCount(), size_t(sc::kMaxItems));
  CHECK_EQ(store.Display().front()->content, L"item500");
  CHECK_EQ(store.Display().back()->content, L"item1");     // 淘汰的是末位 item0
  bool item0Survives = false;
  for (const auto* item : store.Display()) if (item->content == L"item0") item0Survives = true;
  CHECK(!item0Survives);
}

void S04_FavoritesNeverEvicted() {
  TempStore tmp("s04");
  sc::Store store(tmp.storage());
  for (int i = 0; i < 300; ++i) store.AddFromClipboard(L"f" + std::to_wstring(i));
  CHECK_EQ(store.TotalCount(), 300u);
  const std::vector<const sc::ClipItem*> snapshot(store.Collection());   // 先快照：重排会重建视图
  for (const auto* item : snapshot) store.ToggleFavorite(item);       // 300 条全部收藏
  for (int i = 0; i < 250; ++i) store.AddFromClipboard(L"g" + std::to_wstring(i));
  CHECK_EQ(store.TotalCount(), 550u);                                 // 非收藏 250 未超限，收藏全保住
  size_t favoriteCount = 0;
  for (const auto* item : store.Collection()) if (item->isFavorite) ++favoriteCount;
  CHECK_EQ(favoriteCount, 300u);
  CHECK(store.Display().empty() == false);              // 普通视图仍有 250 条非收藏
  store.SetFilter(sc::FilterType::Favorite);
  CHECK_EQ(store.Display().size(), 300u);               // 收藏视图才看得见收藏
  // 再复制到超限：非收藏 251 → 仍不超 500，收藏一条不掉
  store.AddFromClipboard(L"extra");
  CHECK_EQ(store.TotalCount(), 551u);
}

void S05_StablePartitionKeepsRelativeOrder() {
  TempStore tmp("s05");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"a");
  store.AddFromClipboard(L"b");
  store.AddFromClipboard(L"c");
  store.AddFromClipboard(L"d");                            // 集合 d c b a
  const sc::ClipItem* c = store.Display()[1];
  const sc::ClipItem* b = store.Display()[2];
  store.ToggleFavorite(c);                                 // c 收藏 → 集合 c d b a
  store.ToggleFavorite(b);                                 // b 收藏 → 收藏区保持相对序 [c b]
  const auto col = store.Collection();
  CHECK_EQ(col[0]->content, L"c");                         // 收藏区在前，且 c 仍在 b 之前
  CHECK_EQ(col[0]->isFavorite, true);
  CHECK_EQ(col[1]->content, L"b");
  CHECK_EQ(col[1]->isFavorite, true);
  CHECK_EQ(col[2]->content, L"d");                         // 非收藏区相对序不变（d 比 a 新）
  CHECK_EQ(col[3]->content, L"a");
  store.ApplySearch(L"");                                  // 点★不即时重算，这里主动刷新一次
  const auto& view = store.Display();                      // 普通视图只剩 d a
  CHECK_EQ(view.size(), 2u);
  CHECK_EQ(view[0]->content, L"d");
  CHECK_EQ(view[1]->content, L"a");
}

void S06_TableBlockOrder() {
  TempStore tmp("s06");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"a1\tb1\r\na2\tb2");
  const auto& d = store.Display();
  CHECK_EQ(d.size(), 4u);
  CHECK_EQ(d[0]->content, L"a1");                          // 整块插入，不倒序
  CHECK_EQ(d[1]->content, L"b1");
  CHECK_EQ(d[2]->content, L"a2");
  CHECK_EQ(d[3]->content, L"b2");
  CHECK_EQ(d[0]->sourceRow, 1); CHECK_EQ(d[0]->sourceCol, 1);
  CHECK_EQ(d[3]->sourceRow, 2); CHECK_EQ(d[3]->sourceCol, 2);
}

void S07_QuickPasteSinksNonFavorite() {
  TempStore tmp("s07");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"x");
  store.AddFromClipboard(L"y");                            // 视图 y x
  const sc::ClipItem* x = store.Display()[1];
  store.PasteDone(x, true);
  CHECK_EQ(x->isPasted, true);
  CHECK(store.Display().back() == x);
}

void S08_FavoriteSinksToFavoriteZoneEnd() {                // C8
  TempStore tmp("s08");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"a");
  store.AddFromClipboard(L"b");
  store.AddFromClipboard(L"c");                            // 集合 c b a
  store.ToggleFavorite(store.Display()[2]);                // a 收藏 → [a | c b]
  CHECK_EQ(store.Collection()[0]->content, L"a");
  CHECK_EQ(store.Display().size(), 3u);                    // 2026-10-06 决议：点★当场不移出
  store.ApplySearch(L"");                                  // 下一次列表刷新才按新状态移出
  CHECK_EQ(store.Display().size(), 2u);
  const sc::ClipItem* favorite = store.Collection()[0];
  store.ToggleFavorite(store.Collection()[1]);             // c 也收藏 → 收藏区 [a c]（稳定分区保序）
  store.PasteDone(favorite, true);
  const auto col = store.Collection();
  CHECK_EQ(col[0]->isFavorite, true);                      // 分区不变式仍成立
  CHECK_EQ(col[1]->isFavorite, true);
  CHECK_EQ(col[2]->isFavorite, false);
  CHECK(col[1] == favorite);                               // C8：收藏项沉到收藏区末位
  CHECK(col.back() != favorite);                           // 而不是沉到整个集合末位
  bool partitionOk = true;
  bool seenNonFavorite = false;
  for (const auto* item : col) {
    if (!item->isFavorite) seenNonFavorite = true;
    else if (seenNonFavorite) partitionOk = false;
  }
  CHECK(partitionOk);
}

void S09_OrderSurvivesRoundTrip() {                        // AC-3 逻辑层
  TempStore tmp("s09");
  std::vector<std::wstring> order;
  {
    sc::Store store(tmp.storage());
    store.AddFromClipboard(L"a");
    store.AddFromClipboard(L"b");
    store.ToggleFavorite(store.Display()[1]);              // a 收藏 → 集合 a b
    store.PasteDone(store.Collection()[1], true);          // b（非收藏）沉底 + 灰显
    for (const auto* item : store.Collection()) order.push_back(item->content);
  }
  CHECK_EQ(order.size(), 2u);
  sc::Store reloaded(tmp.storage());
  reloaded.LoadFromDisk();
  CHECK_EQ(reloaded.Collection().size(), order.size());
  for (size_t i = 0; i < order.size() && i < reloaded.Collection().size(); ++i) {
    CHECK_EQ(reloaded.Collection()[i]->content, order[i]);
  }
  CHECK_EQ(reloaded.Collection()[0]->isFavorite, true);
  CHECK_EQ(reloaded.Collection()[0]->isPasted, false);
  CHECK_EQ(reloaded.Collection()[1]->isPasted, true);
  CHECK_EQ(reloaded.Display().size(), 1u);                 // 普通视图只剩未收藏的 b
  CHECK_EQ(reloaded.Display()[0]->content, L"b");
}

void S10_ResetClearsGrayAndSortsByTimeDesc() {             // C5
  TempStore tmp("s10");
  sc::Store store(tmp.storage());
  // 系统时钟粒度约 15.6ms：同一 tick 内三条记录时间戳相等，stable_sort 会保持原序，
  // 期望的"时间降序"就变成随机结果。这里显式拉开时间戳，并在断言前自证前提。
  store.AddFromClipboard(L"a");                            // 最旧
  Sleep(kClockTickMs);
  store.AddFromClipboard(L"b");
  Sleep(kClockTickMs);
  store.AddFromClipboard(L"c");                            // 最新
  CHECK(CompareFileTime(&store.Display()[0]->createdUtc, &store.Display()[1]->createdUtc) > 0);
  CHECK(CompareFileTime(&store.Display()[1]->createdUtc, &store.Display()[2]->createdUtc) > 0);
  store.ToggleFavorite(store.Display()[2]);                // a 收藏 → [a | c b]
  store.PasteDone(store.Collection()[1], true);            // c 沉到非收藏区末位 → [a | b c]
  CHECK_EQ(store.Collection()[2]->content, L"c");
  store.Reset();
  bool allUnpast = true;
  for (const auto* item : store.Collection()) allUnpast = allUnpast && !item->isPasted;
  CHECK(allUnpast);
  CHECK_EQ(store.Collection()[0]->content, L"a");          // 收藏区不变
  CHECK_EQ(store.Collection()[1]->content, L"c");          // 非收藏区按时间降序：c 比 b 新
  CHECK_EQ(store.Collection()[2]->content, L"b");
  CHECK_EQ(store.Display().size(), 2u);                    // a 是收藏，不进普通视图
}

void S11_SearchFoldsFullWidthAndCase() {
  TempStore tmp("s11");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"价格４００元");
  store.AddFromClipboard(L"ABC def");
  store.ApplySearch(L"400");
  CHECK_EQ(store.Display().size(), 1u);
  CHECK_EQ(store.Display()[0]->content, L"价格４００元");   // 全角折叠命中
  store.ApplySearch(L"abc");
  CHECK_EQ(store.Display().size(), 1u);
  CHECK_EQ(store.Display()[0]->content, L"ABC def");       // 大小写不敏感
  store.ApplySearch(L"");
  CHECK_EQ(store.Display().size(), 2u);
}

void S12_FilterAndSearchCombined() {
  TempStore tmp("s12");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"plain");
  store.AddFromClipboard(L"cell1\tcell2");
  store.SetFilter(sc::FilterType::TableCell);
  store.ApplySearch(L"cell2");
  CHECK_EQ(store.Display().size(), 1u);
  CHECK_EQ(store.Display()[0]->content, L"cell2");
  const sc::ClipItem* visible = store.Display()[0];
  store.Select(visible);
  CHECK(store.Selected() == visible);
  store.ApplySearch(L"cell");                              // 选中项仍在视图内 → 保持选中
  CHECK(store.Selected() == visible);
  store.ApplySearch(L"nothing");                           // 选中项被过滤掉 → 复位
  CHECK(store.Selected() == nullptr);
  store.SetFilter(sc::FilterType::All);                    // 视图 ⊆ 全量且顺序一致
  store.ApplySearch(L"");
  size_t tableCells = 0;
  for (const auto* item : store.Display()) if (item->type == sc::ClipType::TableCell) ++tableCells;
  CHECK_EQ(tableCells, 2u);
}

void S13_FavoritesOnlyInFavoriteView() {                   // 2026-10-04 用户决议
  TempStore tmp("s13");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"plain");
  store.AddFromClipboard(L"c1\tc2");                       // 整块头插 → [c1 c2 plain]
  CHECK_EQ(store.TotalCount(), 3u);
  store.ToggleFavorite(store.Collection()[2]);             // plain 收藏 → [plain | c1 c2]
  store.ToggleFavorite(store.Collection()[1]);             // c1 收藏 → 收藏区 [plain c1]

  store.ApplySearch(L"");                                  // 点★不即时重算，这里主动刷新一次
  CHECK_EQ(store.Display().size(), 1u);
  CHECK_EQ(store.Display()[0]->content, L"c2");
  store.SetFilter(sc::FilterType::Text);
  CHECK(store.Display().empty());                          // 唯一的文本条目已收藏
  store.SetFilter(sc::FilterType::TableCell);
  CHECK_EQ(store.Display().size(), 1u);
  CHECK_EQ(store.Display()[0]->content, L"c2");
  store.SetFilter(sc::FilterType::Favorite);
  CHECK_EQ(store.Display().size(), 2u);                    // 【收藏】视图才看得到
  CHECK_EQ(store.Display()[0]->content, L"plain");
  CHECK_EQ(store.Display()[1]->content, L"c1");

  store.SetFilter(sc::FilterType::All);
  store.ApplySearch(L"plain");                             // 搜索命中收藏项也不得漏出
  CHECK(store.Display().empty());
  store.ApplySearch(L"");
}

// 2026-10-04 用户决议：来源标注不上屏，但仍参与搜索（渲染层已删，命中逻辑必须留着）
void S14_SourceLabelSearchableWhileHidden() {
  TempStore tmp("s14");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"a\tb\r\nc\td");                 // 4 个单元格，行号 1/2
  CHECK_EQ(store.TotalCount(), 4u);
  store.SetFilter(sc::FilterType::All);
  store.ApplySearch(L"来自表格");                          // 内容里不含这四个字
  CHECK_EQ(store.Display().size(), 4u);
  store.ApplySearch(L"第 2 行");
  CHECK_EQ(store.Display().size(), 2u);
  CHECK_EQ(store.Display()[0]->content, L"c");             // 整块插入不倒序（S06 同一口径）
  CHECK_EQ(store.Display()[1]->content, L"d");
  store.ApplySearch(L"第 3 行");                           // 不存在的行号 → 全滤掉
  CHECK(store.Display().empty());
  store.ApplySearch(L"");
  CHECK_EQ(store.Display().size(), 4u);
}

// 2026-10-04 用户决议 C12：收藏条目永久保存，「清除」只清非收藏区
void S15_ClearAllKeepsFavorites() {
  TempStore tmp("s15");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"keep-1");
  store.AddFromClipboard(L"keep-2");
  store.AddFromClipboard(L"drop-1");
  store.AddFromClipboard(L"drop-2");
  CHECK_EQ(store.TotalCount(), 4u);
  const std::vector<const sc::ClipItem*> snapshot(store.Collection());   // [drop-2 drop-1 keep-2 keep-1]
  store.ToggleFavorite(snapshot[3]);                                   // keep-1 → 收藏区
  store.ToggleFavorite(snapshot[2]);                                   // keep-2 → 收藏区 [keep-1 keep-2]
  store.Select(store.Collection()[0]);                                 // 选中一条非收藏
  CHECK(store.Selected() != nullptr);

  CHECK_EQ(store.ClearAll(), 2u);                                      // 返回删除条数
  CHECK_EQ(store.TotalCount(), 2u);
  CHECK(store.Selected() == nullptr);                                  // 被删的选中项复位
  CHECK_EQ(store.Collection()[0]->content, L"keep-1");                // 收藏区原序
  CHECK_EQ(store.Collection()[1]->content, L"keep-2");
  store.SetFilter(sc::FilterType::All);
  CHECK(store.Display().empty());                                      // 普通视图被清空
  store.SetFilter(sc::FilterType::Favorite);
  CHECK_EQ(store.Display().size(), 2u);                                // 【收藏】视图仍在

  sc::Store reloaded(tmp.storage());                                   // 落盘后重载：收藏没被写掉
  reloaded.LoadFromDisk();
  CHECK_EQ(reloaded.Collection().size(), 2u);
  CHECK_EQ(reloaded.Collection()[0]->content, L"keep-1");
  CHECK_EQ(reloaded.Collection()[0]->isFavorite, true);
  CHECK_EQ(reloaded.ClearAll(), 0u);                                   // 只剩收藏时再按清除 = 空操作
  CHECK_EQ(reloaded.Collection().size(), 2u);
}

void S16_QuickModeAnchorsSelectionToFirstRow() {          // 2026-10-05 用户决议：复制后直接空格即贴最新一条
  TempStore tmp("s16");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"old");
  CHECK(store.Selected() == nullptr);                                   // 普通模式：入列不钉选中位
  store.SetPasteMode(sc::PasteMode::Quick);
  store.AddFromClipboard(L"new");                                       // 入列即钉到第一行
  CHECK_EQ(store.Selected()->content, L"new");
  store.AddFromClipboard(L"newest");                                    // 再复制 → 选中位跟着走
  CHECK_EQ(store.Selected()->content, L"newest");
  store.Select(store.Display()[2]);                                     // 模拟单击别的行（视图 newest new old）
  CHECK_EQ(store.Selected()->content, L"old");
  store.PasteDone(store.Selected(), true);                              // 空格粘完 → 沉底 → 回到第一行
  CHECK_EQ(store.Selected()->content, L"newest");
  store.ApplySearch(L"old");                                            // 过滤后钉到匹配区第一行
  CHECK_EQ(store.Selected()->content, L"old");
  store.ApplySearch(L"");                                               // 清掉关键字 → 又钉回第一行
  CHECK_EQ(store.Selected()->content, L"newest");
  store.SetPasteMode(sc::PasteMode::Normal);                            // 切回普通：选中位不再被挪
  store.AddFromClipboard(L"third");
  CHECK_EQ(store.Selected()->content, L"newest");
}

// C15 无开关接力（v2.3.0，2026-10-05 用户决议）：接力永远取**屏幕上的实时第一行**，
// 过滤态与搜索态都按当前显示区算（所见即所贴），贴完给 nullptr、绝不回头重贴。
// 钩子那半段（WH_MOUSE_LL）在无输入总线的单测里跑不到，这里只锁推进规则本体。
void S17_RelayTakesFrontRowAndStopsWhenExhausted() {
  TempStore tmp("s17");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"a\tb\tc");                    // 一行三格 = 接力的三条
  CHECK_EQ(store.Display().size(), size_t(3));
  CHECK_EQ(store.RelayNext()->content, L"a");           // 第一行 = 行优先的第一格
  store.PasteDone(store.RelayNext(), true);              // 贴过即沉底，不需要额外指针
  CHECK_EQ(store.RelayNext()->content, L"b");
  store.PasteDone(store.RelayNext(), true);
  store.PasteDone(store.RelayNext(), true);
  CHECK(store.RelayNext() == nullptr);                   // 第一行是灰条 → 本次不动作

  // v2.2.0 的「必须停在【全部】视图 + 搜索框必须为空」两条准入已删除：
  // 接力不再要求先改视图，用户看见哪条就贴哪条。
  TempStore tmp2("s17b");
  sc::Store st2(tmp2.storage());
  st2.AddFromClipboard(L"apple");
  st2.AddFromClipboard(L"banana");                       // 新复制的在第一位
  st2.ApplySearch(L"banana");
  CHECK_EQ(st2.RelayNext()->content, L"banana");         // 搜索态：贴匹配区的第一行
  st2.ApplySearch(L"");
  st2.SetFilter(sc::FilterType::Text);
  CHECK_EQ(st2.RelayNext()->content, L"banana");         // 过滤态：同样取过滤后的第一行
}

// 2026-10-06 用户决议：点★当场不把该行抽走（避免"条目突然消失"的突兀感），只原地翻星标；
// 下一次列表刷新才按新状态移出【全部】等页面。收藏与取消收藏对称。
void S18_FavoriteStaysUntilNextRefresh() {
  TempStore tmp("s18");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"a");
  store.AddFromClipboard(L"b");
  store.AddFromClipboard(L"c");                            // 普通视图 [c b a]
  const sc::ClipItem* b = store.Display()[1];

  store.ToggleFavorite(b);
  CHECK_EQ(b->isFavorite, true);                           // 星标当场翻转（渲染层现读它）
  CHECK_EQ(store.Display().size(), 3u);                    // 但该行仍留在【全部】原位
  CHECK(store.Display()[1] == b);
  CHECK_EQ(store.Collection()[0]->content, L"b");          // 集合层已并进收藏区

  store.ApplySearch(L"");                                  // 任意一次列表刷新
  CHECK_EQ(store.Display().size(), 2u);                    // 这时才按新状态移出
  CHECK_EQ(store.Display()[0]->content, L"c");
  CHECK_EQ(store.Display()[1]->content, L"a");
  store.SetFilter(sc::FilterType::Favorite);
  CHECK_EQ(store.Display().size(), 1u);                    // 已落进【收藏】
  CHECK(store.Display()[0] == b);

  store.ToggleFavorite(b);                                 // 取消收藏：对称处理
  CHECK_EQ(b->isFavorite, false);
  CHECK_EQ(store.Display().size(), 1u);                    // 当场也不移出
  store.SetFilter(sc::FilterType::All);                    // 切视图即刷新
  CHECK_EQ(store.Display().size(), 3u);                    // 取消收藏的 b 回到普通区

  TempStore tmp2("s18b");
  sc::Store st2(tmp2.storage());
  st2.AddFromClipboard(L"x");
  st2.AddFromClipboard(L"y");                              // [y x]
  st2.ToggleFavorite(st2.Display()[0]);                    // y 收藏
  CHECK_EQ(st2.Display().size(), 2u);                      // 当场仍在
  st2.SetFilter(sc::FilterType::Favorite);                 // 一换视图即按新状态重算
  CHECK_EQ(st2.Display().size(), 1u);
  CHECK_EQ(st2.Display()[0]->content, L"y");

  // 2026-10-06 用户决议：同值重复选过滤项也算一次刷新（否则"点★后点【全部】没反应"）
  TempStore tmp3("s18c");
  sc::Store st3(tmp3.storage());
  st3.AddFromClipboard(L"m");
  st3.ToggleFavorite(st3.Display()[0]);
  CHECK_EQ(st3.Display().size(), 1u);                      // 当场仍在
  st3.SetFilter(sc::FilterType::All);                      // 本来就是 All → 同值也必须重算
  CHECK(st3.Display().empty());
}

// P0-1 回归（2026-10-06）：表格逐格去重会把 FR-02 迁移来的收藏项带回本批，
// 而整块插入按单元格顺序落位 → 收藏项可能落进非收藏区中间，破坏 [收藏区|非收藏区]
// 不变式；此后 ClearAll 用"收藏数量"当分界抹尾，就会误删用户收藏（违反 C12）。
void S19_TableBatchKeepsFavoritePartition() {
  TempStore tmp("s19");
  sc::Store store(tmp.storage());
  store.AddFromClipboard(L"x");
  store.AddFromClipboard(L"plain");                        // 集合 [plain x]
  const sc::ClipItem* x = store.Collection()[1];
  store.ToggleFavorite(x);                                 // → [x(收藏) plain]，分界 = 1
  CHECK_EQ(store.Collection()[0]->content, L"x");
  CHECK_EQ(store.Collection()[0]->isFavorite, true);
  CHECK_EQ(store.Collection()[1]->content, L"plain");
  CHECK_EQ(store.Collection()[1]->isFavorite, false);

  store.AddFromClipboard(L"y\tx");                         // 格1 "y" 新增；格2 "x" 命中旧收藏 → 收藏态迁移
  const auto col = store.Collection();
  CHECK_EQ(col.size(), 3u);
  CHECK_EQ(col[0]->content, L"x");                         // 分区不变式：收藏全部在前
  CHECK_EQ(col[0]->isFavorite, true);
  CHECK_EQ(col[1]->isFavorite, false);
  CHECK_EQ(col[2]->isFavorite, false);

  CHECK_EQ(store.ClearAll(), 2u);                          // 只清两条非收藏
  CHECK_EQ(store.TotalCount(), 1u);
  CHECK_EQ(store.Collection()[0]->content, L"x");          // C12：收藏还在
  CHECK_EQ(store.Collection()[0]->isFavorite, true);

  sc::Store reloaded(tmp.storage());                       // 落盘重载后分区仍成立
  reloaded.LoadFromDisk();
  CHECK_EQ(reloaded.Collection().size(), 1u);
  CHECK_EQ(reloaded.Collection()[0]->content, L"x");
  CHECK_EQ(reloaded.Collection()[0]->isFavorite, true);

  reloaded.Reset();                                        // 复位后收藏区仍只含收藏
  CHECK_EQ(reloaded.Collection().size(), 1u);
  CHECK_EQ(reloaded.Collection()[0]->isFavorite, true);
}

// ============ §9.6 Settings（4 例）============// settings.json 与 .NET v2.0.2 互换读写的硬要求：键名、键序、null 语义都得逐字节对齐。
std::string ReadAllBytes(const std::wstring& path) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return {};
  std::string out;
  char buf[4096]{};
  DWORD read = 0;
  if (ReadFile(f, buf, DWORD(sizeof(buf) - 1), &read, nullptr)) out.assign(buf, read);
  CloseHandle(f);
  return out;
}

void WriteRaw(const std::wstring& path, const std::string& bytes) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr);
  CloseHandle(f);
}

void G01_DotNetFileReadsAndWritesBackIdentical() {
  TempStore tmp("cfg1");
  const std::wstring p = tmp.dir() + L"\\settings.json";
  // 用户在用的 .NET 版真实产物（逐字节），C++ 必须原样读进、原样写回
  const std::string dotnet =
      R"({"Left":1540,"Top":216,"Width":380,"Height":600,"BoundProcessName":null,"Topmost":true,"PasteMode":0,"SplitSingleColumn":false,"FilterType":0})";
  WriteRaw(p, dotnet);

  const sc::Settings s = sc::LoadSettings(p);
  CHECK_EQ(s.left, 1540); CHECK_EQ(s.top, 216);
  CHECK_EQ(s.width, sc::kWinW); CHECK_EQ(s.height, sc::kWinH);
  CHECK(s.hasRect);
  CHECK(s.boundProcessName.empty());        // null = 未绑定
  CHECK(s.topmost);
  CHECK_EQ(s.pasteMode, 0);
  CHECK(!s.splitSingleColumn);
  CHECK_EQ(s.filterType, 0);

  CHECK(sc::SaveSettings(p, s));
  CHECK_EQ(ReadAllBytes(p), dotnet);        // 键序 + null 语义与 .NET 写出的完全一致
}

void G02_RoundTripWithBinding() {
  TempStore tmp("cfg2");
  const std::wstring p = tmp.dir() + L"\\settings.json";
  sc::Settings s;
  s.left = -1024; s.top = 60; s.width = 420; s.height = 700; s.hasRect = true;
  s.boundProcessName = L"PASTETARGET";
  s.topmost = false; s.pasteMode = 1; s.splitSingleColumn = true; s.filterType = 3;
  CHECK(sc::SaveSettings(p, s));

  const sc::Settings back = sc::LoadSettings(p);
  CHECK_EQ(back.left, -1024); CHECK_EQ(back.top, 60);
  CHECK_EQ(back.width, 420); CHECK_EQ(back.height, 700);
  CHECK(back.hasRect);
  CHECK_EQ(back.boundProcessName, L"PASTETARGET");
  CHECK(!back.topmost);
  CHECK_EQ(back.pasteMode, 1);
  CHECK(back.splitSingleColumn);
  CHECK_EQ(back.filterType, 3);
}

void G03_MissingOrCorruptFallsBackToDefaults() {
  TempStore tmp("cfg3");
  const std::wstring p = tmp.dir() + L"\\settings.json";

  const sc::Settings none = sc::LoadSettings(p);          // 首次运行：文件不存在
  CHECK(!none.hasRect);
  CHECK_EQ(none.width, sc::kWinW); CHECK_EQ(none.height, sc::kWinH);
  CHECK(none.topmost);                                    // C13 默认置顶
  CHECK_EQ(none.pasteMode, 0); CHECK_EQ(none.filterType, 0);
  CHECK(none.boundProcessName.empty());

  WriteRaw(p, "{");                                       // 半截 JSON
  CHECK(!sc::LoadSettings(p).hasRect);
  WriteRaw(p, "[]");                                      // 类型不对（照抄 history 的形态）
  CHECK(!sc::LoadSettings(p).hasRect);
  WriteRaw(p, "");                                        // 空文件
  CHECK(!sc::LoadSettings(p).hasRect);
  CHECK(sc::LoadSettings(L"").width == sc::kWinW);        // 数据目录不可用也不能抛
  CHECK(!sc::SaveSettings(L"", sc::Settings{}));          // 无路径 → 直接失败，不崩
  CHECK(!sc::SaveSettings(tmp.dir() + L"\\no-such-dir\\settings.json", sc::Settings{}));
}

void G04_OutOfRangeAndWrongTypePerField() {
  TempStore tmp("cfg4");
  const std::wstring p = tmp.dir() + L"\\settings.json";
  const std::string json =
      R"({"Left":99999999,"Top":0,"Width":100,"Height":9000,"Topmost":null,)"
      R"("PasteMode":true,"FilterType":"2","SplitSingleColumn":1,"Unknown":7})";
  WriteRaw(p, json);
  CHECK_EQ(ReadAllBytes(p), json);   // 先自证文件写到位，失败时才分得清「没写进去」和「没解析出来」

  const sc::Settings s = sc::LoadSettings(p);
  CHECK(!s.hasRect);                    // Left 是荒谬坐标 → 整组位置作废
  CHECK_EQ(s.left, 0);
  CHECK_EQ(s.width, sc::kWinW);         // 100 低于 kMinW → 回默认（不做静默放大）
  CHECK_EQ(s.height, sc::kWinH);        // 9000 超上限 → 回默认
  CHECK(s.topmost);                     // null → 按 C13 默认 true
  CHECK_EQ(s.pasteMode, 0);             // true 不是数字序号
  CHECK_EQ(s.filterType, 0);            // "2" 是字符串
  CHECK(s.splitSingleColumn);           // 1 走 asBool 的数字分支，认
}

}  // namespace

int main() {
  std::printf("SuperClip C++ 逻辑层单测\n");

  Run("§9.2-01 单行两列", T01_SingleRowTwoCols);
  Run("§9.2-02 两行两列", T02_TwoRows);
  Run("§9.2-03 空单元格列号不前移", T03_EmptyCellColKept);
  Run("§9.2-04 空行包裹的文本非表格", T04_BlankWrappedTextNotTable);
  Run("§9.2-05 尾部换行被裁", T05_TrailingNewlineTrimmed);
  Run("§9.2-06 裸 CR 当换行", T06_BareCarriageReturnAsLineBreak);
  Run("§9.2-07 三列", T07_ThreeColumns);
  Run("§9.2-08 表格复制模式逐行拆", T08_SingleColumnModeSplitsLines);
  Run("§9.2-09 一般复制多行整体", T09_NormalModeKeepsMultilineWhole);
  Run("§9.2-10 纯制表符零格", T10_OnlyTabsYieldNoCells);
  Run("§9.2-11 空行行号不前移", T11_BlankRowRowIndexKept);
  Run("§9.2-12 单值非表格", T12_PlainTextNotTable);
  Run("§9.2-13 单元格不 trim", T13_CellContentNotTrimmed);

  Run("§9.3-01 SHA-256 已知向量", H01_KnownVector);
  Run("§9.3-02 空内容不计哈希", H02_EmptyContentNoHash);
  Run("§9.3-03 中文稳定小写 hex", H03_ChineseStableLowerHex);
  Run("§9.3-04 不同内容不同哈希", H04_DifferentContentDifferentHash);
  Run("§9.3-05 长文本哈希", H05_LongText);
  Run("§9.3-06 普通文本无来源标注", L06_TextLabelEmpty);
  Run("§9.3-07 单元格来源标注", L07_TableCellLabel);
  Run("§9.3-08 无行列防御", L08_TableCellWithoutRowCol);

  Run("§9.4-01 去重与收藏态迁移", S01_DedupMigratesFavorite);
  Run("§9.4-02 插入位=收藏数量", S02_NewItemLandsAfterFavorites);
  Run("§9.4-03 末位淘汰（C7 回归）", S03_EvictOldestNotNewest);
  Run("§9.4-04 收藏永不淘汰", S04_FavoritesNeverEvicted);
  Run("§9.4-05 稳定分区保序", S05_StablePartitionKeepsRelativeOrder);
  Run("§9.4-06 表格整块不倒序", S06_TableBlockOrder);
  Run("§9.4-07 快速沉底（非收藏）", S07_QuickPasteSinksNonFavorite);
  Run("§9.4-08 快速沉底（收藏区末位，C8）", S08_FavoriteSinksToFavoriteZoneEnd);
  Run("§9.4-09 存盘重载顺序一致", S09_OrderSurvivesRoundTrip);
  Run("§9.4-10 Reset 清灰显 + 时间降序（C5）", S10_ResetClearsGrayAndSortsByTimeDesc);
  Run("§9.4-11 全角/大小写折叠搜索", S11_SearchFoldsFullWidthAndCase);
  Run("§9.4-12 过滤+搜索+选中保持", S12_FilterAndSearchCombined);
  Run("§9.4-13 收藏仅在【收藏】视图显示", S13_FavoritesOnlyInFavoriteView);
  Run("§9.4-14 来源标注不上屏仍参与搜索", S14_SourceLabelSearchableWhileHidden);
  Run("§9.4-15 清除保留收藏（C12）", S15_ClearAllKeepsFavorites);
  Run("§9.4-16 快速模式选中位钉第一行", S16_QuickModeAnchorsSelectionToFirstRow);
  Run("§9.4-17 接力取第一行与贴完即止（C15）", S17_RelayTakesFrontRowAndStopsWhenExhausted);
  Run("§9.4-18 点★暂留到下次刷新", S18_FavoriteStaysUntilNextRefresh);
  Run("§9.4-19 表格批次不破坏收藏分区（P0-1 回归）", S19_TableBatchKeepsFavoritePartition);

  Run("§9.6-01 .NET 文件读入并逐字节写回", G01_DotNetFileReadsAndWritesBackIdentical);
  Run("§9.6-02 全字段往返（含绑定进程名）", G02_RoundTripWithBinding);
  Run("§9.6-03 缺失/损坏回落默认", G03_MissingOrCorruptFallsBackToDefaults);
  Run("§9.6-04 越界与类型不符按字段作废", G04_OutOfRangeAndWrongTypePerField);

  std::printf("\n用例 %d，断言 %d 项，失败 %d 项 → %s\n", g_cases, g_checks, g_failures,
              g_failures == 0 ? "全部通过" : "存在失败");
  return g_failures == 0 ? 0 : 1;
}
