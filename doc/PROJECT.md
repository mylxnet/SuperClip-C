# SuperClip C++ 版 技术方案（实现级）

> 本文件原名 `C++_技术方案.md`，2026-10-05 随 agent.md 九.2 的目录标准移入 `doc/` 并改名（正文未改）。
> 仓内源码注释里出现的「技术方案 §x」一律指本文；.NET 旧版那份是 `doc/技术方案.md`，两者不要混。
>
> 版本：文档 v1.0（冻结于 v2.0.3）· 当前对应产品 **v2.5.0**（§11 步骤 1–12 A 段已落地）· 配套文档：`doc/DESIGN.md`（契约级，含 C1–C13 取值与 T1–T6 决议）
> 定位：模块接口、消息路由、状态机、渲染管线、降级矩阵、构建与测试的可执行说明
> 平台：Windows 7 SP1 / 10 / 11 x64 · C++20 / MSVC · 单文件免运行时 exe
> 冲突处理：本文与 `doc/DESIGN.md` 不一致时以契约文档为准；两者均优先于 `doc/SuperClip_设计规范.html` 的 §1/§10/§11/§12（技术栈章节，已作废）

---

## 1. 约定与红线

### 1.1 全局常量（`core/Config.h`，唯一来源，禁止散落魔数）

```cpp
namespace sc {
constexpr int      kMaxItems          = 500;      // FR-04 非收藏上限
constexpr UINT     kDebounceMs        = 300;      // FR-06 搜索防抖
constexpr UINT     kReadRetryMs       = 25;       // 剪贴板读取重试间隔
constexpr int      kReadRetryMax      = 6;        // 最多 6 次
constexpr UINT     kFocusWaitMs       = 60;       // 前台切换后等待焦点稳定
constexpr UINT     kPasteGuardMs      = 1000;     // C4：粘贴防护兜底
constexpr UINT     kPickTimeoutMs     = 8000;     // T2：点选卡死自动取消
constexpr size_t   kMaxContentChars   = 262144;   // T3：单条截断 256K 字符
constexpr int      kPreviewMeasureChars = 2000;   // 预览排版只测前 2000 字符
constexpr int      kMaxTipChars   = 2000;         // 悬浮气泡正文上限
constexpr int      kTipMaxW = 320, kTipMaxH = 240;  // 气泡尺寸上限（逻辑px）
constexpr UINT     kHoverTipMs    = 400;          // 悬停多久浮现气泡
constexpr int      kLogMaxBytes       = 1 << 20;  // error.log 上限，超限截断保留尾部
// 窗口逻辑尺寸（96dpi 基准）
constexpr int kWinW = 380, kWinH = 600, kMinW = 320, kMinH = 420;
constexpr int kTitleH = 36, kToolbarH = 76, kStatusH = 22, kBtn = 24, kStarW = 22, kTimeW = 58, kIndexW = 26;
constexpr int kRowPad = 8, kContentMaxH = 18;   // kContentMaxH = 单行高度上限（标注行已随 C11 取消，无 kLabelH）
constexpr UINT kBorderGrayHex = 0xC9CFD8, kInkGrayHex = 0x272C36;  // D2D 与气泡 GDI 两条路径共用
// v2.1.0 新增（仍在同一文件，仍是唯一来源）
constexpr int kTitleIconDip = 18, kTitleIconGap = 6;        // 标题栏应用图标边长 / 与模式文字的间隔
constexpr int kSearchClearDip = 16, kSearchClearInset = 6;  // 搜索框右端清除叉号边长 / 距右内缘
constexpr int kTipOffsetChars = 3;   // 气泡相对条目左缘右移的全角字数（字宽运行期实测）
inline constexpr wchar_t kProjectUrl[] = L"…github.com/mylxnet/SuperClip-C";  // 署名单击目标
// v2.2.0 新增、v2.3.0 修订、v2.3.3 改兜底键（C15 接力；仍同一文件、仍是唯一来源）
constexpr UINT WM_APP_RELAY_TRIGGER = WM_APP + 7;  // 0x8007 钩子投来的目标根窗
constexpr WPARAM kWtsSessionUnlock = 2;            // v2.3.0：解锁后由 SyncRelayHook 按状态装回
constexpr UINT     kRelayDedupeMs  = 300;          // Alt+双击的两次按下只算一次
constexpr int      kHotkeyIdRelay  = 2;            // 兜底键 id（v2.3.3 起 Alt+`，此前 Ctrl+Alt+空格）
constexpr UINT     kVkOem3         = 0xC0;         // ` ~ 键：呼出键与兜底键共用同一键位
constexpr UINT     kModNoRepeat    = 0x4000;       // MOD_NOREPEAT：旧 SDK 未导出，固定值兜底
// v2.3.3 删除 kVkSpace(0x20)：兜底键改用 kVkOem3 后失去唯一引用
}
```

### 1.2 命名与编码约定

| 项 | 约定 |
|---|---|
| 命名 | 类型 `PascalCase`、函数/变量 `camelCase`、成员尾下划线 `hwnd_`、常量 `kPascal`、文件 `PascalCase.h/.cpp`（与类同名） |
| 字符 | 全程 **UTF-16**（`std::wstring`）为工作编码；仅 JSON 落盘转 UTF-8；源文件统一 UTF-8 with BOM，编译加 `/utf-8` |
| Unicode | 一律 `W` 后缀 API；定义 `UNICODE/_UNICODE` |
| 所有权 | 每个 `HWND`/`HHOOK`/`HMENU`/`HGLOBAL`/`HDC` 必须有唯一 owner 与固定释放点，见 §4.1 |
| 禁止 | 异常跨越 `WndProc` 边界（最外层 `__try/__except` + `catch(...)`）；禁止 `std::endl` 之外的同步刷写进渲染路径；禁止在钩子回调内做 IO/弹窗/排版 |
| 日志 | `SC_LOGW/E(...)`，Release 下 Info 级编译期剥离，只保留 Warn/Error |

### 1.3 分层依赖（单向）

```
ui/  ──▶ core/ ──▶ services/ ──▶ native/ ──▶ Win32/D2D/DWrite
        （core 不含任何 HWND / COM 渲染类型；services 不含 ui 类型）
```
`core/Store` 是唯一业务状态持有者；`ui` 只读 `Store` 并通过方法驱动变更；`services` 通过 `std::function` 回调向上投递，不反向依赖。无 DI 容器（规模小，构造函数显式 `new`/引用注入）。

---

## 2. 文件与职责清单

| 文件 | 职责 | 预估行数 |
|---|---|---|
| `src/main.cpp` | `wWinMain`：COM 初始化、DPI 上下文、单实例判定、构造 `AppContext`、消息循环 | 90 |
| `src/app/AppContext.h/.cpp` | 服务编排、退出清理链、全局防护标志（`_internalPaste`/`_lastPasted`）、崩溃兜底 | 300 |
| `src/core/ClipItem.h` | `ClipItem` + `ClipType` + `SourceLabel()` + 折叠搜索键 | 90 |
| `src/core/TableParser.h/.cpp` | `IsTable` / `Parse` / `Cell` | 110 |
| `src/core/Sha256.h/.cpp` | BCrypt 封装 + 十六进制 | 60 |
| `src/core/Store.h/.cpp` | 列表状态机（去重/分区/沉底/上限/过滤/搜索/事件） | 340 |
| `src/core/Time.h/.cpp` | naive 本地时间 ↔ UTC ↔ ISO 串 | 90 |
| `src/core/Text.h/.cpp` | UTF8↔16、全角半角+ASCII 折叠、宽字符 trim/split | 140 |
| `src/native/*.h` | 句柄 RAII（§4.1）、剪贴板、前台、键盘、热键、光标、DPI/DWM 动态加载 | 420 |
| `src/services/ClipboardMonitor.h/.cpp` | 隐藏窗口 + 重试读取状态机 | 200 |
| `src/services/PasteService.h/.cpp` | 写盘剪贴板 + 三段夺前台 + SendInput 阶段机 | 220 |
| `src/services/StorageService.h/.cpp` | history.json 原子读写与容错 | 210 |
| `src/services/TrayService.h/.cpp` | `Shell_NotifyIcon` + 菜单 + `TaskbarCreated` 自愈 | 210 |
| `src/services/ProcessPicker.h/.cpp` | WH_MOUSE_LL 点选 + 光标替换 + 超时/锁屏取消（**v2.5.0 起绑定不再持久化，原 `FindWindowByProcess` 按进程名找回绑定窗口的能力随设置持久化一并移除**） | 230 |
| `src/services/RelayService.h/.cpp` | **（v2.2.0 引入 / v2.3.0 无开关，C15）** WH_MOUSE_LL 接力钩子：只管"机制"——**幂等重装**的装/卸、Alt 判定、去重、`PostMessage` 投递目标根窗。**不含策略**（该不该装、取哪条、贴到哪、怎么提示的裁决全在 `AppContext::SyncRelayHook()`）；进程内 `instance_` 转发回调 | 43 + 75 |
| `src/ui/MainWindow.h/.cpp` | 窗口类、消息路由、命中分发、焦点/模式编排 | 938 |
| `src/ui/ListRenderer.h/.cpp` | 行测量、TextLayout 缓存、绘制、滚动 | 430 |
| `src/ui/Theme.h/.cpp` | 颜色/字号/几何图标路径、高对比度切换 | 200 |
| `src/ui/HoverTip.h/.cpp` | 悬浮全文气泡（自绘弹出窗，GDI 量算与绘制，不依赖 comctl32） | 190 |
| `src/ui/Widgets.h/.cpp` | `EDIT`（占位自绘）、自绘按钮/菜单（步骤 9+ 才建，气泡已落 `HoverTip`） | 180 |
| `src/ui/HelpWindow.h/.cpp` | 9 步模态引导（v2.4.0 曾加「填表接力」页，v2.4.1 删除） | 150 |
| `src/res/app.rc`、`manifest.xml` | 图标、`VERSIONINFO`、DPI/comctl6 | 90 |
| `tests/*` | 单测：40 例（§9.2/9.3/9.4；§9.1 的"33 例/Catch2"是步骤 10 前的旧口径，见下方备注）。**用例数不再写死在源码里**：`Run()` 自增 `g_cases`，末尾一行打印实测值 | 759 |

合计约 **4600 行**（.NET 版约 2000 行），差异集中在 `ui/`。

---

## 3. 核心数据与控制流

### 3.1 数据结构

```cpp
// core/ClipItem.h
enum class ClipType : int { Text = 0, TableCell = 1 };

struct ClipItem {
  std::wstring id;                 // GUID 小写无花括号，仅工厂写入
  std::wstring content;            // 仅工厂写入（T3 已截断）
  ClipType     type = ClipType::Text;
  int  sourceRow = 0, sourceCol = 0;   // 0 = 无
  FILETIME createdUtc{};              // 统一 UTC 存储，显示/序列化转本地
  std::wstring hash;               // SHA-256 小写 hex（空内容 → 空串）
  std::wstring foldContent;        // 派生：折叠后的 content，入列时算一次，不持久化
  std::wstring foldLabel;          // 派生：折叠后的 SourceLabel
  bool isFavorite = false;         // 可变
  bool isPasted   = false;         // 可变

  std::wstring SourceLabel() const;   // "来自表格：第 X 行 第 Y 列" | L""（不上屏，仅作搜索字段）
};

// 工厂（唯一构造入口，保证派生字段与哈希一致）
std::unique_ptr<ClipItem> MakeItem(std::wstring content, ClipType t, int row, int col);
```
语法上不用 `const` 成员（`vector` 迁移需赋值），"仅工厂可写"由约定 + 单测（§9.4 不变式用例）保证。

### 3.2 Store 接口

```cpp
enum class FilterType { All, Text, TableCell, Favorite };
enum class PasteMode  { Normal, Quick };
enum class CopyMode   { Normal, TableSingleColumn };   // 原 SplitSingleColumn

enum class StoreEventKind { FullReplaced, ItemsInserted, ItemRemoved, FlagsChanged, SelectionChanged };
struct StoreEvent { StoreEventKind kind; const ClipItem* primary = nullptr; };

class Store {
public:
  explicit Store(StorageService&);
  void LoadFromDisk();                       // 读 → ApplyOrder → 规范化保存 → FullReplaced
  void AddFromClipboard(std::wstring_view);   // FR-01/02/05 总入口
  void ToggleFavorite(const ClipItem*);       // v2.4.2：只「翻转→ApplyOrder→落盘→Emit」，不再调 RebuildDisplay()
  void PasteDone(const ClipItem*, bool moveToEnd);
  size_t ClearAll();  void Reset();   // ClearAll 只清非收藏区（C12），返回删除条数
  void SetFilter(FilterType);  void ApplySearch(std::wstring kw);  // UI 已防抖
  void Select(const ClipItem*);              // 所有模式同步选中（原 SelectItem）
  void   AnchorQuickSelection();             // C14：Quick 下把选中位钉到 display_.front()，普通模式空操作
  const ClipItem* RelayNext() const;         // C15（v2.3.0）：＝ display_.front()，所见即所贴；它已灰显或显示区空 → nullptr
  // v2.2.0 的 `RelayArmable()`（【全部】视图 + 空搜索才许武装）已在 v2.3.0 删除：作用域改由 AppContext 裁决。
  std::function<void(const StoreEvent&)> onEvent;

  const std::vector<const ClipItem*>& Display() const;
  const ClipItem* Selected() const;  size_t TotalCount() const;
  std::optional<size_t> IndexOfDisplay(const ClipItem*) const;   // 序号 = index+1（FR-17）
private:
  size_t Boundary() const;      // 第一个非收藏项下标（= 收藏数量）
  void   EnforceLimit();  void ApplyOrder();  void RebuildDisplay();
  std::vector<std::unique_ptr<ClipItem>> items_;   // 不变式 [fav… | nonFav…] 最新在前
  std::vector<const ClipItem*> display_;
  std::wstring foldedKeyword_;
  FilterType filter_ = FilterType::All;
};
```

### 3.3 顺序不变式与派生规则（含 C7/C8）

| 规则 | 实现 | 出处 |
|---|---|---|
| 结构不变式 | `items_` = `[收藏区(最新在前) \| 非收藏区(最新在前)]`，即下标 `0..Boundary()-1` 为收藏，其后为非收藏 | §4.3 |
| 插入位 | `insert(begin()+Boundary(), item)`（表格块按阅读顺序整块插入，首格在上） | FR-05 |
| 沉底 | 非收藏项 → `move` 到 `end()`；**收藏项 → `move` 到收藏区末尾（`begin()+Boundary()-1`）** | FR-10 / **C8 补充** |
| 淘汰 | 删除**非收藏区末尾**超额项（`erase(end()-excess, end())`） | **C7 末位淘汰** |
| 重排 | `std::stable_partition(begin,end,isFavorite)` | FR-08 |
| 复位 | 清全部 `isPasted`；两次 `stable_sort`（收藏按 `createdUtc` 降序、非收藏同样降序，稳定 → 同刻保持原序） | FR-14 / C5 |
| 快速模式选中位 | `RebuildDisplay()` 末尾（以及 `TogglePasteMode`、点选绑定完成回调）调 `AnchorQuickSelection()`：Quick 且显示区非空时把 `selected_` 钉为 `display_.front()`，已是首行则不发事件；**普通模式空操作** | **C14**（2026-10-05 用户决议）/ FR-10 |

**C8 补充说明**（原文未覆盖）：快速模式沉底若对收藏项执行"移到 vector 末尾"，会破坏 `[收藏区 | 非收藏区]` 不变式（收藏项落到非收藏区之后）。故收藏项的沉底目标是**收藏区末尾**，既保持"沉到本分区最旧位"，又维持分区结构。

### 3.4 关键流程

**采集链路**
```
WM_CLIPBOARDUPDATE(0x031D) @Monitor
  ├─ AppContext.isInternalPaste()? ─是→ 清标志 return        // 第一层
  ├─ ScheduleRead(): attempt=1, SetTimer(kReadRetryMs, ID_READ)
  │    └─ 每次 ID_READ：OpenClipboard+GetClipboardData(CF_UNICODETEXT)
  │         ├─ 成功/非文本/空文本 → 结束（非文本不重试，省 6 轮）
  │         └─ 失败 && attempt<6 → ++attempt, 重设定时器
  ├─ content == AppContext.lastPasted()? ─是→ return          // 第二层
  ├─ T3: 超过 kMaxContentChars → 截断 + Warn 日志
  └─ Store.AddFromClipboard(text)
         ├─ TableParser.IsTable → Parse → 逐格 MakeItem（去重移除旧格）→ 整块插入 Boundary
         ├─ 否则 MakeItem → 同 hash 先删旧（继承旧收藏态）→ 插 Boundary
         ├─ EnforceLimit()                                    // C7
         ├─ Storage.Save(items_)                               // 原子写
         └─ onEvent → ui（Rebuild + 保持选中/滚动）
```

**粘贴链路**（`DoPaste`，全异步分段、无阻塞）
```
DoPaste(item, moveToEnd, targetOverride = nullptr):        // v2.2.0：接力传点中的根窗，普通链为 nullptr
  isInternalPaste = true; lastPasted = item.content
  PasteService.Start(owner=mainWnd, target=targetOverride ? targetOverride : GetPasteTarget(), item.content)
    stage=Write →  stage=WaitForeground（60ms 定时器）  →  stage=SendKeys（InjectCtrlV：一次 SendInput 投 5 或 6 事件）  →  stage=Idle
    （v2.3.2 批次＝[按需 Ctrl↑] Ctrl↓ Alt↑ V↓ V↑ Ctrl↑，每事件带 wScan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC)；
      Alt↑ 恒排进批次、不按键态判断，Ctrl↑ 在注入那一刻用 GetAsyncKeyState 复查；
      写盘阶段只保留"用户按着 Ctrl 就补发 Ctrl up"，v2.3.2 已删掉点击那一刻的单独 Alt↑；
      目标已是前台则跳过夺前台。⚠ 这两版改动都没修好真表格落点，见 doc/PROJECT_STATE.md §4 坑 #20）
  收到 WM_APP_PASTE_DONE：
    Store.PasteDone(item, moveToEnd)         // 灰显 + 沉底（C8）；沉底后第一行＝下一条未粘贴（C14/C15 共用）
    SetTimer(kPasteGuardMs, ID_PASTE_GUARD)  // 兜底清标志
```

**接力链路（C15，v2.3.0 无开关）**
```
作用域裁决 AppContext::SyncRelayHook()          // 唯一装/卸入口，被下面每一个状态变化点调用
  want = Store.pasteMode()==Quick && MainWindow.IsVisible() && !ProcessPicker.picking()
  调用点：Initialize（默认普通模式）· ToggleVisibility 收起分支 · ShowAndFocus · 标题栏最小化按钮 ·
          TogglePasteMode（标题栏文字与右键菜单两条切换共用它）· TogglePick 两条路径 ·
          picker_.onPicked / onCanceled · OnSessionUnlock · OnSessionLock/OnEndSession 直接 Disarm
  ├─ want 为假 → RelayService::Disarm(原因)（未挂着时静默早退，不刷日志）
  └─ want 为真 → RelayService::Arm(mainWnd, inst)   // SetWindowsHookExW(WH_MOUSE_LL)
        ★ 幂等**重装**：已挂着就先 UninstallHook() 再挂——低层钩子被系统按 LowLevelHooksTimeout
          静默摘掉后没有任何查询 API，症状只是"Alt+点不灵且不报错"，重挂是唯一自愈路径
        装不上 → LogWarn + 状态栏提示，普通粘贴不受影响

钩子回调 RelayService::LlHook → OnMouse(lParam)     // 运行在装载钩子的 UI 线程，只做两件事
  ├─ !IsAltDownAsync()            → return 0  放行
  ├─ kRelayDedupeMs(300ms) 内再触发 → return 0  放行（Alt+双击只算一次）
  ├─ root = GetAncestor(WindowFromPoint(pt), GA_ROOT)
  ├─ root 为空 / 是自家窗          → return 0  放行
  └─ PostMessageW(mainWnd, WM_APP_RELAY_TRIGGER, root) ; return 0   // ★放行，绝不 return 1

主窗 WM_APP_RELAY_TRIGGER → AppContext::OnRelayTrigger(target)
  └─ RelayStep(target)
        ├─ target 已失效 / 是自家窗 → LogWarn 后返回，不动列表
        ├─ item = Store.RelayNext()  // ＝显示区第一行，过滤/搜索态按屏幕所见的算（所见即所贴）
        │     └─ nullptr（第一行是灰条）→ LogInfo + 状态栏「没有未粘贴的条目，复制新的内容即可继续」
        │                                  ★ 不卸钩：用户复制一条新的，第一行立刻就是它
        └─ MainWindow::PasteForRelay(item, target) → DoPaste(item, /*moveToEnd*/true, target)

卸下（没有"结束接力"这个用户动作）
  切到普通模式 · 收起/最小化主窗 · 点选开始 · 会话锁屏 · 注销/关机 · 退出清理链
  → RelayService::Disarm(reason)：先 UnhookWindowsHookEx 再清 instance_（顺序不能反，同点选 §7.2）
```

---

## 4. 基础设施层设计

### 4.1 句柄 RAII（`native/`，全部 header-only）

| 类型 | 获取 | 释放 | 用途 |
|---|---|---|---|
| `ClipbrdLock` | `OpenClipboard` | `CloseClipboard` | 读/写窗口期；析构必关（防他进程永久阻塞） |
| `GlobalMem` | `GlobalAlloc(GMEM_MOVEABLE)` | 移交成功则 `release()`；否则 `GlobalFree` | 剪贴板数据块 |
| `MenuGuard` | `CreatePopupMenu` | `DestroyMenu` | 右键/筛选菜单 |
| `HookGuard` | `SetWindowsHookExW(WH_MOUSE_LL)` | `UnhookWindowsHookEx` | 点选钩子 |
| `ProcessHandle` | `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` | `CloseHandle` | 进程名解析 |
| `Window` | `CreateWindowExW` | `DestroyWindow`（仅 Monitor/Tray；主窗由 `PostQuitMessage` 收尾） | 隐藏窗口 |
| `ComPtr<T>` | 工厂 `Create*` | `Release` | 直接用 `Microsoft::WRL::ComPtr` |
| `ScopeExit` | — | 执行 lambda | 光标复位等"任何路径都要做"的收尾 |

**规则**：任何 `HWND` 不在 RAII 中时，必须在 `AppContext::Exit` 的固定顺序表里出现一次；新增窗口需同步该表（§5.3）。

### 4.2 文本工具（`core/Text.h`）

```cpp
std::string  ToUtf8(std::wstring_view);
std::wstring FromUtf8(std::string_view);        // 非法字节替换 U+FFFD，不抛
std::wstring FoldKey(std::wstring_view);        // 全角→半角(U+FF01..FF5E −0xFEE0, U+3000→U+0020) + ASCII 大小写折叠
bool ContainsFolded(std::wstring_view hayFolded, std::wstring_view needleFolded);
std::vector<std::wstring_view> SplitLines(std::wstring_view);   // 支持 \r\n / \r / \n
std::wstring TrimDisplay(std::wstring_view);    // 渲染前：'\t'→4空格，'\r'→'\n'
```
**明确不对西里尔/希腊字母折叠**（中文场景无损；避免引区域 API 破坏测试确定性）。`CompareStringW` 不再使用。

### 4.3 时间与 GUID

```cpp
FILETIME NowUtc();                       // GetSystemTimeAsFileTime
std::wstring ToLocalIso(FILETIME);       // SystemTimeToTzSpecificLocalTime → "YYYY-MM-DDTHH:MM:SS.mmm"
std::optional<FILETIME> ParseIso(std::string_view);  // 兼容：带毫秒/无毫秒/带 ±hh:mm 偏移
std::wstring NewGuidString();            // CoCreateGuid → StringFromGUID2 → 去 {} + 转小写
```
`Timestamp` 无时区后缀 → 解析时按 `TzSpecificLocalTimeToSystemTime` 还原；中国大陆时区无夏令时，歧义为零。带偏移的旧数据按其偏移解析。

### 4.4 动态加载（`native/SystemInfo.h`）

Win7 上直接引用 Win10 符号会在**加载期**失败（exe 双击无反应）。因此以下一律 `GetProcAddress` 探测 + 回退：

| 符号 | 首选 | 回退链 |
|---|---|---|
| DPI 感知 | `SetProcessDpiAwarenessContext(PER_MONITOR_V2)`（1607+） | `SetProcessDPIAware`（Vista+） |
| 取 DPI | `GetDpiForWindow`（1607+） | `shcore!GetDpiForMonitor`（8.1+） → `GetDeviceCaps(hdc, LOGPIXELSY)` |
| 圆角 | `dwmapi!DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)`（11 22H2+） | `DwmExtendFrameIntoClientArea`（Vista+）→ 无阴影直角 |

因 `_WIN32_WINNT=0x0601`，上述新 API 的**原型不在系统头文件里**，需在 `native/SystemInfo.h` 自行声明函数指针 typedef 与常量值（如 `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4`、`DWMWA_WINDOW_CORNER_PREFERENCE = 33`、`DWMWCP_ROUND = 2`），不得通过提升 `_WIN32_WINNT` 来"省事"——那会让链接器放行不可用符号，在 Win7 上变成启动即失败。

manifest 同时写 `<dpiAware>true</dpiAware>` 与 `<dpiAwareness>PerMonitorV2</dpiAwareness>`：Win7 读前者，Win10 读后者。

### 4.5 日志与异常兜底

- `Log`：`%AppData%\SuperClip\error.log` 追加；行格式 `[2026-10-04 00:31:02.123] [E] [module] msg (gle=5)`；超过 `kLogMaxBytes` 时保留尾部 512KB 重写。
- `main.cpp` 里三层：`SetUnhandledExceptionFilter`（SEH/系统异常）、`std::set_terminate`（未捕获 C++ 异常）、`__try/__except` 包裹每个 `WndProc` 主体。
- **过滤器第一动作**（顺序固定，防残留）：`ResetSystemCursors()` → `Unhook（若装着）` → `NIM_DELETE` → `UnregisterHotKey` → 写日志 → `MessageBoxW`（仅非会话关闭时）→ 退出。
- 崩溃兜底内**不得**申请新内存排版、不得写 history（只写日志）。

---

## 5. 服务层设计

### 5.1 ClipboardMonitor

```cpp
class ClipboardMonitor {
public:
  bool Create(HINSTANCE, UINT dpiFallback);
  void Destroy();
  std::function<void(std::wstring)>            onText;
  std::function<bool()>                        isInternalPaste;
  std::function<std::wstring()>                lastPasted;
private:
  LRESULT Handle(UINT msg, WPARAM, LPARAM);    // WM_CLIPBOARDUPDATE / WM_TIMER(ID_READ) / WM_DESTROY
  void OnClipboardUpdate();  void TryRead();
  HWND hwnd_ = nullptr;  int attempt_ = 0;
};
```
要点：窗口必须 `WS_POPUP` + `WS_EX_TOOLWINDOW` + 标题 `L"SuperClipMonitor"` + 尺寸 0×0 @(-32000,-32000)；**禁 `HWND_MESSAGE`**（`TaskbarCreated` 广播不投递 message-only）。`AddClipboardFormatListener` 失败 → Warn 日志、继续运行（功能降级：不自动入列，手动粘贴仍可用）。

### 5.2 PasteService

```cpp
class PasteService {
public:
  enum class Stage { Idle, Write, WaitForeground };
  void Start(HWND owner, HWND target, std::wstring text);   // 立即返回
  bool busy() const { return stage_ != Stage::Idle; }
  void OnTimerId(UINT id);                                   // ID_FOCUS_WAIT
  std::function<void(bool ok)> onDone;                       // → 主窗投 WM_APP_PASTE_DONE
private:
  bool WriteClipboard(const std::wstring&);                   // GHND → EmptyClipboard → SetClipboardData
  bool BringTargetToFront(HWND target);                       // 三段兜底（pid 由 WindowPid 取）
  UINT InjectCtrlV();                                         // v2.3.1 引入、v2.3.2 改序：复查 Ctrl → 同批注入 Ctrl↓→Alt↑→V↓→V↑→Ctrl↑（带扫描码），返回事件数
  Stage stage_ = Stage::Idle; HWND owner_ = nullptr, target_ = nullptr; std::wstring injected_;   // injected_ 供日志自证批次顺序
};
```
关键分支与降级：
| 情况 | 行为 |
|---|---|
| `target == nullptr \|\| target == 主窗` | 只写剪贴板，不发键（防粘回自己）——继承原语义 |
| `IsIconic(target)` | **T4：`ShowWindow(SW_RESTORE)` 后继续**（最小化窗口 `SetForegroundWindow` 无效）；还原后**仍须走满三段**，见下注 |
| `GetKeyState(VK_CONTROL) < 0`（用户按着 Ctrl） | 先发 `VK_CONTROL↑` 再走流程，防目标收到裸 `v` |
| 三段都失败 | 仍在当前前台发 `Ctrl+V`（原行为），Warn 日志；不弹提示 |
| `OpenClipboard` 失败（被占） | 重试 3 次 × 25ms，仍失败 → `onDone(false)`，静默 |
| 提权目标（UIPI） | `SendInput` 被静默丢弃，无法判定成功与否（N5 已知边界） |

> **实机注（步骤 8，2026-10-04）**：`BringTargetToFront` 里**不能**加"`GetForegroundWindow()==target` 就直接返回"的短路。
> `SW_RESTORE` 已把目标顶到前台，此时 `GetForegroundWindow()` 读起来是目标，但缺少本进程的
> `AllowSetForegroundWindow`/`SetForegroundWindow` 授权步骤，随后 `SendInput` 的按键仍会被上一个前台线程吞掉：
> 自建宿主最小化态下粘贴 4/4 失败（日志显示"前台=目标、事件数=4"却无内容落地），去掉短路后 2/2 通过。

`SetClipboardData` 成功后所有权归系统，**不得** `GlobalFree`；失败才释放。

### 5.3 StorageService

```cpp
class StorageService {
public:
  bool EnsureDir();                                   // SHGetKnownFolderPath + CreateDirectoryW
  std::vector<std::unique_ptr<ClipItem>> Load();      // 任何失败 → 返回空（不报错给调用方）
  bool Save(const std::vector<std::unique_ptr<ClipItem>>&);   // tmp + ReplaceFileW
  bool Corrupted() const;                             // 供 UI 提示"历史文件已重置"
private:
  std::wstring path_, tmpPath_;
};
```
序列化：字段顺序 `Id,Content,Type,SourceRow,SourceCol,Timestamp,Hash,IsFavorite,IsPasted`，紧凑单行；`null` 用于 `SourceRow/Col==0`（对应 .NET `int?`）。读侧：`0/1` 之外的 `Type` → 按 `Text` 处理并 Warn；条目解析失败 → **跳过该条**而非整体失败（比原版更宽容，尽量保数据）。
原子写失败（`ReplaceFileW` 返回 FALSE，如目标被占用）→ 退 `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`；再失败 → `DeleteFileW` + `MoveFileExW`；全部失败仅 Warn，内存态继续可用。

### 5.4 TrayService

`NOTIFYICONDATAW`：`uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP`，`uCallbackMessage = WM_APP+1 (0x8001)`，图标 `LoadImageW(SM_CXSMICON)`，tooltip `L"SuperClip 超级剪贴板"`。
- `WM_LBUTTONDBLCLK` → 通知打开主窗；`WM_RBUTTONUP` → `SetForegroundWindow(自身)` 后 `TrackPopupMenuEx(TPM_RETURNCMD)`（**先置前台，否则点菜单外不消失**）→「打开 / 退出」。
- `RegisterWindowMessageW(L"TaskbarCreated")` 收到即 `NIM_DELETE`+`NIM_ADD` 重挂（Explorer 重启自愈）。
- `NIM_ADD` 失败 → Warn 继续运行（可 Alt+Tab 回主窗）。
- **`ShowBalloon(title, text)`（v2.2.0 新增，v2.3.0 起暂无调用方）**：拷贝 `nid_` → `uFlags = NIF_INFO` + `szInfoTitle`/`szInfo`（`wcsncpy_s` 截断）
  + `dwInfoFlags = NIIF_INFO` → `NIM_MODIFY`。**当初存在的理由已经消失**：v2.2.0 接力武装/解除时主窗是隐藏的，状态栏没人看得见；
  v2.3.0 接力不再藏窗（能看见列表才能贴），所有接力反馈都走 `AppContext::ShowStatusHint()` 的状态栏路径，本方法遂**成为保留但未使用的能力**。
  保留不删的判据：它是契约 T5「热键注册失败要给用户可见提示」的现成落点（见下条），删掉等于把那条待办重新做一遍。
  字段名是 `szInfoTitle`（不是 `szTitle`，写错会报 "has no member named"）。**能否弹出已实测通过**（2026-10-05 19:47
  `cpp/build-mingw/relay-balloon.png`：Win11 把 `NIF_INFO` 转成 toast，正文逐字正确；**标题位乱码是照出的资源缺陷**，见 `doc/PROJECT_STATE.md` §4 坑 #15 —— **v2.3.4 已在资源侧修掉**（`app.rc` 加 `#pragma code_page(65001)`，产物 `FileDescription` 按码点读回＝`超级剪贴板`）；那张图是 v2.3.0 之前拍的，仍留着乱码标题作为缺陷存证，**toast 标题的实机观感尚未重拍验证**）。
- ~~若 `T5` 热键注册失败：`NIM_MODIFY` + `NIF_INFO` 气泡提示快捷键被占用~~ → **未实现**：`Ctrl+`` 被占用时
  `MainWindow::RegisterHotkeys()` 只写一行 `LogWarn`（界面无感）。`ShowBalloon` 已具备投递能力，这条只差一个调用点，
  但属新增行为、不在 v2.3.0 范围，记入 `doc/PROJECT_STATE.md` §6 待办。

### 5.5 ProcessPicker（点选）

```cpp
class ProcessPicker {
public:
  bool Start(HWND mainWnd, HINSTANCE);      // 装钩子 + 换光标 + 隐藏主窗
  void Cancel();  bool picking() const;
  std::function<void(HWND bound, std::wstring processName)> onPicked;
  std::function<void()> onCanceled;
private:
  static LRESULT CALLBACK LlHook(int, WPARAM, LPARAM);   // 转发到实例
  LRESULT OnMouse(int code, WPARAM, LPARAM);
  void EndPick(bool ok, HWND target);
  HHOOK hook_ = nullptr; bool active_ = false; UINT_PTR timeoutTimer_ = 0;
};
```
流程（含 T2 决议）：
```
Start: SetSystemCursor(LoadCursorW(nullptr, IDC_CROSS), OCR_NORMAL)   // 失败仅 Warn，继续无特效点选
       SetTimer(kPickTimeoutMs, ID_PICK)  →  ShowWindow(mainWnd, SW_HIDE)
       SetWindowsHookExW(WH_MOUSE_LL, LlHook, GetModuleHandleW(nullptr), 0)
LlHook(WM_LBUTTONDOWN):
       pt = MSLLHOOKSTRUCT.pt (屏幕坐标)
       hwnd = WindowFromPoint(pt) → root = GetAncestor(hwnd, GA_ROOT)
       if root == mainWnd 或 GetWindowThreadProcessId(root)==自身 → 返回 1 吞掉，仍在点选
       else: PostMessageW(selfWnd, WM_APP_PICK_DONE, root) ; return 1   // T2：吞掉，目标选区不被改变
主线程 WM_APP_PICK_DONE:
       name = ProcessNameOf(root)  // OpenProcess → QueryFullProcessImageNameW → 基名大写、去 .exe
       EndPick(ok=true, root)
EndPick: UnhookWindowsHookEx → KillTimer → SystemParametersInfoW(SPI_SETCURSORS,0,nullptr,0) → ShowWindow(mainWnd, SW_SHOW)
```
`EndPick` 幂等 + `ScopeExit` 保证光标复位；`WM_WTSSESSION_CHANGE`（锁屏）、`WM_ENDSESSION`、超时、Esc、再点靶心、热键——六条路径都走同一个 `Cancel()`。降级：钩子装不上 → 直接绑 `_lastExternalWindow` 并 Warn。

> **实现注（步骤 9，2026-10-04）**
> - `LlHook` 无用户数据参数，用类内 `static ProcessPicker* instance_` 转发；`Cleanup()` 里**先 `UnhookWindowsHookEx` 再清 `instance_`**，反序会在卸钩子瞬间丢回调对象。
> - 钩子内只做 `WindowFromPoint`/`GetAncestor` + `PostMessageW`；进程名解析（`OpenProcess`/`QueryFullProcessImageNameW`）留给主线程，避免踩 `LowLevelHooksTimeout`。`WindowFromPoint` 拿不到窗口时**放行**这次点击，绝不吞。
> - `ProcessNameOf(HWND)` 就放在 `services/ProcessPicker.cpp`（§5.5 流程里的同名步骤），不新增 native 文件；基名大写、去扩展名，读不到返回空串而绑定照旧生效（状态栏显示"已绑定（进程名未知）"）。
> - `SetSystemCursor` 第二参是 `DWORD`，`OCR_NORMAL` 即序号 32512（`Config.h::kOcrNormal`；mingw-w64 的 winuser.h 未导出 `OCR_*`）。
> - 定时器挂在主窗上：主窗隐藏期间 `WM_TIMER` 仍会投递，所以 8s 超时不依赖窗口可见。
> - 取消语义：`Cancel()` **不清已有绑定**（取消的是本次点选）；`_boundOnce`/首次呼出自动绑定已作废，见设计方案 §5.4、§6.5 决议。
> - T2 只拦 `WM_LBUTTONDOWN`：低层钩子吞掉 down、放行 up，故目标只收到一次"按下已缺失的抬起"，选区与按钮状态不受影响（QA 主机 `WM_APP+9` 计数器实测：普通点击 +2，点选命中那一下 +1）。
> - 光标判据：武装期间 `GetCursorInfo.hCursor` = 65541（十字），`Cancel` 后回 65539（箭头），实测无残留。
> - `WM_ENDSESSION` **无法用 `PostMessageW` 注入验证**（返回 TRUE 但窗口过程收不到；同法注入 `WM_WTSSESSION_CHANGE` 0x02B1 可达并已实测取消）。真实注销/关机由系统自行投递，代码路径与锁屏共用 `EndPick`，实机复核需真实会话结束。


### 5.6 单实例

```cpp
HANDLE m = CreateMutexW(nullptr, TRUE, L"Local\\SuperClip_SingleInstance_9F3A2B1C");   // T1
if (GetLastError() == ERROR_ALREADY_EXISTS) { ActivateExisting(); return 0; }
```
- **T1**：`Global\` 需要 `SeCreateGlobalPrivilege`，标准用户创建会失败 → 改 `Local\`，语义为"每登录会话单实例"（多用户同时登录各自一份，符合桌面工具预期）。
- `ActivateExisting`：`EnumWindows` 遍历标题含 `SuperClip` 的可见 top-level → `GetWindowThreadProcessId` → `QueryFullProcessImageNameW` 与 `GetModuleFileNameW` **全路径比对**相同者才算旧实例（消除原 R10"同名标题误激活"）。命中后 `IsIconic→SW_RESTORE` + `SetForegroundWindow`。

### 5.7 RelayService（接力钩子，v2.2.0 引入 / v2.3.0 改无开关 / C15）

```cpp
class RelayService {
public:
  ~RelayService();                        // 析构即 Disarm(L"析构")
  bool Arm(HWND mainWnd, HINSTANCE);      // 幂等：没挂就挂，已挂着先 UninstallHook() 再重挂（自愈）
  void Disarm(const wchar_t* reason);     // 卸钩；当前没挂着直接 return（幂等，不刷日志）
  bool armed() const;
private:
  static LRESULT CALLBACK LlHook(int, WPARAM, LPARAM);   // 进程内单例 instance_ 转发
  LRESULT OnMouse(LPARAM);
  void UninstallHook();                   // 先 UnhookWindowsHookEx，再清 instance_（反序会在卸钩瞬间丢回调对象）
  HHOOK hook_ = nullptr; HWND mainWnd_ = nullptr; bool active_ = false;
  DWORD lastFire_ = 0;                    // kRelayDedupeMs 内的重复按下忽略（Alt+双击只算一次）
  static RelayService* instance_;
};
```

v2.2.0 有而现在**没有**的三样：闲置定时器（`SetTimer(ID_RELAY_IDLE, kRelayIdleMs)` 曾是武装的前置条件）、
`Touch()`、`Arm()` 里的"建不起定时器就不武装"。无开关之后钩子的收放由外部状态裁决，
"用完忘了关"这个原始担忧不复存在（收起窗口即卸），所以不留任何自动收口。

**职责边界（本轮重构过两轮，别再改回去）**：`RelayService` 只管**机制**（钩子的装/卸/重挂、Alt 判定、去重、投递），
所有**策略与用户反馈**（该不该装、取哪条、贴到哪、贴完提示什么）都在 `AppContext`——
裁决入口是 `AppContext::SyncRelayHook()`，锁屏/注销沿用 `MainWindow` 已有的
`WM_WTSSESSION_CHANGE`/`WM_ENDSESSION` 分支转给 `AppContext`，服务层不留回调、不留 `OnIdleTimeout` 之类的重复入口。
曾经把 `onTrigger`/`onAutoDisarm` 与 `OnIdleTimeout`/`OnSessionLock` 塞进服务层，既与头文件内联定义冲突，
又让"钩子线程 vs 主线程"的边界变糊，已删。

**与 §5.5 点选的三条共用约束 + 一条相反语义**：

| | 点选 §5.5 | 接力 §5.7 |
|---|---|---|
| 钩子返回 | `return 1`（吞掉，保护选区） | **`return 0`（放行，目标必须拿到光标）** |
| `instance_` 转发 / UI 线程内无锁读写状态 / 回调只做"取窗 + `PostMessage`" | 同 | 同 |
| 卸钩时机 | 命中即卸 | 快速模式 + 主窗在屏期间常驻，`Disarm` 才卸；**点选期间不叠挂**（`SyncRelayHook()` 里 `picker_.picking()` 为真即卸） |

降级与生命周期：钩子装不上 → `Arm()` 返回 false，`SyncRelayHook()` 只在状态栏报"接力装不起来（鼠标钩子不可用），
空格粘贴不受影响"并 `LogWarn`，普通粘贴链路不受影响；切普通模式 / 收起主窗 / 最小化 / 点选开始 / 锁屏 / 注销 / 退出
都收敛到 `Disarm`（钩子**绝不能跨会话残留**；解锁 `WTS_SESSION_UNLOCK` 再由 `SyncRelayHook()` 按当前状态装回）。
`AppContext.h` 里 `relay_` 必须声明在 `picker_` **之后**：析构顺序要求先卸接力钩子、且卸钩时主窗仍有效。

> **实现注（v2.2.0 引入，v2.3.0 修订，2026-10-05）**
> - `GetAncestor(..., GA_ROOT)` 而非 `GA_ROOTOWNER`：Excel 模态对话框用 ROOTOWNER 会退到主框架窗，键入落到对话框外。
> - `IsOwnWindow(root)` 按**进程号**比对（`native/Foreground.h:35`），自家四窗（主窗 / monitor / 托盘宿主 / 悬浮气泡）全在里面：
>   "在自己列表上 Alt+点"一律放行不贴，不会把内容打进自己的搜索框。取不到窗口同样放行，绝不吞。

> - Alt 判定用 `GetAsyncKeyState(VK_MENU) & 0x8000`（异步态）；`GetKeyState` 在 `WM_APP` 处理里读的是上一条按键的遗留状态。
> - `PostMessageW` 的 `wParam` 直接带 `HWND`：钩子回调不能阻塞，投递后即返回。目标窗在 `RelayStep` 里再 `IsWindow` 校验（可能已关）。
> - **为什么 `Arm()` 要"每次都重挂"**：低层钩子只有装与卸两个 API，**没有查询接口**。系统按 `LowLevelHooksTimeout`
>   摘钩、或被别的程序卸掉之后，症状只是"Alt+点不灵且不报错"，用户视角就是"功能又没了"。把重挂在 `Arm()` 里做成幂等，
>   `SyncRelayHook()` 的每一次调用都顺带自愈一次；代价是收放窗时各有一次 `Unhook`+`SetWindowsHookEx` 的微秒级空窗，
>   空窗内的 Alt+点只是不接力，点击本身照常送达目标程序（钩子是过滤器，缺席即透传），无副作用。
> - **v2.2.0 那版的七条判据已于 2026-10-05 19:44–20:02 实机走查**（逐条见 `doc/TESTING.md` §2）；
>   **v2.3.0 的无开关作用域已于 2026-10-05 21:49–21:58 走查通过**（六条判据逐条见 `doc/TESTING.md` §2 与 §5）。
>   已证的机制层（钩子放行与取窗、60 ms 落位、兜底热键〔v2.3.0 当时是 `Ctrl+Alt+空格`，v2.3.3 改 `` Alt+` `` 后**该判据需重跑**〕、双击去重）本轮原样复用；被删掉的那几条（藏窗、气泡、闲置 5 min）不再需要证据。
>   **22:12:30–22:15:55 用户自己在真 WPS 表格上跑本版**，日志补上了脚本拍不到的两块：点选起止的卸下/装回（22:13:57.356 卸 → 22:13:59.007 备 → 继续动作）、
>   全灰显期间连 10 次触发动作而不卸钩且新条目上位后同一钩子立刻动作。
>   **⚠ v2.3.1 订正：同一段日志里的"`EXCEL7|工作簿1` 焦点控件上的多轮真表格落点"不成立。** 用户随即报障
>   「`Alt+左键`，没有起效果，粘贴不到 excel 中」「没反应」「能贴出东西，但是每次都是一样的条目」——那些 `Ctrl+V` **一条都没落进单元格**。
>   根因是抬 `Alt` 发生在点击那一刻、`Ctrl+V` 由 60 ms 后的定时器注入且旧批次只有 4 个事件（不含 `Alt↑`），真人按住 `Alt` 时目标实收
>   `Alt+Ctrl+V`（选择性粘贴）→ **日志全绿而表格无变化**；走查压不出它是因为脚本合成的 `Alt` 会自己按时抬起。
>   v2.3.1 把抬键并进 `Ctrl+V` 的**同一个 `SendInput` 批次**并在注入那刻复查异步键态，详见 `doc/PROJECT_STATE.md` §4 坑 #19。
>   **⚠ v2.3.2 再订正：v2.3.1 只修好了一半，而 v2.3.2 那一刀失败了。** 真人手验给出的分界是焦点控件类型——
>   `EXCEL6|`（编辑态）能贴、`EXCEL7|工作簿1`（网格仅选中）贴不进；v2.3.2 据此同时修 keytip 与扫描码两个假设
>   （批次改 `Ctrl↓→Alt↑`、每事件补 `wScan`、删掉点击那一刻的单独 `Alt↑`），结果**双双证伪且连编辑态也失效**。
>   同轮那个不涉及 `Alt` 的对照测试（普通空格粘贴在 WPS 网格里「没成功过」）把结论推到更底层：
>   **注入链在 `EXCEL7` 上从来没成功过**，最大嫌疑是「一次 `SendInput` 投整批」本身（.NET 旧版是 `keybd_event` 四次独立调用）。
>   用户 2026-10-06 裁定**放弃修改、保持现状（未回退）**，全部证据与下一刀方向见坑 #20 与 `CHANGELOG.md` v2.3.2 段。
>   仍未证明的：**真表格单元格落点（现已记为未通过、且已裁定不再追）**、重复 `Alt↑` 的副作用、
>   注销 `WM_ENDSESSION`、`paste_.busy()` 叠贴丢弃、钩子被系统摘除后 `Arm()` 幂等重装的自愈、
>   **`` Alt+` `` 兜底键真人按下能否触发（v2.3.3 只证到 `RegisterHotKey` 返回 TRUE）**。

---

## 6. UI 层设计

### 6.1 消息路由总表（MainWindow）

| 消息 | 处理 |
|---|---|
| `WM_CREATE` | 存 `this` 到 `GWLP_USERDATA`；建 `EDIT` 子窗（子类化 `EditProc`：占位文字/清除叉号自绘、`EM_SETMARGINS`、回车回投 —— §6.7；`WM_SETFONT`）；建 D2D/DWrite 资源；**v2.5.0 起不再恢复持久化几何，直接按默认右缘停靠**；**v2.3.0：此处不装接力钩子**（钩子只由 `AppContext::SyncRelayHook()` 装卸） |
| `WM_SIZE` | `renderTarget_->Resize`；重排 `EDIT`/按钮几何；`ListRenderer::Rebuild`；`InvalidateRect(NULL)` |
| `WM_DPICHANGED` | 采纳 `lParam` 建议矩形 `SetWindowPos`；`SetDpi`；重测所有 `TextLayout`（**v2.5.0 起无持久化几何，采建议矩形即可，无 settings 冲突分支**） |
| `WM_DISPLAYCHANGE` `WM_WTSSESSION_CHANGE` | 校验窗口是否仍在某显示器内（否则回默认停靠）；锁屏时 `picker.Cancel()` |
| `WM_PAINT` | `BeginDraw`→背景→标题栏→工具栏→`PushAxisAlignedClip(视口)`+`SetTransform(0,-scroll)`→行→状态栏→`EndDraw`；`EndDraw` 返回设备丢失 → §6.5 重建 |
| `WM_ERASEBKGND` | `return 1`（D2D 全量覆盖，防闪烁） |
| `WM_GETMINMAXINFO` | `kMinW/kMinH` 约束 |
| `WM_NCHITTEST` | 标题栏区（排除三按钮与模式文字）→ `HTCAPTION` 拖拽；客户区其余 `HTCLIENT` |
| `WM_LBUTTONDOWN/UP` | `HitTest` 分发（§6.2）；双击由 `WM_LBUTTONDBLCLK` 到达（普通模式粘贴） |
| `WM_MOUSEMOVE` | `TrackMouseEvent(TME_LEAVE)` 维护 hover；命中按钮改 `SetCursor(IDI_HAND)` |
| `WM_MOUSELEAVE` | 清 hover 并重绘受影响行 |
| `WM_MOUSEWHEEL` | `GET_WHEEL_DELTA_WPARAM/WHEEL_DELTA * SPI_GETWHEELSCROLLLINES * 行高`，钳制 scroll |
| `WM_KEYDOWN` | `VK_SPACE`：`mode==Quick && focusOwner==List && Selected()` → `DoPaste(sel,true)` 并 `return 0`；`VK_ESCAPE`：点选中→取消，否则不隐藏（契约未定义关闭行为）；`VK_APPS` → `return DefWindowProcW(...)`，由系统合成 `WM_CONTEXTMENU`（`lParam=-1,-1`）。**`VK_F10` 不放行**（2026-10-04 决议：本窗无菜单栏，单按 F10 进系统菜单模式的后果不可见即不可测，只把实机验过的行为上线）；其余按键一律 `return 0` 吞掉 |
| `WM_CHAR` | `focusOwner==List` 时吞掉可打印字符（防无关按键音），仅放行 Space/Esc/Enter |
| `WM_COMMAND` | `EN_CHANGE`（搜索框）→ 重启 300ms 防抖；`EN_KILLFOCUS/EN_SETFOCUS` → 更新 `focusOwner`；自绘按钮命令 ID |
| `WM_CONTEXTMENU` | `picking()` 中直接吞掉（点选期间不弹菜单）。否则 `ShowMainMenu()`：**三项 + 一条分隔线** `TrackPopupMenuEx(TPM_RETURNCMD)`（粘贴模式 / 复制模式 / ─── / 使用帮助，动态文案；v2.2.0 曾插入第 3 项「填表接力」，v2.3.0 随无开关改造删除）。`lParam==-1,-1`（键盘 `VK_APPS`）→ 锚点取列表区左上换算成屏幕坐标。搜索框是**真 `EDIT` 子窗**，它在自己区域内直接收到 `WM_CONTEXTMENU` 并走系统默认（原生编辑菜单，实机 15 项），主窗这条分支碰不到它，因此无需 `OriginalSource` 判定 |
| `WM_TIMER` | 见 §6.3 分派 |
| `WM_APP_SEARCH_ENTER` | `EDIT` 子类窗回投：回车把按键归属交回列表（`focusOwner=List`+`SetFocus(主窗)`） |
| `WM_APP_PASTE_DONE` | `PasteService` 结果回投 → `OnPasteDone(ok)`：`ok` 才 `Store::PasteDone`（灰显/沉底/连贴跳转），随后 `ArmPasteGuard()` 起 1000ms 兜底 |
| `WM_APP_PICK_DONE` | `LlHook` 回投根窗口句柄 → `ProcessPicker::OnPickMessage()`：进程名解析与绑定态写入都在主线程（步骤 9） |
| `WM_APP_RAISE_TOPMOST` | C13 兜底（v2.1.1）：`topmost_` 为真但 `WS_EX_TOPMOST` 位不在时补发一次 `SetWindowPos(HWND_TOPMOST, NOMOVE\|NOSIZE\|NOACTIVATE)`。由 `WM_ACTIVATE` 投递 |
| `WM_APP_RELAY_TRIGGER` | **（v2.2.0 引入，v2.3.0 沿用，C15）** `RelayService` 的 LL 钩子投来 `wParam = 被点中的根窗口` → `AppContext::OnRelayTrigger(target)`：`relay_.armed()` 与 `shuttingDown_` 双闸后 `RelayStep(target)`——取 `Store::RelayNext()`（当前视图 `display_.front()`）贴进该窗。**v2.3.0 起这里不再有 `relay_.Touch()`**：闲置定时器已删，卸下钩子的唯一途径是状态裁决（`SyncRelayHook()`）。消息处理全在主线程，钩子回调里绝不碰剪贴板 |
| `WM_HOTKEY`（`kMsgHotkey` 0x0312） | `wParam==kHotkeyId(1)` → `Ctrl+`` 呼出/收起（**v2.3.0：呼出不再解除接力**，收起/展开本身经由 `ToggleVisibility` 走一次 `SyncRelayHook()`）；`wParam==kHotkeyIdRelay(2)` → `AppContext::OnRelayHotkey(GetForegroundWindow())`（`` Alt+` `` 兜底，v2.3.3 起；此前是 `Ctrl+Alt+空格`。目标＝按键瞬间的前台窗）。两把都在 `RegisterHotkeys()` 里注册，兜底那把先带 `kModNoRepeat` 失败再退回不带；`UnregisterHotkeys()` 按各自的成功标志独立注销 |
| `WM_WTSSESSION_CHANGE` | 值 0x02B1；`WM_CREATE` 里 `WTSRegisterSessionNotification(NOTIFY_FOR_THIS_SESSION)` 才收得到。`wParam==WTS_SESSION_LOCK`（值 1）→ `picker.Cancel()` + `relay_.Disarm(L"会话锁屏")`，光标与钩子绝不跨会话残留（步骤 9）；**`wParam==WTS_SESSION_UNLOCK`（值 2，v2.3.0 新增）→ `ctx_->OnSessionUnlock()` 重新裁决一次**，主窗仍可见且处于快速模式就把接力钩子装回来 |
| `WM_ENDSESSION` | 注销/关机：正在点选同样 `Cancel()`，随后 §7.2 清理链 |
| `WM_ACTIVATE` | 失活且非点选/菜单期间 → 不做处理（`_lastExternalWindow` 在唤起前记录，更可靠）。**激活**（`LOWORD!=WA_INACTIVE`）且 `topmost_` 为真而 `WS_EX_TOPMOST` 位缺失 → `PostMessageW(WM_APP_RAISE_TOPMOST)`：本窗不在前台时系统会丢掉置顶带变更（v2.1.1 实机坐实），而**不能在这条消息里直接改 z-order**（系统处理完 `WM_ACTIVATE` 还会再动一次，同 `PasteTarget.cpp:76` 记过的坑），所以延后一条消息 |
| `WM_CLOSE` | → `AppContext.Exit()`（标题栏 ✕ 即彻底退出，FR-15③） |
| `WM_DESTROY` | `PostQuitMessage(0)` |

Monitor / Tray 窗口只处理各自 2–3 条消息（§5.1、§5.4），其余 `DefWindowProcW`。

### 6.2 命中区域模型

```cpp
enum class HitZone { None, ModeText, BtnMinimize, BtnTopmost, BtnClose, BtnSignature,
                     SearchBox, BtnFilter, BtnClear, BtnReset, BtnPick,
                     RowBody, RowStar, ListBackground, Status };
struct HitResult { HitZone zone; const ClipItem* item = nullptr; size_t displayIndex = 0; };
```
`RowBody` 与 `RowStar` 在 `ListRenderer::rows_` 中预存矩形（数据变更时重建，每帧不重算）；命中顺序：先按钮与标题栏，再列表可见行。

**状态栏带要最先吃掉（v2.1.0 修复）**：`y > ClientH()-kStatusF` 时直接判定为 `BtnSignature`（落在署名矩形内）或 `Status`，
**不再往下算 `contentY`**。原来的缺陷正是这条：列表视口只画到 `ClientH()-kStatusF`，但命中仍按 `contentY` 反算行号，
落在底栏的点击就落到最下面那条上——用户点 `by Mr lin` 结果改了下面条目的收藏。`WM_LBUTTONUP` 里 `Status` 显式 `break`（什么都不做），
`BtnSignature` 调 `OpenProjectPage()`（AC-8 边界裁决见 DESIGN.md §1.2）。

### 6.3 定时器总表

| ID | 宿主 | 间隔 | 语义 | 触发后 |
|---|---|---|---|---|
| `ID_SEARCH` | MainWindow | 300ms | FR-06 防抖 | `Store.ApplySearch(editText)`；单次性（每次输入 `KillTimer+SetTimer`） |
| `ID_READ` | Monitor | 25ms | 读取重试 | `TryRead()`，成功或 6 次后 `KillTimer` |
| `ID_FOCUS_WAIT` | MainWindow | 60ms | 等前台焦点稳定 | `InjectCtrlV()`（v2.3.1 引入、v2.3.2 改序：按需 `Ctrl↑` → `Ctrl↓` → `Alt↑`（恒发）→ `V↓` → `V↑` → `Ctrl↑`，同一批 `SendInput`、每事件带扫描码）→ `onDone` |
| `ID_PASTE_GUARD` | MainWindow | 1000ms | C4 防护兜底 | 清 `isInternalPaste`/`lastPasted` |
| `ID_PICK` | Picker 宿主 | 8000ms | T2 卡死取消 | `picker.Cancel()` |
| `ID_STATUS_HINT` | MainWindow | 3000ms | 点★后条目**留在原位**（v2.4.2 起不再当场离开当前视图），状态栏提示仍需自动消隐 | `AppContext::OnStatusHintTimer()`：清空 `statusHint_` + 重绘 |

`SetTimer` 精度下限约 10–16ms，25ms 档实测可用；不引 `winmm!timeSetEvent`（避免多一个 DLL）。所有定时器 ID 唯一，`WM_TIMER.wParam` 分派。
**v2.3.0 删掉了 `ID_RELAY_IDLE`**（原 5 min 闲置自动解除）：接力改为状态驱动后，"该不该装钩子"每个状态入口都会重新裁决一次，
不需要时间兜底这条收口了。表内因此只剩 6 把。

### 6.4 行测量与布局

```
cardHeight = kRowPad
           + min(contentLayoutHeight, kContentMaxH)          // 主内容固定单行 / ≤18 逻辑px（超出→…，全文见 HoverTip）
           + kRowPad                                          // 2026-10-04 C11：来源标注不上屏，行高不再随它浮动
rowPitch   = cardHeight + kRowGap                            // 卡片间留白
行内列：[序号 kIndexW] [内容 自适应] [时间 kTimeW] [★ kStarW ← 独占整卡高度的最右一列]
```
- 主内容 `IDWriteTextLayout`：宽度 = 内容列宽，高度取 `GetMetrics().height`。**行上限不用 `SetTrimming` 实现**——Win11 26100 的 `dwrite.dll` 实测（`cpp/build-mingw` 探针，2026-10-04）：`LINE_BY_LINE`（枚举值 3）在 format 级与 layout 级一律 `E_INVALIDARG`；`CHARACTER`/`WORD` 无论 `count` 取 1 还是 200 都把文本压成**单行**加省略号，拿不到"最多 N 行"。→ 统一走**手工前缀裁剪**：先按整段排版，`height > kContentMaxH` 时对字符数二分（上界 400 字，不切断代理对），取"高度 ≤ 上限"的最长前缀 + `…` 重排一次，结果进 `LayoutCache`，同时把该行的 `truncated` 置真（只有这些行需要气泡）。两条工具链（MSVC/mingw）与 Win7 同一条路径，无需 `GetLineMetrics`（mingw 10.3 的 `DWRITE_LINE_METRICS`/`DWRITE_TEXT_METRICS` 字段是早期草案布局，只有 `height` 偏移一致，可安全读取）。
- 单行布局：预览 layout 建时 `SetWordWrapping(NO_WRAP)` 且框高 = `kContentMaxH`，绘制用 `D2D1_DRAW_TEXT_OPTIONS_CLIP` 兜住越列溢出。**注意**：`CreateTextLayout` 的高度传 0 时，CLIP 会把整段文字裁没（实机踩过，行内容全空），必须传非 0 框高。
- 预览参与排版的文本：`FoldDisplay(content)` 的前 `kPreviewMeasureChars` 字符（`'\t'`→4 空格、`'\r'`→`'\n'`）；**存储内容不变**，粘贴时原样。
- 缓存：`unordered_map<const ClipItem*, LayoutCache{ComPtr<IDWriteTextLayout> preview, previewH, truncated}>`，随 `StoreEvent::ItemRemoved`/`FullReplaced` 失效；**内容宽度变化（窗口缩放）时整体清空**——截断点与旧行宽绑定，沿用会提前截断或溢出列；上限 512 条，超限整体清空（正常不触发）。
- 序号（FR-17）= `displayIndex+1`，绘制期取，不用静态索引（集合重建后仍正确）。
- **悬浮全文（`HoverTip`）**：`Row::truncated` 为真的行，鼠标停留 `kHoverTipMs` 后由 `WM_TIMER(ID_HOVER_TIP)` 触发，把行卡片客户区矩形换算成屏幕物理像素交给气泡；气泡自绘（GDI `DrawTextW` + `DT_CALCRECT` 量尺寸，超 `kTipMaxH` 再二分前缀），不引 comctl32。滚轮/数据变化/尺寸/DPI 变化/点击/鼠标离开即刻收起并重新武装计时。
- **气泡落点（v2.1.0 用户指定）**：改到**该条目上方**、并右移 `kTipOffsetChars = 3` 个字符宽度（字宽用 `GetTextExtentPoint32W(dc, L"中", 1)` 现场量，随系统字体与 DPI 走，不写死像素）。锚点：`x = rowScreen.left + 3×字宽`、`y = rowScreen.top - gap - 气泡高`；上方放不下（越过工作区顶）时翻到行下方并按工作区底夹紧。类名 `SuperClipHoverTip`、窗口标题为空串，QA 只能按类名找窗。
- **选中行滚入视口（v2.1.0 C14 配套）**：`MainWindow::ScrollSelectionIntoView()`——快速模式下 `OnStoreChanged` 之后把选中行滚进来（`row->card.top < scrollY_` 上翻，`bottom > scrollY_+viewH` 下翻，随后 `ClampScroll`）。不补这一步，钉到第一行的动作在列表已往下滚时会把高亮留在视口外，用户看到"没选中任何东西"。
- **置顶要"请求 + 断言"两次（v2.1.1，C13 实机订正）**：`Create()` 里 `ShowWindow`+`UpdateWindow` 之后再补一次 `SetWindowPos(topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, NOMOVE|NOSIZE|NOACTIVATE)`；`WM_ACTIVATE` 里若被激活且位仍缺失，`PostMessageW(WM_APP_RAISE_TOPMOST)` 延后补发。根因见 `doc/PROJECT_STATE.md` §4 坑 #12：**本窗不在前台时系统静默丢弃置顶带变更**（返回 TRUE、`gle=0`、位不落），重试与延时都无效。QA 判据因此只读 `GWL_EXSTYLE & WS_EX_TOPMOST`，不读 API 返回值。**v2.1.2 补上兜底分支的实机证据**（17:41）：后台启动的实例正是丢带态（`0x40000`），外部 `BringWindowToTop` 与跨进程 `SetWindowPos(HWND_TOPMOST)` 都返回 TRUE 而位不落，本窗成为前台（`fg==hwnd`）的瞬间位仍 0，**800 ms 后由 `WM_APP_RAISE_TOPMOST` 补发成 `0x40008` 并保持**——即"延后一条消息"是必需的，且从进程外部无法代劳。

- **实现注（步骤 10 前置的四处 UI 决议，2026-10-04）**：① `Row::star` = `Rect(starLeft, rowTop, kStarW, cardHeight)`，整列都是命中区（比原来的 22×22 角标好点），字形矩形按 `(列高 - kStarW)/2` 下移后绘制；`Row::favorite` 字段与"收藏区/普通区分隔线"随 C10 一并删除（`Theme::Separator`/`kSeparatorHex` 已无使用者，删）。② 来源标注 `kLabelHex` 由 `0x1E88E5` 改浅为 `0x5B9BD5`，并新增 `labelPasted_`（`BlendOver(label, card, kPastedAlpha)`）——原先灰显行的标注仍是满色，比正文还抢眼。**该决议已于同日 C11 推翻：标注整体不再上屏，这两个画刷与 `fmtLabel_`/`kLabelH`/`kFontLabel` 一并删除，只留下方的搜索命中。**③ 收藏过滤规则落在 `Store::RebuildDisplay()` 的**第一行**（`filter_ != Favorite && item.isFavorite → continue`），数组分区不变式与 `Boundary()`/`EnforceLimit()`/`MoveToBack` 一律不动；集合原始顺序改由新增的 `Store::Collection()` 观测（单测用它断言分区不变式，`Display()` 只用于断言视图规则，见 §9.4-13）。④ C11 的删除范围只在绘制侧：`ListRenderer::Measure()` 不再建标注 layout、`Rebuild()` 的 `cardH` 去掉 `labelH` 项、`Draw()` 去掉那一次 `DrawTextLayout`；`ClipItem::SourceLabel()`、`foldLabel`、`RebuildDisplay()` 里 `ContainsFolded(item.foldLabel, …)` 与 `SourceRow/SourceCol` 的读写全部保留（用户补充口径："不显示，但仍参与搜索"）。

### 6.5 设备丢失与资源分层

| 层 | 对象 | 处理 |
|---|---|---|
| 工厂级 | `ID2D1Factory`、`IDWriteFactory`、4×`IDWriteTextFormat`、geometry、`IDWriteInlineObject`、`IDWriteNumberSubstitution` | 进程存活期复用，DPI 不变时不重建 |
| 目标级 | `ID2D1HwndRenderTarget`、`ID2D1SolidColorBrush`、`ID2D1Layer`（若用）、`IDWriteTextLayout` | `EndDraw` 返回 `DXGI_ERROR_DEVICE_REMOVED/RESET/RESTARTED/VCORRUPTED` 时重建；`CheckWindowState(OCCLUDED)` 期间跳过绘制，不重建 |

重建流程：释放目标级 → `CreateHwndRenderTarget(新 DPI)` → 重建画刷 → `InvalidateRect(NULL)` 全量重绘。若硬件失败 → 以 `D2D1_RENDER_TARGET_TYPE_SOFTWARE`（WARP）重试一次（RDP 会话典型路径）；再失败 → 写日志 + `MessageBoxW` 提示后退出（唯一"不可降级"故障）。

### 6.6 绘制细则

- **描边锐利**：1px 线与矩形在 `D2D1_ANTIALIAS_MODE_ALIASED` 下绘制，坐标对齐 `+0.5f`；圆角卡片用 `FillRoundedRectangle`（`radiusX/Y=6` 缩放值）。
- **文本**：`D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE`；灰显**不用** `PushLayer`（会强制灰度 AA），改为预混合：`blend(fg, bg, 0.55)` = `bg + (fg-bg)*0.55` 逐通道，缓存 5 组颜色（normal/muted/pastedLabel/…）。
- **高对比度**：`SPI_GETHIGHCONTRAST` 开启时 → 底色/前景/描边取 `GetSysColor(COLOR_WINDOW/BTEXT/WINDOWFRAME/HIGHLIGHT)`，取消预混合灰显（改纯灰字），保留描边加粗 1px。
- **几何图标**（不用字形）：收起 = 向下折线 + 底线；悬浮 = **斜图钉**（针头圆偏左上 + 45° 针身 + 底部短横杠；未置顶灰描边，置顶填 accent 色并在下方加 accent 下划线，v2.1.0 改形，见 DESIGN.md §5.2）；关闭 = 两条对角线；靶心 = 同心两圆 + 十字。每个 `ID2D1PathGeometry` 建一次复用；按钮 hover 加浅色圆底。★/☆ 用字形（雅黑含该字符）。
- **标题栏应用图标**（v2.1.0）：唯一走位图的路径——`LoadImageW(IMAGE_ICON, px, LR_SHARED)` 取按 DPI 选档的 HICON，`GetIconInfo`+`GetDIBits` 拷出预乘 BGRA，`ID2D1RenderTarget::CreateBitmap` 建 `ID2D1Bitmap`，`DrawBitmap` 进 18×18 DIP 框（1:1 设备像素，不重采样）。位图属**目标级**资源：设备丢失时随 `rt_` 一起弃（`ReleaseTargetLevel()`），DPI 变了重取（`titleIconDpi_` 记账）。`LR_SHARED` 的句柄归系统缓存，**不得 `DestroyIcon`**。取不到像素就只画文字并写一条 warn，不影响其他绘制。
- **状态栏署名可点**（v2.1.0）：`by Mr lin` 单击 → `ShellExecuteW("open", kProjectUrl)`；可点矩形宽度用 `IDWriteFactory::CreateTextLayout` 实测（mingw 的 `ID2D1RenderTarget` 没有 `CreateTextLayout` 包装，测量必须走工厂）。
- **绑定状态色**：靶心红 `#E53935` = 未绑定，绿 `#43A047` = 已绑定（校验 `IsWindow` 后才绿，R5）。

### 6.7 焦点与输入归属

```cpp
enum class FocusOwner { List, SearchEdit };   // EDIT 获焦经 EN_SETFOCUS/EN_KILLFOCUS 更新
```
- `focusOwner==List`：Space→粘贴（快速）；双击→粘贴（普通）；滚轮→滚动。
- `focusOwner==SearchEdit`：所有按键交原生；Space 输入空格（边界条件，原文 §13 明示）；`Enter` = 让焦回列表。
- 唤起主窗后 `SetFocus(mainWnd)` 并把焦点标记为 `List`（避免抢走 `EDIT` 内容）。点行（`RowBody`）同样让焦给列表；点星标只切收藏，不抢焦点（便于收藏后继续输入）。
- `EDIT` 吞掉 `VK_RETURN`，父窗收不到 → 子类化里在 `WM_CHAR` 命中回车时 `PostMessage(父, WM_APP+5)`，父窗据此让焦回列表。

### 6.8 控件清单

| 控件 | 实现 | 备注 |
|---|---|---|
| 搜索框 | 真 `EDIT`（`WS_CHILD\|WS_VISIBLE\|ES_AUTOHSCROLL\|WS_BORDER`） | IME 完好；占位文字与**清除叉号都在 `EditProc` 的 `WM_PAINT` 自绘**（`EDIT` 永远盖在父窗之上，父窗画的会被文字盖掉；原记 `EM_SETCUEBANNER` 与落码不符，2026-10-05 订正）；框内有字时右端出现 ✕（16 DIP、距右内缘 6 DIP），`EM_SETMARGINS(EC_RIGHTMARGIN)` 给文字留出叉号位，`WM_LBUTTONUP` 命中叉 → `SetWindowTextW(L"")` 清空，之后由原生 `EN_CHANGE` 接 300ms 防抖；`WM_SETCURSOR` 在叉区换手型；`WM_CTLCOLOR*` 父窗返回白底画刷 |
| 筛选 | 自绘 `全部▾` 按钮 + `TrackPopupMenuEx` 四项 + 当前项打勾 | 免 `COMBOBOX` 主题割裂（ADR）。按钮文字随当前值变（`全部/文本/表格/收藏 ▾`，按钮宽 60 逻辑px 放不下"表格单元格"，菜单项用全称）；主窗是 `WS_POPUP` 非激活窗，弹出前 `SetForegroundWindow`、返回后 `PostMessage(WM_NULL)`，否则点窗口外菜单不消失 |
| 清除/复位/靶心/标题栏按钮 | 全自绘 + `HitZone` | 无子 HWND，减少 NC 处理。**清除**（2026-10-04 C12）只删非收藏区、无确认框，删完在状态栏给 3s 提示"已清除 N 条，收藏 M 条永久保留"（复用 `ID_STATUS_HINT`）——收藏在【全部】视图本就不可见，不提示会看起来按了没反应 |
| 置顶 | `MainWindow::topmost_` **默认 true**，`DockToWorkArea` 启动即 `HWND_TOPMOST`；★ 按钮切 `SetWindowPos(HWND_TOPMOST/HWND_NOTOPMOST)`，开启态在图标下画青色下划线 | 2026-10-04 用户指定"应用打开默认浮于各窗口最上层"（原 `false` 是步骤 8 遗留）。`Topmost` 落盘要等步骤 10 `SettingsService`，本轮固定"每次启动都开"；**v2.5.0 起设置不再持久化，`Topmost` 恒为启动默认开（该落盘计划已移除，见 doc/DESIGN.md §8.3）** |
| 列表 | 全自绘 | 无 UIA（N1） |
| 标题栏应用图标 | `ID2D1Bitmap`（GDI 取像素） | v2.1.0；纯装饰、不参与 `HitZone`，模式文字起点因此改为 `kModeLeft = kPad + 18 + 6`，绘制与命中同用一个常量 |
| 状态栏署名 `by Mr lin` | 自绘文字 + `HitZone::BtnSignature` | v2.1.0 起单击 `ShellExecuteW("open", kProjectUrl)`；矩形按 DWrite 实度量宽，右侧贴 `ClientW()-kPad`。底栏其余区域是 `Status`，命中即吞掉、不改任何状态（见 §6.2） |
| 主窗右键菜单 | `MainWindow::ShowMainMenu()`：`CreatePopupMenu` **三项 + 一条分隔线**（粘贴模式 / 复制模式 / ─── / 使用帮助）+ `TrackPopupMenuEx(TPM_RETURNCMD\|TPM_LEFTALIGN\|TPM_TOPALIGN)` | 步骤 11（2026-10-04）建三项。**"严格三项"是当时的用户裁定**：置顶已有标题栏 ★，清除/复位带确认链、误触代价与开关不对等，不进菜单。v2.2.0（2026-10-05）曾加第 3 项「填表接力」（理由是"开关型动作要有隐蔽的关闭入口"），**v2.3.0 随无开关改造删除**——接力不再是开关，菜单里再放一项就是一个点了没用的条目。项文字按当前状态生成并写明点击后果（"粘贴模式：普通（点此切到快速）"），取消即零改动。与筛选菜单同一套 `SetForegroundWindow` 前置 + `PostMessage(WM_NULL)` 收尾（主窗是 `WS_POPUP` 非激活窗）。复制模式点击后仅改内存态、**v2.5.0 起不再落盘**（下次启动仍回到「一般」，见 doc/DESIGN.md §8.3）。**走查判据回到"4 行含分隔线"**；v2.2.0 那次的实拍（`cpp/build-mingw/relay-menu-tight.png`，19:47，5 行含接力项）已作废，v2.3.0 需重拍（`doc/TESTING.md` §5 判据 6） |
| 帮助窗 | 独立无边框 `WS_POPUP`（`WS_EX_TOOLWINDOW\|TOPMOST`、owner=主窗），D2D 绘制 10 步 + 上一步/下一步/关闭按钮 | 入口：右键菜单「使用帮助」。420×300 逻辑px，贴主窗**左侧**（放不下回落右侧/居中，再 `FitRectToDesktop`）。模态用 `EnableWindow(主窗, FALSE)`，**不起嵌套消息循环**；首末位钳住且按钮禁用，重开回 `1 / 9`；`Esc`/关闭 还原主窗并 `SetFocus`。步骤 11 实机已验翻页（鼠标与 `VK_RIGHT`）、钳位、模态、还原；DPI 150%/200% 排版未验 |

---

## 7. 生命周期

### 7.1 启动时序
```
wWinMain
 ├─ SetDpiAwarenessBestEffort()  (Win10 1607+ / 回退 SetProcessDPIAware)
 ├─ CreateMutexW(Local\) → 已存在则 ActivateExisting + return 0
 ├─ CoInitializeEx(COINIT_APARTMENTTHREADED)
 ├─ SetUnhandledExceptionFilter / set_terminate
 ├─ AppContext.Init:
 │    StorageService.EnsureDir → Store.LoadFromDisk → ApplyOrder → Save
 │    TrayService.Create → ClipboardMonitor.Create(+AddListener) → ProcessPicker 就绪
 │    MainWindow.Create(WM_CREATE 内建 D2D/DWrite；失败即弹框退出)
 │    RegisterHotKey → 位置：右缘垂直居中（v2.5.0 起不再恢复持久化几何）
 └─ while(GetMessageW) { Translate; Dispatch; }  → AppContext.Shutdown
```

### 7.2 退出清理链（顺序固定，`Exit()` 与崩溃过滤器共用）
```
shuttingDown_ = true                       // 先置位：其后所有消息回调一律早退
→ ProcessPicker::Cancel()                  // 卸点选钩子 + SPI_SETCURSORS 复位光标
→ RelayService::Disarm(L"退出清理")         // 卸接力钩（v2.2.0 引入；v2.3.0 起这是纯状态清理，无武装概念）
→ KillTimer × 5                            // ID_PICK / ID_PASTE_GUARD / ID_SEARCH / ID_STATUS_HINT / ID_READ(monitor)
→ ClipboardMonitor.Destroy()               // RemoveClipboardFormatListener → DestroyWindow
→ TrayService.Destroy()                    // NIM_DELETE → DestroyWindow
→ Store::SaveToDisk()                      // 失败静默（变更即落盘，这里只是兜底）
→ MainWindow::Destroy()                    // v2.5.0 起不再保存窗口几何（设置不持久化）；主窗内部注销两把热键
→ PostQuitMessage → ReleaseMutex/CloseHandle → CoUninitialize
```
**顺序依据**：两个钩子必须在**主窗销毁之前**卸掉——LL 钩子的回调投给 `mainWnd_`，窗先没了指针就悬空；
而 `SaveToDisk` 排在监听与托盘销毁之后，因为那两者不再改列表。注销热键在 `MainWindow::Destroy()` 里，
不是独立一步（`UnregisterHotkeys()` 按 `hotkeyRegistered_` / `relayHotkeyRegistered_` 两个标志各自判断）。

`WM_ENDSESSION`（注销/关机）与 `WM_WTSSESSION_CHANGE`（锁屏）走同一条解除链。
**接力钩子在这两条会话消息里一律先解除**（v2.2.0 起如此）：LL 钩子跨会话残留会让下一次登录的桌面吞掉鼠标输入。
**v2.3.0 补上另一半**：`WTS_SESSION_UNLOCK` 到达时走 `AppContext::OnSessionUnlock()` → `SyncRelayHook()` 重新裁决，
解锁后若主窗仍可见且处于快速模式，Alt+左键自动恢复可用——用户不需要任何额外动作。
`AppContext.h` 里 `relay_` 声明在 `picker_` 之后，靠析构顺序保证"先卸接力钩、且卸钩时主窗仍有效"。

---

## 8. 错误处理与降级矩阵

| 故障 | 检测 | 用户可见 | 恢复策略 |
|---|---|---|---|
| D2D/DWrite 不可用（Win7 无 SP1 / RDP WARP 也失败） | 工厂/目标创建 `HRESULT` | 一次性弹框"需 Windows 7 SP1 以上"，退出 | 软件渲染重试一次 |
| 设备丢失 | `EndDraw` 返回 DXGI 错误 | 无感 | 重建目标级资源，下一帧正常 |
| `AddClipboardFormatListener` 失败 | `FALSE` | 无（状态栏不显示） | 继续运行；手动粘贴可用，仅不自动入列 |
| `RegisterHotKey` 失败（被占用） | `FALSE` | **T5** 托盘气泡"快捷键 Ctrl+` 被其他程序占用" | 无热键，可用托盘双击/Alt+Tab 打开 |
| `NIM_ADD` 失败 | `FALSE` | 无 | 程序可用，仅无托盘图标 |
| `SetWindowsHookEx` 失败 | `nullptr` | 状态栏提示"点选不可用，已绑定上次窗口" | 绑 `_lastExternalWindow` |
| `SetSystemCursor` 失败 | `FALSE` | 无 | 点选继续（普通光标），仅提示语区分 |
| 剪贴板被占用（读） | `OpenClipboard==FALSE` | 无 | 6×25ms 重试后放弃本次 |
| 剪贴板被占用（写） | 同上 | 无 | 3×25ms 重试，失败则 `onDone(false)`，条目不变灰 |
| `SetClipboardData` 失败 | `nullptr` | 无 | `GlobalFree` 后返回，不发键（避免粘旧内容） |
| 前台切换三段全失败 | `GetForegroundWindow()!=target` | 无 | 仍在当前前台发 `Ctrl+V`（原行为），Warn |
| 提权目标窗口 | 无法判定 | 无 | 已知边界 N5，失败静默 |
| `%AppData%` 不可写 | 写盘失败 | 首次失败时状态栏红字"历史无法保存" | 内存态继续；每 20 次保存重试一次探测 |
| `history.json` 损坏 | 解析异常 | 首次弹一次"历史文件已重置"，其后静默 | 能解析的条目保留，坏条目跳过（比原版整体丢弃更好） |
| 保存的窗口位置在已拔掉的显示器上 | `MonitorFromWindow` 无匹配 | 无 | 回默认右缘停靠 |
| 收藏项被快速粘贴 | — | — | **C8**：沉到收藏区末尾，不破坏分区不变式 |
| Explorer 重启 | `TaskbarCreated` | 无 | 重挂托盘图标 |
| 会话锁屏/关机 | `WM_WTSSESSION_CHANGE`/`WM_ENDSESSION` | — | 取消点选、复位光标、走清理链 |
| 单条内容 > 256K | 长度判定 | 状态栏短提示（可选） | **T3** 截断入列 + Warn 日志 |

**原则**：UI 永不因服务失败崩溃；崩溃必留日志且必复位系统状态。

---

## 9. 测试方案

### 9.1 结构与构建
`tests/test_main.cpp` 自带极简断言器（`CHECK/CHECK_EQ` + 计数汇总），**不引 Catch2 也不引任何第三方**（守住 §1.1 的零依赖红线）；目标 `sc_tests`（`add_executable(sc_tests ...)`，不链 d2d/dwrite）。被测范围：`core/` 全部纯函数 + `Store`（`StorageService` 以临时目录注入）。`ui/` 不写单测（无头环境不可行）→ §9.5 手测脚本。

> 构建与运行路径（2026-10-05 更新）：`bash build-tests.sh` 走项目专属 WSL 发行版 `superclip` 的 mingw 交叉构建（内部即 `cmake --build`，清单只有 `CMakeLists.txt` 一份），产物 `sc_tests.exe` 拷到 Windows 本机实跑（无 wine）。当前规模 **40 例 / 219 断言 / 0 失败**（v2.2.0 加 §9.4-17；v2.3.0 把该例从 8 项断言改写成 6 项，见 §9.4-17；**v2.4.2 打破 v2.3.1–v2.4.1 六轮的「42 例 / 228 断言」同数**——改 `Store::ToggleFavorite()`/`SetFilter()` 行为，新增 §9.4-18「点★暂留到下次刷新」并改写 §9.4-02/-05/-08/-13 的旧判据，净 +1 例 / +18 断言，见 §9.4-18；**v2.4.3 再加 §9.4-19「表格批次不破坏收藏分区」（P0-1 回归）**，净 +1 例 / +18 断言，见 §9.4-19；**v2.5.0 按用户决议彻底移除设置持久化，删除 §9.6 `Settings` 往返组 4 例，净 −4 例 / −45 断言**；最近实跑 2026-10-06，产物 `build-mingw/SuperClip.exe` 3,639,549 B / MD5 `F4ED65444F798678E36512DDBCD7FD92`）。

### 9.2 TableParser（13 例，对齐原 §12）
| # | 输入 | 断言 |
|---|---|---|
| 1 | `L"a\tb"` | `IsTable==true`，2 格：(1,1)a (1,2)b |
| 2 | `L"a\tb\r\nc\td"` | 4 格，行 1/2、列 1/2 顺序正确 |
| 3 | `L"a\t\tc"` | 2 格：(1,1)a、(1,3)c —— 空单元格跳过且**列号不前移** |
| 4 | `L"\n\na\n\n"` | 无 `\t` + `CopyMode::Normal` → 非表格，单条 Text |
| 5 | `L"a\tb\n"` | 尾换行被 trim，2 格 |
| 6 | `L"a\tb\rc\td"` | `\r` 当换行，4 格 |
| 7 | `L"1\t2\t3"` | 3 格，列号 1/2/3 |
| 8 | `L"行1\n行2\n行3"` + `CopyMode::TableSingleColumn` | 3 格，各 (r,1) |
| 9 | `L"行1\n行2\n行3"` + `CopyMode::Normal` | 非表格，单条含换行 |
| 10 | `L"\t\t"` | 表格但 0 格 → `AddFromClipboard` 不产生任何条目 |
| 11 | `L"a\tb\n\nc\td"` | 空行跳过，行号仍 1 与 3 |
| 12 | `L"x"` | 非表格 |
| 13 | 首尾空格 `L" a \t b "` | 内容原样保留（不 trim 单元格） |

### 9.3 哈希与标签（8 例）
| # | 输入 | 期望 |
|---|---|---|
| 1 | `L"abc"` | `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` |
| 2 | `L""` | `L""`（**空内容不计算**，与原 `ComputeHash` 一致） |
| 3 | `L"中文测试"` | 64 位小写 hex，稳定且 ≠ ASCII 同长相 |
| 4 | `L"abc"` vs `L"abd"` | 哈希不同 |
| 5 | 1MB 长文本 | 64 位 hex、耗时 < 5ms |
| 6 | `ClipItem{Text}` | `SourceLabel()==L""` |
| 7 | `ClipItem{TableCell,2,3}` | `L"来自表格：第 2 行 第 3 列"` |
| 8 | `TableCell` 但 row/col=0 | `L""`（防御） |

### 9.4 Store 不变式（现 19 例；1–12 是 §11 的规划口径，13–19 随用户决议追加）
1 重复复制 → 总数不变、位置到新内容位、时间戳更新、`IsFavorite` 迁移。
2 已有 3 收藏 + 新复制 → 插入下标 == 3。
3 非收藏 501 → 淘汰**末尾**一项（C7 回归用例，锁死"不删最新"）。
4 收藏 600 条不淘汰（`EnforceLimit` 跳过收藏）。
5 `ToggleFavorite` → `stable_partition` 后各区相对顺序不变。
6 表格 4 格整块插入后顺序为 (1,1)(1,2)(2,1)(2,2)，不倒序。
7 快速沉底（非收藏）→ 位置 == 末尾，`isPasted==true`。
8 快速沉底（**收藏**）→ 落在收藏区末尾，分区不变式仍成立（C8）。
9 沉底后重启（Save→Load）顺序一致（AC-3 逻辑层）。
10 `Reset` → 全部 `isPasted=false`，收藏/非收藏各自时间降序（C5）。
11 搜索 `400` 命中 `L"４００"`（全角折叠）；大小写不敏感命中 `ABC`/`abc`。
12 过滤 `TableCell` + 关键词组合 → `Display() ⊆ items_` 且顺序一致；结果变化后原选中项若仍可见则保持选中（契约 §4.6 原地同步语义）。
13 收藏仅在【收藏】视图显示，`全部/文本/表格单元格` 三视图一律剔除（C10，2026-10-04 决议追加）。
14 来源标注**不上屏但仍参与搜索**（C11）。
15 `ClearAll()` 只删非收藏区、收藏原序保留并落盘、返回实际删除条数（C12）。
16 Quick 模式 `AnchorQuickSelection()` 的五种触发（入列 / 切过滤 / 搜索 / `Reset` / 点选绑定后）都把选中位钉到
   `display_.front()`；普通模式一律不动（C14）。
17 **接力（C15，v2.2.0 引入 / v2.3.0 改无开关）**：`AddFromClipboard(L"a\tb\tc")` 拆三条后 `RelayNext()` 依次返回 a→b→c；
   全部贴完返回 `nullptr`（绝不回头重贴）。**v2.3.0 追加两条"所见即所贴"**：`ApplySearch(L"banana")` 后 `RelayNext()`
   返回匹配区第一行；切到非【全部】过滤视图（清空搜索词后 `SetFilter(Text)`）同样按当前显示区算。
   v2.2.0 那版共 8 项断言；本轮删掉锁 `RelayArmable()` 准入的断言、补进锁"所见即所贴"的断言，净余 **6 项**
   （全库总断言 230→228，与 21:26 实跑一致）。
   **单测只锁"取哪条 / 何时停"**——"该不该装钩子"是 UI 状态裁决（`SyncRelayHook` 读 `Store` 的模式与主窗可见性），
   钩子、Alt 判定、注入与夺前台链路都不在逻辑层能力范围内，实机判据见 `doc/TESTING.md` §5。
18 **点★暂留到下次刷新（v2.4.2，2026-10-06 用户决议）**，三条子场景：
   ① 在【全部】视图对一条点★ → 该条**当场不离开** `Display()`（仍在原位）、`IsFavorite` 已翻转；
      随后任一刷新（`SetFilter` / `ApplySearch` / `AddFromClipboard` / `PasteDone` / `ClearAll` / `Reset` / `LoadFromDisk`）才按新状态把它移出【全部】；
   ② 取消收藏对称：点★（取消）后同样当场不移出、下一次刷新才回位；
   ③ **切视图与"同值过滤项"都算一次刷新**——`SetFilter(同值)` 不再提前返回（v2.4.2 去掉该短路），点★后点【全部】也会重算。
   **代价（既定）**：点★到下次刷新之间 `display_` 与 `filter_` **允许暂时不自洽**（见 `doc/DESIGN.md` §0.1 C10）。
   本用例锁的是**逻辑层的 `display_` 不变式**；"点★后那一行在屏幕上留在原位、星标立刻翻转"属 D2D 自绘层的
   **像素/交互表现，逻辑层覆盖不到**，须用户真机目视确认（见 `doc/TESTING.md` §5 与 §2 v2.4.2 行）。
19 **表格批次不破坏收藏分区（v2.4.3，P0-1 回归）**：`AddFromClipboard` 整块插入 `fresh` 后**必须补 `ApplyOrder()`**。
   根因：表格逐格去重时，若某格命中历史收藏，`TakeFavoriteIfDuplicate` 会把 `isFavorite` 迁回新条（FR-02），
   而整批落位按**单元格顺序**，收藏项未必排在最前 → 一旦它落进非收藏区中间，`Boundary()`（＝收藏数量）失真，
   此后 `ClearAll()` 按该数量抹尾就会**误删收藏**（违反 C12「收藏永久保留」）。
   用例：集合 `[x(收藏) | plain]` 上再复制 `L"y\tx"` → 断言 `Collection()` 仍是 `[x(收藏), 非收藏, 非收藏]`、
   `ClearAll()` 只删 2 条且 `x` 仍在；再落盘重载与 `Reset()` 各验一次分区仍成立。
   **负向对照**：把 `ApplyOrder()` 注释掉后本用例 **8 项转红**（收藏真的被删），证其为有效回归闸。

### 9.5 实机验收脚本（UI/粘贴不可单测部分）
```
AC-1 Excel 复制 A1:B2 → 4 条带行列标注；含空单元格列号不前移
AC-2 记事本目标：普通双击粘到光标处；快速空格粘贴，条目沉底变灰
AC-3 复制 20 条 + 收藏 2 条 + 粘贴 3 条 → 退出 → 重启：内容/★/灰显/顺序逐项比对
AC-4 悬浮开→其他程序全屏窗口仍压在其上；再点恢复
AC-5 过滤"表格"后序号连续（1..n），非原集合序号
AC-6 目标为 Chrome 输入框 / Excel 单元格 / 微信输入框 三类命中
AC-7 干净 VM（未装 .NET、未装 VC++ 运行库）双击 exe → 常驻托盘、热键可用
AC-8 防火墙出站规则拦截 SuperClip.exe（TCP/UDP 全禁）+ 运行 30 分钟复制粘贴 100 次 → 无拦截日志
AC-9 （v2.2.0 引入、v2.3.0 改无开关、v2.3.1 修注入批次）快速模式 + 主窗在屏时 Alt+左键点外部输入框 → 贴当前视图第一行、该条沉底变灰、
      新复制的条目自动上位继续贴；切回普通模式/收起主窗/锁屏/退出 四种途径下钩子均已卸下
      ⚠ v2.3.1：抬 Alt/Ctrl 并入 Ctrl+V 的同一个 SendInput 批次（旧写法下真人按住 Alt 时目标收到 Alt+Ctrl+V，贴不进去）；
      ⚠ v2.3.2：批次改 Ctrl↓→Alt↑ + 每事件补扫描码 + 删掉点击时单独 Alt↑ —— 真人手验**失败**，两个假设双双证伪，
        且连 v2.3.1 唯一能成功的编辑态（EXCEL6）也失效；对照测试证明**注入链在 WPS 网格（EXCEL7）上从来没成功过**。
        用户 2026-10-06 裁定放弃修改、保持现状（未回退）。"内容真的落进单元格"这条**记为未通过**，见 doc/PROJECT_STATE.md §4 坑 #20
      ⚠ v2.3.3：兜底键 Ctrl+Alt+空格 → Alt+`（只改键位，产品逻辑未动）；注册已实测成功，真人按下能否触发未实测
额外：RDP 会话启动（WARP 路径）、150%/200% DPI、拔副显示器重启、高对比度主题、
      Explorer 被 kill 后托盘自愈、点选中途锁屏（光标不残留）、任务管理器观察 2 小时句柄不增
```

### 9.6 SettingsService（步骤 10 新增；v2.5.0 已移除）
v2.5.0 按用户决议彻底移除设置持久化（见 `doc/DESIGN.md` §8.3），该组 4 例（G01 .NET 文件读入并逐字节写回 / G02 全字段往返 / G03 缺失损坏回落默认 / G04 越界与类型不符按字段作废）随 `Settings` 模块一并删除，`test_main.cpp` 已无该组，总用例 44 → 40（219 断言）。

---

## 10. 构建与发布

### 10.1 CMake（要点）

> **权威来源是 `cpp/CMakeLists.txt` 本身**，本节只是要点，不再逐行照抄（历史上正是"文档抄一份、脚本抄一份、CMake 一份"三份不一致，才让主构建断链无人察觉；见 `doc/审计核实与整改清单.md` §1）。
> 交叉构建与 MSVC 构建**共用同一份清单**：`bash build-tests.sh` 内部就是 `cmake --build`。

```cmake
cmake_minimum_required(VERSION 3.20)
project(SuperClip LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # /MT
add_executable(SuperClip WIN32 ${SC_APP_SOURCES})                          # 源清单单一来源
target_compile_features(SuperClip PRIVATE cxx_std_20)
target_compile_definitions(SuperClip PRIVATE UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
target_compile_options(SuperClip PRIVATE /W4 /permissive- /utf-8 /EHsc /sdl /guard:cf $<$<CONFIG:Release>:/O2 /GL>)
target_link_options(SuperClip PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup $<$<CONFIG:Release>:/LTCG>)
target_link_libraries(SuperClip PRIVATE
  user32 kernel32 shell32 gdi32 advapi32 ole32 oleaut32 uuid bcrypt d2d1 dwrite wtsapi32)
#   uuid：FOLDERID_* 定义（不产生导入表项）  wtsapi32：WTSRegisterSessionNotification（锁屏取消点选）
#   dwmapi 不在此列：由 src/native/SystemInfo.cpp 走 LoadLibraryW 动态加载
```

DPI 感知由 `src/res/app.manifest` 内嵌提供，**不设 `VS_DPI_AWARE`**（二者同时存在会冲突）。
**零第三方依赖**：JSON 为自研极简实现（`src/core/Json.h/.cpp`），单测用 `tests/test_main.cpp` 自带的极简断言器（`CHECK/CHECK_EQ` + 计数汇总），不引 RapidJSON 也不引 Catch2。

### 10.2 资源（`res/app.rc` + manifest）
- `101 ICON "SuperClip.ico"`（多尺寸 16/24/32/48/256，256 档为 PNG 压缩；托盘按 `SM_CXSMICON`、窗口类按 `SM_CXICON` 向系统要档）。
  **数字 ID 必须与 `Config.h::kIconIdApp`（101）一致**：`TrayService`/`MainWindow` 走 `MAKEINTRESOURCEW(kIconIdApp)` 取图标，ID 对不上只会静默回退系统图标。
- `VERSIONINFO`：`FILEVERSION`/`PRODUCTVERSION` 与 `Config.h::kVersionText` 同号（`PackageRelease.bat` 出包前用 `findstr` 校验，不一致拒绝打包）；`StringFileInfo`（`080404b0`）填 `ProductName=SuperClip 超级剪贴板`、`FileDescription`、`CompanyName`、`LegalCopyright`，右键属性可见（继承原 csproj 元数据要求）。
- **`#pragma code_page(65001)` 必须是 `app.rc` 的第一条指令**（v2.3.4 起，修坑 #15）：本文件是**无 BOM 的 UTF-8**，
  没有这条 pragma 时 windres 与 `rc.exe` 会按**构建机的 ANSI 代码页**逐字节读入再把每个字节宽化成 UTF-16。
  中文机器（CP936）恰好能把 `超级剪贴板` 的 15 字节配回 5 个汉字、"看起来是对的"；本项目交叉构建走 WSL
  （locale C/POSIX → CP1252），于是资源里存的是 `è¶…çº§å‰ªè´´æ…`，属性面板/任务管理器的「文件说明」与
  Win10/11 toast 的标题位（取 `FileDescription`）全部显示成乱码。**判据只能是从产物里按码点读回 `VersionInfo`**，
  控制台文本会被 cp936 管道骗反。两个工具链都认这条 pragma，所以 A 档一处改动两边同时生效
  （B 档 `-J utf-8` 只救 mingw、C 档改纯 ASCII 等于删掉中文描述）。
- manifest：`compatibility` Win7/8/8.1/10 GUID 全列；` dpiAware=true` + `dpiAwareness=PerMonitorV2,system`；`dependent → Microsoft.Windows.Common-Controls version 6.0.0.0`（`EM_SETCUEBANNER` 依赖）。
- `SetProcessDpiAwarenessContext` 由代码在 `wWinMain` 首行调用（早于任何窗口创建）。

### 10.3 脚本与产物
| 脚本 | 内容要点 |
|---|---|
| `build.bat` | 探测 `vcvars64.bat`（VS2022 Community/BuildTools）→ `cmake -S . -B build` → `cmake --build build --config Release` |
| `CleanAndBuild.bat` | `rmdir /s /q build` 后同上（发布前必用，防缓存） |
| `PackageRelease.bat` | 从 `src/res/app.rc` 正则抓 `FILEVERSION`（抓不到即硬失败）→ **校验 `src/core/Config.h` 的 `kVersionText` 含同一 `vX.Y.Z`**（不一致拒绝出包，agent.md 四.2）→ 复制 `build/Release/SuperClip.exe` + `README.md` + `CHANGELOG.md` + `installer/*.bat` → 生成 `SuperClip_v<VER>.exe` → `Compress-Archive` 出 `release\SuperClip_v<VER>_portable.zip`（英文命名） |
| `installer\install.bat` | 复制 exe 到 `%ProgramFiles%\SuperClip\`（版本化名还原为固定名）+ `WScript.Shell` 建 `.lnk` |
| `installer\uninstall.bat` | `taskkill /im SuperClip.exe /f` → 删目录与快捷方式 |

产物目标体积 **≤ 3 MB**（原 130 MB），启动到可交互 < 300ms。固定名 `SuperClip.exe` 不可改（单实例路径比对、installer、进程名显示都依赖它）。

---

## 11. 实施顺序与完成判据

| 序 | 任务 | 完成判据（可验证） |
|---|---|---|
| 1 | `native/` RAII + `Text`/`Time`/`Sha256` | §9.3 8 例绿；`Sha256` 已知向量匹配 |
| 2 | `TableParser` | §9.2 13 例绿 |
| 3 | `Store` + `StorageService` | §9.4 12 例绿；写出的 `history.json` 能被 .NET 版读回（互读验证一次） |
| 4 | 三窗口 + 消息循环 + 单实例 + 热键 + 托盘 | 无 UI 也能：托盘图标出现、`Ctrl+`` 无响应（窗未建）、日志无泄漏；`tasklist` 单实例 |
| 5 | `ClipboardMonitor` | 收起窗口后仍持续入列；Excel 复制不丢；自粘贴不重复入列（AC-2 前置） |
| 6 | `MainWindow` 骨架 + `Theme` + `ListRenderer` | 380×600 自绘窗口显示列表与序号；150%/200% DPI 无错位；200 条滚动流畅 |
| 7 | 搜索/过滤/收藏/清除/复位/悬浮/收起/关闭 | AC-4/5、FR-06..08/12..14/17 实机走查 |
| 8 | `PasteService` + `DoPaste` 编排 + 目标捕获 | AC-2/6 三应用实机；1000ms 守护期间复制不重复入列 |
| 9 | `ProcessPicker`（含 T2） | 点选 Excel 不改变其选区；中途锁屏光标不残留；红/绿状态正确 |
| 10 | `SettingsService` + 位置/模式恢复（**v2.5.0 起设置持久化已移除，见 doc/DESIGN.md §8.3；本行以下为历史里程碑判据**） | 重启后位置、模式、置顶、绑定进程名恢复；拔掉副显示器回默认停靠。**2026-10-04 实机结论**：§9.6 四例绿（40 例/214 断言/0 失败）；`Left100/Top120/400×520` 精确还原并原值回写；`Left9000/Top7000` 回默认停靠 `1540,216`；无文件首启＝右停靠+`topmost=True`+普通模式+全部+未绑定，退出时才建默认文件；唯一候选恢复绑定（日志+绿靶心+"已绑定：PasteTarget"）；同名两窗一律不绑（红靶心+状态栏提示）；★ 关→`WS_EX_TOPMOST` 位消失且**立即**落盘→重启仍关→可开回；点模式文字→`PasteMode` 立即落盘。**未验证**：筛选经模态菜单变更后的 `FilterType` 落盘；`SplitSingleColumn` 无 UI 入口 |
| 11 | `HelpWindow` + 右键菜单 + 气泡 | 9 步引导可翻页（**v2.4.0 曾扩到 10 步**：新增「填表接力」页；**v2.4.1 按用户决议删掉该页，回到 9 步**）；菜单项文案动态显示当前模式。**2026-10-04 实机结论**：三项菜单在鼠标右键与 `VK_APPS`（锚列表区左上）两路都能弹出，4 项含分隔线、文字随状态翻转（"粘贴模式：普通（点此切到快速）"）；选「粘贴模式」`PasteMode` 0→1→0 落盘、选「复制模式」`SplitSingleColumn` false→true 落盘、取消零改动；帮助窗在主窗左侧 12 逻辑px、420×300、`enabled=False` 证明模态、点「下一步」与 `VK_RIGHT` 均可翻页、`1/9` 与 `9/9` 钳住且按钮禁用、重开回 `1/9`、`Esc` 与「关闭」都还原主窗；搜索框内右键仍是原生 `EDIT` 菜单（15 项）；主窗无回归。**未验证**：① 点选期间不弹菜单——代码有分支，但驱动无法在盲态安全右键（会点到别家窗口），未跑；② `Shift+F10` 按决议不实现；③ 帮助窗 150%/200% DPI 排版（挂步骤 6 遗留）；④ `true→false` 的反向落盘未单独复验。**v2.2.0 变更**：菜单加第 3 项「填表接力」，判据由"4 项含分隔线"改为**"5 项含分隔线"**（粘贴模式 / 复制模式 / 填表接力 / ─── / 使用帮助），本轮**已实拍**（19:47 `VK_APPS` 那一路，见 §6.8 行末与 `doc/TESTING.md` §2 v2.2.0 行）。**v2.3.0 回退**：接力改无开关，该项连同 `HideForRelay()` 一起删除，判据回到**"4 项含分隔线"**（粘贴模式 / 复制模式 / ─── / 使用帮助）；本轮**未实拍**，需随 v2.3.0 走查重拍（`doc/TESTING.md` §5 判据 6） |
| 12 | 打包脚本 + 干净 VM 验收 | AC-7/8；`dumpbin /dependents` 仅 §附录白名单 DLL |
| 12 之后 | **v2.1.0**：五项界面修订 + C14 快速模式选中位钉第一行 | 交叉构建 error 0、§9.4 增至 16 例（合计 41 例/222 断言/0 失败）。**2026-10-05 17:01–17:07 实机走查通过**（合成数据、每次点击前 `WindowFromPoint`→`GA_ROOTOWNER` 守卫、`GUARD_FAILS=0`）：图标渲染、图钉两态与 `topmost` True→False→True、✕ 出现/清空、点底栏空白收藏数不变（2→2）、点署名后前台窗标题变 `mylxnet/SuperClip-C`、气泡 `tip_left-win_left=51px` 在行之上；C14 两轮靶窗 dump 各含本轮 token。逐条判据见 `doc/TESTING.md` §2 与 `doc/PROJECT_STATE.md` §6 |
| 12 之后 | **v2.1.1**：C13 置顶兜底（修"启动即置顶"在真实桌面不生效） | 交叉构建 error 0、41 例/222 断言/0 失败、`FileVersion=2.1.1.0`。实机：修复后连续两次冷启动（含让前台给靶窗那次）`WS_EX_TOPMOST` 位为 1；当时 `WM_ACTIVATE` 兜底分支一次都没被执行过（再没能复现出"丢带"的起始态）→ 标未验证，**同日 17:41 由 v2.1.2 那轮补测通过** |
| 12 之后 | **v2.1.2**：发布验收资产轮（零产品代码变更） | `ReleaseChecklist.md` 重做成可执行判据单（§2.1 环境自检 / §2.2 十行判据 / §5 发布记录表单 / §6 体积门三档降级），新增 `cpp/qa/ac8_loop.ps1`（AC-8 断网连贴 100 轮驱动，自带 WinForms 靶窗 + 两道防误跑守卫）。全量干净重建 `RC=0`、error 0、自有源 warning 0；单测 41/222/0；产物 3 649 181 B / MD5 `ab5a5ff2…`。**顺带把 v2.1.1 挂着"未验证"的置顶兜底分支实测通过**（见 `doc/PROJECT_STATE.md` §4 坑 #12 ④） |
| 12 之后 | **v2.2.0**：C15「填表接力」（Alt+左键逐条贴 + `Ctrl+Alt+空格` 兜底） | 新增 `services/RelayService.h/.cpp`（42 + 86 行）、`Store::RelayArmable/RelayNext`、`PasteService` 两处（抬 Alt、目标已前台不夺前台）、`DoPaste` 第三参 `targetOverride`、第二把热键、`ID_RELAY_IDLE` 闲置定时器、`TrayService::ShowBalloon`、菜单第 3 项。交叉构建 `RC=0`、error 0、自有源 warning 0；**单测 42 例 / 230 断言 / 0 失败**（新增 §9.4-17；用例数改为 `Run()` 自增统计）；版本三处同号 `2.2.0`。**2026-10-05 19:44–20:02 实机走查通过**（合成 15 条投喂、每次点击前 `WindowFromPoint`→`GA_ROOT`→按 **PID** 判自家窗，`GUARD_FAILS=0`；产物先正常退出实例才拷进 `cpp/build-mingw/`，size+md5 与 WSL 源逐字节相同，见 `doc/PROJECT_STATE.md` §4 坑 #14）：Alt+左键四次触发四次落位（间隔 52–66 ms，`焦点控件=Edit`、`事件数=4`）、四次贴出内容互异证明沉底→下一条上位、`Ctrl+Alt+空格` 兜底成功、托盘气泡实拍可见且正文逐字正确（**标题乱码是照出的新缺陷**，坑 #15）、四条自动解除里**闲置 300.006 s / 锁屏 / 呼出 / 退出清理**均有日志行（注销 `WM_ENDSESSION` 沿用 N9 仍无法注入）、Alt+双击 60 ms 只产生 1 次触发。**残留六条见 §6 首行**，逐条判据见 `doc/TESTING.md` §2 末行。**⚠ 本轮的交互模型已于同日 20:20 被用户否决**（四道武装门槛让接力在真实使用中常常根本没装上），机制层全部沿用，门槛与开关由 v2.3.0 删除 |
| 12 之后 | **v2.3.0**：C15 改「无开关接力」（可见性 + 模式驱动，用户裁定 B 档） | **删**：右键菜单「填表接力」项、`HideForRelay()`、武装/解除托盘气泡、`ID_RELAY_IDLE` 闲置定时器与 `Touch()`、`Store::RelayArmable()`（含【全部】视图与空搜索两条准入）、`OnRelayIdle()`/`EndRelay()`/`RelayBlockReason()`/`relayArmed()`。**增**：`AppContext::SyncRelayHook()`（唯一装/卸入口，幂等重装即 LL 钩子的自愈路径）、`OnSessionUnlock()` + `kWtsSessionUnlock`。改：`RelayNext()` 取所见即所贴的 `display_.front()`；全灰显不卸钩只状态栏提示。交叉构建 `RC=0`、error 0、自有源 0 警告；**单测 42 例 / 228 断言 / 0 失败**（21:26 复跑）；版本三处同号 `2.3.0`；依赖仍 10 个 DLL、零网络库。**实机走查已通过**（2026-10-05 21:49–21:58，六条判据逐条时间戳见 `doc/TESTING.md` §2 与本文 §11 本行）：启动即就绪、搜索态贴所见第一行、全灰显不卸钩、收起即卸·呼出即回、切模式即卸·即回、锁屏卸·**解锁自动装回**·兜底热键·双击去重·退出清理，七次点击全过命中守卫零误投。**22:12:30–22:15:55 用户在真 WPS 表格上自用本版**，日志补齐了脚本拍不到的两点：点选起止（22:13:57.356 卸下 → 22:13:58.982 命中绑定 `ET` → 22:13:59.007 就绪 → 继续贴）与全灰显连 10 次触发不卸钩、新条目上位后同一钩子立刻动作。**⚠ 当时记下的"真表格单元格落点（`EXCEL7`/`EXCEL6` 焦点控件）多轮命中"已由 v2.3.1 订正为不成立**——用户随即报障那些粘贴一条都没落进单元格，日志只能证"取了第 1 行、按键投给了那个焦点控件"（坑 #19）。逻辑层与实机之外，只剩「钩子被系统摘除后 `Arm()` 自愈」一条标未验证 |
| 12 之后 | **v2.3.1**：修接力注入批次（用户报障「`Alt+左键` 粘贴不到 Excel 中」） | **根因**：抬 `Alt`/`Ctrl` 在点击那一刻单独发一批，`Ctrl+V` 由 60 ms 后的 `ID_FOCUS_WAIT` 定时器发、旧批次只有 4 个事件（不含 `Alt↑`）；C15 的手势是"**按住** `Alt` 点"，于是目标实收 `Alt+Ctrl+V`（Excel/WPS＝选择性粘贴）→ **日志全绿而表格毫无变化**。走查压不出它：脚本合成的 `Alt` 由脚本自己按时抬起。**改**：`SendCtrlV(bool releaseCtrl)` 把 `Alt↑`（**恒发**）与按需的 `Ctrl↑` 排进**同一次 `SendInput`**（原子投递，5 或 6 事件）；新增 `PasteService::InjectCtrlV()` 在注入那刻用新增的 `IsCtrlDownAsync()` 复查 `Ctrl`（`WM_TIMER` 不带键盘状态快照）。`Alt↑` 不按键态判断——点击那一刻已单独抬过一次，注入时读到的是我们自己抬起的结果，判断会漏发（详见 `doc/PROJECT_STATE.md` §4 坑 #19）；粘贴日志新增「`同批抬键=`」，`事件数` 恒为 5/6（旧版 4）。改动面＝3 文件（`Keyboard.h`、`PasteService.h/.cpp`）。**已实测**：交叉构建 `RC=0`、error 0 行、自有源 warning 0 行；单测 **42 例 / 228 断言 / 0 失败**；版本三处同号 `2.3.1`，产物 `SuperClip-v231.exe` 的 `VersionInfo.FileVersion` 读回 `2.3.1.0`；字符串探针（新 exe 含 `v2.3.1` 与新日志串 `同批抬键`、旧 exe 均无）证新分支已编入。**未验证（不得宣称通过）**：真人按住 `Alt` 点真表格单元格是否真的落进内容（判据单在 `doc/TESTING.md` §5：内容变了 + 日志 `同批抬键=Alt`、`事件数=5` + 连点 3 格贴出 3 条不同内容）、重复 `Alt↑` 副作用、Excel keytip 是否闪。同时把 DESIGN §5.5/§6.4/AC-9、TESTING §2/§5、PROJECT_STATE §6 里"真表格落点已实证"的结论一并订正，登记为坑 #19。**→ 手验已于同晚 23:25–23:40 执行，结果"半通过"（编辑态 `EXCEL6` 能贴、网格 `EXCEL7` 贴不进），后续见下两行** |
| 12 之后 | **v2.3.2**：注入批次改 `Ctrl↓→Alt↑` + 补扫描码（**失败，用户裁定保持现状**） | **起点**：v2.3.1 手验的分界现象——`EXCEL7\|工作簿1`（WPS 网格＝单元格仅选中）十一次全不落地，`EXCEL6\|`（编辑框）六次全落地（`IsPasted` 2→22）。**两个互斥假设一起修**：H1 干净 `Alt` 点按让表格进 keytip 菜单态、随后的 `Ctrl+V` 被当菜单加速键吞掉；H2 `wScan=0` 不被网格 accelerator 接受。**改**：批次顺序 `Ctrl↓ → Alt↑ → V↓ → V↑ → Ctrl↑`（`Ctrl` 已按下时抬 `Alt` 是和弦、不构成独立点按）；新增 `ScanOf(vk)=WORD(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC))`，`SendKey()` 与批次 `push()` 都写 `ki.wScan`（**有意偏离**技术方案 §5.2「只送虚拟键码」，已在 `Keyboard.h` 就地订正）；删掉 `PasteService.cpp` 点击那一刻的单独 `Alt↑`；日志值改 ``Ctrl↓→Alt↑(含扫描码)``。改动面＝3 文件（`Keyboard.h`、`PasteService.h/.cpp`）。**构建侧全绿**：error 0、自有源 warning 0、单测 42/228/0、`VersionInfo`=`2.3.2.0`、探针 v232 三项与 v231 相反。**手验失败**：23:52:33–23:53:06 八次触发日志全是 `同批抬键=Ctrl↓→Alt↑(含扫描码)`、`事件数=5`，`EXCEL7`/`EXCEL6` 两态都出现过，用户回「还是没有进去」→ **H1、H2 双双证伪，且比 v2.3.1 退了一步**。**本轮最大的收获是那个对照测试**：完全不涉及 `Alt`（WPS 里选中空单元格 → 快速模式选中一条 → 按空格普通粘贴）用户答「**没成功过**」→ **C++ 版注入链在 `EXCEL7` 上从来没成功过**，与接力/`Alt`/本轮改动都无关；两边权威实现只剩打包方式一个差别（.NET 旧版 `keybd_event` 四次独立调用 vs C++ 一次 `SendInput` 投整批）。**用户 2026-10-06 裁定放弃修改、保持现状，代码原样保留未回退**；下一刀方向（拆多次 `SendInput` + 事件间短延时 + 回退扫描码）与全部证据见 `doc/PROJECT_STATE.md` §4 坑 #20、`CHANGELOG.md` v2.3.2 段。附带订正：`IsPasted` 58→2 不是缺陷，是用户中途按过一次复位（`Store::Reset()` 不写日志） |
| 12 之后 | **v2.3.3**：接力兜底键 `Ctrl+Alt+空格` → `` Alt+` ``（用户决议，**产品逻辑一行未动**） | **起因**：用户原话「能否修改组合键CTRL+ALT+空格键，ALT+空格键？？」。**没照他最初说的做 `Alt+空格`**：`RegisterHotKey(MOD_ALT, VK_SPACE)` 能注册成功（Windows 只保护 `Win+L` 与 `Ctrl+Alt+Del`），但会把 `Alt+空格`＝「窗口系统菜单」**从所有程序手里静默抢走**且不给任何提示；`Ctrl+空格` 撞输入法中英切换。`` Alt+` `` 与呼出键 `` Ctrl+` `` 同键位、只差修饰键，零冲突（与 `Shift+F10` 当初"后果不可见即不可测所以不做"同一口径）。**改**（7 处机械改动）：`MainWindow.cpp:782` `mods` 由 `MOD_CONTROL\|MOD_ALT` 改 `MOD_ALT`、`:784/:786` 键位 `kVkSpace`→`kVkOem3`、`:789` 日志文案改「``Alt+` 被占用…``」；注释 3 处（`Config.h:115`、`AppContext.h:59`、`MainWindow.h:100`）；**删掉 `Config.h` 的 `kVkSpace`**（失去唯一引用）；版本三处同号 `2.3.3`。`kModNoRepeat` 的"先带它注册、失败退回不带"两级逻辑原样保留。**未动**：注入链、`SyncRelayHook()`、`RelayNext()`、C8 沉底、C14 上位、300 ms 去重、自家窗放行、零网络；**v2.3.2 那两处注入改动原样保留未回退**。**已实测**：error 0、自有源 warning 0、单测 42/228/0；探针 `SuperClip-v233.exe` 含 `v2.3.3`=True、`v2.3.2`=False、``Alt+` 被占用``=True、`Ctrl+Alt+空格 被占用`=False（v232 四项相反）；`VersionInfo.FileVersion`=`2.3.3.0`；换版上机（旧实例 v232 主窗收起、`MainWindowHandle=0`，改用 `EnumWindows` 按类名 `SuperClipMain` 投 `WM_CLOSE` → exited cleanly）新实例 pid 13624、hwnd 1705728、`IsWindowVisible=True`、条目 146 条、绑定 `ET` 与钩子均就绪；**启动日志最后 40 行内 `[W]`/`[E]` 0 行 → 两把 `RegisterHotKey` 都返回 TRUE**。`cpp/build-mingw/SuperClip.exe` 本轮**成功覆盖**为 v2.3.3（MD5 `5334764b…`，与 `SuperClip-v233.exe` 逐字节相同），坑 #14 的镜像锁未再触发。**未验证**：真人按 `` Alt+` `` 能否触发接力（注册成功 ≠ 按键可用）、与第三方抢键、**界面上没有任何地方告知这把键**（帮助窗无接力页，旧账）。驱动器 `cpp/qa/relay.ps1` 的 `-HotkeyRelay` 已由 `CtrlAltSpace()` 改为 `AltOem3()`（纯 ASCII、Parser 0 错），本轮**未跑**该分支 |
| 12 之后 | **v2.3.4**：修「应用描述乱码」（坑 #15 A 档，**产品逻辑一行未动**） | **起因**：用户原话「修改，应用的描述中出现乱码SuperClip è¶…çº§å‰ªè´´æ」——即属性面板/任务管理器「文件说明」与 Win10/11 toast 标题位读的那个 `FileDescription`。**根因**：`app.rc` 是无 BOM 的 UTF-8，windres/`rc.exe` 在没有 `code_page` 声明时按**构建机 ANSI 代码页**逐字节读入再宽化成 UTF-16；中文机器 CP936 恰好配回汉字所以一直看不出来，本项目交叉构建走 WSL（locale C/POSIX → CP1252），15 个字节就成了 15 个拉丁字符。**改**（4 处）：`app.rc` 顶部（`#include <windows.h>` 之前）加 4 行说明注释 + `#pragma code_page(65001)`；`FILEVERSION`/`PRODUCTVERSION` → `2,3,4,0`、两个 `VERSIONINFO` 字符串同；`Config.h:64` `kVersionText` → `L"v2.3.4"`。**未动**：任何 `.cpp`/`.h` 逻辑、`CMakeLists.txt`、`PackageRelease.bat`、QA 脚本。三档里取 A 是因为 B（`-J utf-8`）只救 mingw、C（纯 ASCII）等于删掉中文描述，A 一行对两条工具链同时生效。**已实测**：`RC=0`、error 0、自有源 warning 0，单独 `windres` 重编 `app.rc` 也 `RC=0`；单测 42/228/0；**决定性判据＝按码点读回 `VersionInfo`**：`FileDescription` 由旧产物 23 字符 `00E8 00B6 2026 …` 变成 15 字符 `8D85 7EA7 526A 8D34 677F`（超级剪贴板），`LegalCopyright` 的 `©` 由 `00C3 00A9` 修成 `00A9`，版本两字段 `2.3.4.0`；探针 `v2.3.4`=True、`v2.3.3`=False、`超级剪贴板`=True、乱码串=False；WSL↔Windows MD5 一致（`d9b6aa09…`），另存 `SuperClip-v234.exe`；**文件体积与 v2.3.3 同为 3,664 469 B，不能拿来判"有没有重编"**——`.text` 同 `0x00100ce0`、`.rsrc` 反而小 32 B（`0x000140f8`→`0x000140d8`），差额被节对齐填充吃掉；导入表仍恰好 10 DLL / 0 网络库，`ExtractAssociatedIcon` 仍取到 32×32 图标。**未验证**：MSVC `rc.exe` 路径（本机无 SDK）、toast 标题位实机观感（`ShowBalloon` 当前无调用方）；**本轮未换版上机**（用户实例仍是 pid 13624 的 v2.3.3，换版要停他的程序、需单独授权） |
| 12 之后 | **v2.4.0**：使用帮助 9→10 步 + README 重写 + **mingw 件作为发布物**（用户裁定） | **起因**：用户原话「判断是否在win7上正常运行吗？修正应用中的使用帮助，升级版本到2.4，重写readme.md，push代码和产物。项目收官。」Win7 一问只给静态证据（`WINVER=0x0601`、`-static` CRT、Win8+/10+ API 全 `GetProcAddress` 探测两级回退），**真机未跑过，不宣称能跑**；帮助窗对照 `doc/TESTING.md` §5 与审计登记落到三处缺陷。**改**：`HelpWindow.cpp` `kSteps[]` 9→10——新增第 4 步「填表接力」（`Alt`+左键手势、"快速模式 + 窗口在屏"生效条件、`` Alt+` `` 兜底、"WPS/Excel 仅选中单元格贴不进、要先双击进编辑态"已知限制），第 3 步补审计 S3「粘贴会先把这条写进系统剪贴板、原内容被覆盖」，第 7 步补 DESIGN N11「拆分按行优先、空行与空格子会被跳过、逐格贴会与格位错开」；头部注释改写（正文 172 DIP / 13px 约 9 行 / `DrawText` 带 CLIP **写超是静默裁掉**，故每页 ≤7 行、改完必须实机截图逐字核对）；`kStepCount` 由 `sizeof` 自推。**版本四处同号**：`app.rc` `2,4,0,0` ×2 + 两字符串、`Config.h:64` `kVersionText`、**`app.manifest` `assemblyIdentity`（v2.0.3 起漏改，本轮补）**、状态栏随 `kVersionText`。**未动**：任何产品逻辑、`CMakeLists.txt`、注入链、QA 脚本。**已实测**：`RC=0`、error 0、自有源 warning 0；单测 42/228/0（五轮同数）；探针 `v2.4.0`=True、`v2.3.4`=False、`填表接力`=True、``Alt + ` ``=True、`覆盖`=True、乱码串=False，`VersionInfo` 两版本字段 `2.4.0.0`、`FileDescription` 仍 15 字符；**帮助窗 10 页逐页截图逐字核对**（2026-10-06 01:01–01:03，合成数据）：十页 md5 互异、页码 `1 / 10`–`10 / 10`、第 1 页「上一步」与第 10 页「下一步」置灰、三处新增文案逐行可见无裁切；README 四张配图同轮实拍（底栏 `v2.4.0  by Mr lin`）；数据防护两轮（146/9 → 15/2 → 还原 md5 一致，只删本轮 `Temp\sc-v240*`）；**收尾经用户授权换版上机**：旧实例 `WM_CLOSE` 正常退出 → 还原 → v2.4.0 重启 pid 5320、`IsWindowVisible=True`、`Responding=True`、live=146/9。**发布**：`PackageRelease.bat` 的 MSVC 链本机跑不了，用户裁定 Release 附件用 mingw 交叉件（3,664,981 B / MD5 `5245fdba…` / 另存 `SuperClip-v240.exe`），超 3 MB 体积门一事在 README 醒目块 + CHANGELOG + `PROJECT_STATE` §6 三处挂账。**未验证**：MSVC 链、干净 VM AC-8、Win7/8.1 真机（KB2670838 与 `Microsoft YaHei UI` 字体族两道硬门槛）、150%/200% DPI、真人 `` Alt+` ``；悬浮气泡配图补拍两次失败（遮挡 + 半绘制，坑 #22），`readme-tip.png` 裁旧图底栏复用 |

判据纪律：每步完成后只报"已验证项 + 未验证项"，未实机验证的 UI/粘贴行为一律标注**未验证**，不得凭代码推断宣称通过。

---

## 12. 兼容性矩阵

| 环境 | 差异点 | 处置 |
|---|---|---|
| Win7 SP1 | D2D 1.0 / DWrite 1.0 可用；`SetLineSpacing` **不可用**；`GetDpiForWindow` 不可用；`RegisterHotKey`/`Shell_NotifyIcon` 可用 | 行距自算；DPI 走 `GetDeviceCaps`；`WM_DPICHANGED` 不会收到（Win7 无PerMonitor） |
| Win8/8.1 | `GetDpiForMonitor` 可用 | 动态加载优先级链覆盖 |
| Win10 1607+ | `PER_MONITOR_V2` + `WM_DPICHANGED` | 主路径 |
| Win11 22H2+ | `DWMWA_WINDOW_CORNER_PREFERENCE` | 有圆角；旧版本接受直角 |
| RDP / 无 GPU | D2D 硬件目标失败 | WARP 软件目标重试一次 |
| 标准用户（非管理员） | `Local\` 互斥体（T1）、`%AppData%` 可写、UIPI 拦提权目标 | installer 需要管理员（写 ProgramFiles），便携版不需要 |
| 多显示器混 DPI | 每窗 DPI 独立 | `WM_DPICHANGED` 重测所有 TextLayout |
| 高对比度 / 深色系统主题 | 用户主题非浅色 | `SPI_GETHIGHCONTRAST` 走系统色；本期不做深色主题（契约 UI 规范为浅色卡片） |
| 中文/日文/韩文 IME | 需正常合成输入 | 搜索框为真 `EDIT`，不引 `InvariantGlobalization` 类风险 |
| 无 .NET / 无 VC++ 运行库 | 目标机 | `/MT` 静态 CRT，`dumpbin` 校验 |

---

## 附录 A：接口清单（自包含）

标注：`u`=user32 `k`=kernel32 `s`=shell32 `g`=gdi32 `o`=ole32 `b`=bcrypt `d`=dwmapi。

### A.1 Win32（按调用模块）

| 模块 | API（全部 `W` 版） |
|---|---|
| `main`/`AppContext` | `CoInitializeEx` `CoUninitialize` (o)、`GetMessageW` `TranslateMessage` `DispatchMessageW` `PostQuitMessage` (u)、`CreateMutexW` `ReleaseMutex` (k)、`SetUnhandledExceptionFilter` `CaptureStackBackTrace` `GetLastError` `SetLastError` `FormatMessageW` (k)、`MessageBoxW` (u) |
| 窗口/布局 | `RegisterClassExW` `CreateWindowExW` `DefWindowProcW` `DestroyWindow` `SetWindowLongPtrW` `GetWindowLongPtrW` `ShowWindow` `IsWindowVisible` `IsWindow` `IsIconic` `SetWindowPos` `GetWindowRect` `GetClientRect` `ClientToScreen` `ScreenToClient` `MapWindowPoints` `GetSystemMetrics` `SetWindowTextW` `GetWindowTextW` `GetWindowTextLengthW` `UpdateWindow` `InvalidateRect` `RedrawWindow` `SetFocus` `GetFocus` `GetActiveWindow` `SetCapture` `ReleaseCapture` `GetDC`/`ReleaseDC` (u/g) `GetDeviceCaps` (g) `MonitorFromWindow` `GetMonitorInfoW` (u) |
| 定时器/消息 | `SetTimer` `KillTimer` `PostMessageW` `SendMessageW` (u) |
| 剪贴板 | `AddClipboardFormatListener` `RemoveClipboardFormatListener` `OpenClipboard` `CloseClipboard` `EmptyClipboard` `SetClipboardData` `GetClipboardData` `IsClipboardFormatAvailable` (u)、`GlobalAlloc` `GlobalFree` `GlobalLock` `GlobalUnlock` `GlobalSize` (k) |
| 前台/输入 | `GetForegroundWindow` `SetForegroundWindow` `SetActiveWindow` `BringWindowToTop` `AllowSetForegroundWindow` (u/2000) `SwitchToThisWindow` (u)、`SendInput` `GetKeyState` (u)、`RegisterHotKey` `UnregisterHotKey` (u) |
| 点选钩子 | `SetWindowsHookExW`(WH_MOUSE_LL) `UnhookWindowsHookEx` `CallNextHookEx` `WindowFromPoint` `GetAncestor`(GA_ROOT) `GetWindowThreadProcessId` `EnumWindows` (u)、`OpenProcess` `QueryFullProcessImageNameW` `CloseHandle` `GetCurrentProcessId` `GetModuleFileNameW` `GetModuleHandleW` (k)、`LoadCursorW` `SetSystemCursor` `SystemParametersInfoW`(SPI_SETCURSORS / SPI_GETWHEELSCROLLLINES / SPI_GETHIGHCONTRAST) (u) |
| 托盘/菜单 | `Shell_NotifyIconW` (s)、`RegisterWindowMessageW` (u)、`CreatePopupMenu` `AppendMenuW` `TrackPopupMenuEx` `DestroyMenu` `GetCursorPos` `LoadImageW` `LoadStringW` `GetSysColor` `GetSysColorBrush` (u) |
| 单实例/目标解析 | `FindWindowW` (u) + `EnumWindows`/`QueryFullProcessImageNameW` 路径比对（消除原 R10） |
| 持久化 | `SHGetKnownFolderPath` (s)、`CreateDirectoryW` `CreateFileW` `WriteFile` `ReadFile` `GetFileSizeEx` `FlushFileBuffers` `CloseHandle` `GetFileAttributesW` `DeleteFileW` `ReplaceFileW` (k/2000) `MoveFileExW` (k) |
| 哈希/ID | `BCryptOpenAlgorithmProvider` `BCryptCreateHash` `BCryptHashData` `BCryptFinishHash` `BCryptDestroyHash` `BCryptCloseAlgorithmProvider` (b)、`CoCreateGuid` `StringFromGUID2` `CoTaskMemFree` (o) |
| 时间/文本 | `GetLocalTime` `GetSystemTimeAsFileTime` `SystemTimeToFileTime` `FileTimeToSystemTime` `SystemTimeToTzSpecificLocalTime` `TzSpecificLocalTimeToSystemTime` `MultiByteToWideChar` `WideCharToMultiByte` (k) |
| 圆角/阴影 | `DwmExtendFrameIntoClientArea` (d/Vista) `DwmSetWindowAttribute` (d/11-22H2，动态加载) |

### A.2 Direct2D / DirectWrite

入口：`D2D1CreateFactory`（`SINGLE_THREADED`）、`DWriteCreateFactory`（`SHARED`）。

| 接口/方法 | 用途 |
|---|---|
| `ID2D1Factory::CreateHwndRenderTarget` `CreateRectangleGeometry` `CreateRoundedRectangleGeometry` `CreatePathGeometry` `CreateStrokeStyle` | 渲染目标与几何图标 |
| `ID2D1RenderTarget::BeginDraw` `EndDraw` `Flush` `Clear` `FillRoundedRectangle` `DrawRoundedRectangle` `FillRectangle` `DrawRectangle` `DrawLine` `FillGeometry` `DrawGeometry` `DrawText` `DrawTextLayout` `CreateSolidColorBrush` `CreateLayer` `PushClip` `PushAxisAlignedClip` `PopClip` `PopAxisAlignedClip` `PushLayer` `PopLayer` `SetTransform` `SetAntialiasMode` `SetTextAntialiasMode` `SetDpi` | 绘制与状态；`PushLayer` 仅备选（灰显走预混合色） |
| `ID2D1HwndRenderTarget::Resize` `CheckWindowState` | `WM_SIZE` 同步、遮挡跳帧 |
| `IDWriteFactory::CreateTextFormat` `CreateTextLayout` `CreateEllipsisTrimmingSign` | 4 组字号排版与省略号 |
| `IDWriteTextFormat::SetWordWrapping` `SetParagraphAlignment` `SetTextAlignment` | 中日韩按字断行、顶左对齐 |
| `IDWriteTextLayout::GetMetrics` | 单行上限判定与行高测量（仅取 `height`，见 §6.4 裁剪说明） |

### A.3 禁止出现（AC-8 与免运行库的正面证明）

`ws2_32 winhttp wininet urlmon cryptnet winmm imm32 oleacc psapi`、`RegSetValueEx*`（不写注册表，FR-18 不做自启）、`keybd_event` `mouse_event` `BlockInput` `SetWindowsHookEx(WH_KEYBOARD_LL)` `SetClipboardViewer`、以及任何 CLR/WinForms/WPF 符号。发布产物不链 `dbghelp`。


### A.4 链接库、版本红线与审计

- 链接库固定：`user32 kernel32 shell32 gdi32 advapi32 ole32 oleaut32 uuid bcrypt d2d1 dwrite wtsapi32`（与 `cpp/CMakeLists.txt` 逐字一致，交叉构建与 MSVC 同一份清单）。
  `dwmapi` **不入链**：由 `src/native/SystemInfo.cpp` 走 `LoadLibraryW`+`GetProcAddress` 动态加载；`uuid` 只提供 `FOLDERID_*` 的 GUID 数据，不产生导入表项。
- 交叉构建（mingw，非发布产物）实测导入表：`DWrite.dll GDI32.dll KERNEL32.dll SHELL32.dll USER32.dll WTSAPI32.dll bcrypt.dll d2d1.dll msvcrt.dll ole32.dll`。
  其中 `msvcrt.dll` 是 mingw 静态 CRT 的残留导入，MSVC `/MT` 版是否出现同名导入**未验证**；`oleaut32`/`advapi32` 在链接清单里但当前无被调符号故不入导入表。
  → 这张表**只能当旁证**，A.4 的结案仍以 MSVC 产物的 `dumpbin /dependents` 为准（步骤 12 B）。
- D2D/DWrite 版本红线：不得调用 `IDWriteTextFormat::SetLineSpacing`（Win7 `E_NOTIMPL`）；`DWRITE_TRIMMING_GRANULARITY_LINE_BY_LINE` 在 Win11 实测即被拒绝（`E_INVALIDARG`），单行上限一律走 §6.4 的手工前缀裁剪，不使用 `SetTrimming`/`GetLineMetrics`
- **审计动作**：发布前 `dumpbin /dependents SuperClip.exe` 与 `dumpbin /imports` 各跑一次，输出与 A.1/A.2/A.3 逐项核对，结果贴在发布记录里（AC-8 结案证据）

