# SuperClip C++ 版 技术方案（实现级）

> 版本：v1.0 · 配套文档：`C++_设计方案.md`（契约级，含 C1–C7 取值与 T1–T6 决议）
> 定位：模块接口、消息路由、状态机、渲染管线、降级矩阵、构建与测试的可执行说明
> 平台：Windows 7 SP1 / 10 / 11 x64 · C++20 / MSVC · 单文件免运行时 exe
> 冲突处理：本文与 `C++_设计方案.md` 不一致时以契约文档为准；两者均优先于 `SuperClip_设计规范.html` 的 §1/§10/§11/§12（技术栈章节，已作废）

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
| `src/core/Settings.h/.cpp` | `settings.json` DTO 与读写 | 130 |
| `src/core/Time.h/.cpp` | naive 本地时间 ↔ UTC ↔ ISO 串 | 90 |
| `src/core/Text.h/.cpp` | UTF8↔16、全角半角+ASCII 折叠、宽字符 trim/split | 140 |
| `src/native/*.h` | 句柄 RAII（§4.1）、剪贴板、前台、键盘、热键、光标、DPI/DWM 动态加载 | 420 |
| `src/services/ClipboardMonitor.h/.cpp` | 隐藏窗口 + 重试读取状态机 | 200 |
| `src/services/PasteService.h/.cpp` | 写盘剪贴板 + 三段夺前台 + SendInput 阶段机 | 220 |
| `src/services/StorageService.h/.cpp` | history.json 原子读写与容错 | 210 |
| `src/services/TrayService.h/.cpp` | `Shell_NotifyIcon` + 菜单 + `TaskbarCreated` 自愈 | 210 |
| `src/services/ProcessPicker.h/.cpp` | WH_MOUSE_LL 点选 + 光标替换 + 超时/锁屏取消；`FindWindowByProcess` 按进程名找回绑定窗口（§8.3，唯一候选才返回） | 230 |
| `src/ui/MainWindow.h/.cpp` | 窗口类、消息路由、命中分发、焦点/模式编排 | 938 |
| `src/ui/ListRenderer.h/.cpp` | 行测量、TextLayout 缓存、绘制、滚动 | 430 |
| `src/ui/Theme.h/.cpp` | 颜色/字号/几何图标路径、高对比度切换 | 200 |
| `src/ui/HoverTip.h/.cpp` | 悬浮全文气泡（自绘弹出窗，GDI 量算与绘制，不依赖 comctl32） | 190 |
| `src/ui/Widgets.h/.cpp` | `EDIT`（占位自绘）、自绘按钮/菜单（步骤 9+ 才建，气泡已落 `HoverTip`） | 180 |
| `src/ui/HelpWindow.h/.cpp` | 9 步模态引导 | 150 |
| `src/res/app.rc`、`manifest.xml` | 图标、`VERSIONINFO`、DPI/comctl6 | 90 |
| `tests/*` | 单测：40 例（§9.2/9.3/9.4/9.6；§9.1 的"33 例/Catch2"是步骤 10 前的旧口径，见下方备注） | 706 |

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
  void ToggleFavorite(const ClipItem*);
  void PasteDone(const ClipItem*, bool moveToEnd);
  size_t ClearAll();  void Reset();   // ClearAll 只清非收藏区（C12），返回删除条数
  void SetFilter(FilterType);  void ApplySearch(std::wstring kw);  // UI 已防抖
  void Select(const ClipItem*);              // 所有模式同步选中（原 SelectItem）
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
| 结构不变式 | `items_` = `[收藏区(最新在前) | 非收藏区(最新在前)]`，即下标 `0..Boundary()-1` 为收藏，其后为非收藏 | §4.3 |
| 插入位 | `insert(begin()+Boundary(), item)`（表格块按阅读顺序整块插入，首格在上） | FR-05 |
| 沉底 | 非收藏项 → `move` 到 `end()`；**收藏项 → `move` 到收藏区末尾（`begin()+Boundary()-1`）** | FR-10 / **C8 补充** |
| 淘汰 | 删除**非收藏区末尾**超额项（`erase(end()-excess, end())`） | **C7 末位淘汰** |
| 重排 | `std::stable_partition(begin,end,isFavorite)` | FR-08 |
| 复位 | 清全部 `isPasted`；两次 `stable_sort`（收藏按 `createdUtc` 降序、非收藏同样降序，稳定 → 同刻保持原序） | FR-14 / C5 |

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
DoPaste(item, moveToEnd):
  isInternalPaste = true; lastPasted = item.content
  PasteService.Start(owner=mainWnd, target=GetPasteTarget(), item.content)
    stage=Write →  stage=WaitForeground（60ms 定时器）  →  stage=SendKeys（SendInput 4 事件）  →  stage=Idle
  收到 WM_APP_PASTE_DONE：
    Store.PasteDone(item, moveToEnd)         // 灰显 + 沉底(C8) + 快速模式选中跳下一条未粘贴
    SetTimer(kPasteGuardMs, ID_PASTE_GUARD)  // 兜底清标志
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
  void SendCtrlV();                                           // SendInput 4 事件
  Stage stage_ = Stage::Idle; HWND owner_ = nullptr, target_ = nullptr; std::wstring pending_;
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
- 若 `T5` 热键注册失败：`NIM_MODIFY` + `NIF_INFO` 气泡提示快捷键被占用。

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

---

## 6. UI 层设计

### 6.1 消息路由总表（MainWindow）

| 消息 | 处理 |
|---|---|
| `WM_CREATE` | 存 `this` 到 `GWLP_USERDATA`；建 `EDIT` 子窗（`EM_SETCUEBANNER(L"搜索…", TRUE)`、`WM_SETFONT`）；建 D2D/DWrite 资源；按 settings 或默认停靠 |
| `WM_SIZE` | `renderTarget_->Resize`；重排 `EDIT`/按钮几何；`ListRenderer::Rebuild`；`InvalidateRect(NULL)` |
| `WM_DPICHANGED` | 采纳 `lParam` 建议矩形 `SetWindowPos`；`SetDpi`；重测所有 `TextLayout`；`Proposed` 与 settings 冲突时以 `LPARAM` 为准 |
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
| `WM_CONTEXTMENU` | `picking()` 中直接吞掉（点选期间不弹菜单）。否则 `ShowMainMenu()`：三项 `TrackPopupMenuEx(TPM_RETURNCMD)`（粘贴模式 / 复制模式 / 使用帮助，动态文案）。`lParam==-1,-1`（键盘 `VK_APPS`）→ 锚点取列表区左上换算成屏幕坐标。搜索框是**真 `EDIT` 子窗**，它在自己区域内直接收到 `WM_CONTEXTMENU` 并走系统默认（原生编辑菜单，实机 15 项），主窗这条分支碰不到它，因此无需 `OriginalSource` 判定 |
| `WM_TIMER` | 见 §6.3 分派 |
| `WM_APP_SEARCH_ENTER` | `EDIT` 子类窗回投：回车把按键归属交回列表（`focusOwner=List`+`SetFocus(主窗)`） |
| `WM_APP_PASTE_DONE` | `PasteService` 结果回投 → `OnPasteDone(ok)`：`ok` 才 `Store::PasteDone`（灰显/沉底/连贴跳转），随后 `ArmPasteGuard()` 起 1000ms 兜底 |
| `WM_APP_PICK_DONE` | `LlHook` 回投根窗口句柄 → `ProcessPicker::OnPickMessage()`：进程名解析与绑定态写入都在主线程（步骤 9） |
| `WM_WTSSESSION_CHANGE` | 值 0x02B1；`WM_CREATE` 里 `WTSRegisterSessionNotification(NOTIFY_FOR_THIS_SESSION)` 才收得到。`wParam==WTS_SESSION_LOCK` 且正在点选 → 走同一条 `Cancel()`，光标绝不残留（步骤 9） |
| `WM_ENDSESSION` | 注销/关机：正在点选同样 `Cancel()`，随后 §7.2 清理链 |
| `WM_ACTIVATE` | 失活且非点选/菜单期间 → 不做处理（`_lastExternalWindow` 在唤起前记录，更可靠） |
| `WM_CLOSE` | → `AppContext.Exit()`（标题栏 ✕ 即彻底退出，FR-15③） |
| `WM_DESTROY` | `PostQuitMessage(0)` |

Monitor / Tray 窗口只处理各自 2–3 条消息（§5.1、§5.4），其余 `DefWindowProcW`。

### 6.2 命中区域模型

```cpp
enum class HitZone { None, ModeText, BtnMinimize, BtnTopmost, BtnClose,
                     SearchBox, BtnFilter, BtnClear, BtnReset, BtnPick,
                     RowBody, RowStar, ListBackground };
struct HitResult { HitZone zone; const ClipItem* item = nullptr; size_t displayIndex = 0; };
```
`RowBody` 与 `RowStar` 在 `ListRenderer::rows_` 中预存矩形（数据变更时重建，每帧不重算）；命中顺序：先按钮与标题栏，再列表可见行。

### 6.3 定时器总表

| ID | 宿主 | 间隔 | 语义 | 触发后 |
|---|---|---|---|---|
| `ID_SEARCH` | MainWindow | 300ms | FR-06 防抖 | `Store.ApplySearch(editText)`；单次性（每次输入 `KillTimer+SetTimer`） |
| `ID_READ` | Monitor | 25ms | 读取重试 | `TryRead()`，成功或 6 次后 `KillTimer` |
| `ID_FOCUS_WAIT` | MainWindow | 60ms | 等前台焦点稳定 | `SendCtrlV()` → `onDone` |
| `ID_PASTE_GUARD` | MainWindow | 1000ms | C4 防护兜底 | 清 `isInternalPaste`/`lastPasted` |
| `ID_PICK` | Picker 宿主 | 8000ms | T2 卡死取消 | `picker.Cancel()` |
| `ID_STATUS_HINT` | MainWindow | 3000ms | 收藏/取消收藏后条目立刻离开当前视图，提示需要自动消隐 | `AppContext::OnStatusHintTimer()`：清空 `statusHint_` + 重绘 |

`SetTimer` 精度下限约 10–16ms，25ms 档实测可用；不引 `winmm!timeSetEvent`（避免多一个 DLL）。所有定时器 ID 唯一，`WM_TIMER.wParam` 分派。

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
- **几何图标**（不用字形）：收起 = 向下折线 + 底线；悬浮 = 图钉（圆 + 针）；关闭 = 两条对角线；靶心 = 同心两圆 + 十字。每个 `ID2D1PathGeometry` 建一次复用；按钮 hover 加浅色圆底。★/☆ 用字形（雅黑含该字符）。
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
| 搜索框 | 真 `EDIT`（`WS_CHILD\|WS_VISIBLE\|ES_AUTOHSCROLL\|WS_BORDER`） | IME 完好；占位用 `EM_SETCUEBANNER`（manifest 需 comctl32 v6）；`WM_CTLCOLOR*` 父窗返回白底画刷 |
| 筛选 | 自绘 `全部▾` 按钮 + `TrackPopupMenuEx` 四项 + 当前项打勾 | 免 `COMBOBOX` 主题割裂（ADR）。按钮文字随当前值变（`全部/文本/表格/收藏 ▾`，按钮宽 60 逻辑px 放不下"表格单元格"，菜单项用全称）；主窗是 `WS_POPUP` 非激活窗，弹出前 `SetForegroundWindow`、返回后 `PostMessage(WM_NULL)`，否则点窗口外菜单不消失 |
| 清除/复位/靶心/标题栏按钮 | 全自绘 + `HitZone` | 无子 HWND，减少 NC 处理。**清除**（2026-10-04 C12）只删非收藏区、无确认框，删完在状态栏给 3s 提示"已清除 N 条，收藏 M 条永久保留"（复用 `ID_STATUS_HINT`）——收藏在【全部】视图本就不可见，不提示会看起来按了没反应 |
| 置顶 | `MainWindow::topmost_` **默认 true**，`DockToWorkArea` 启动即 `HWND_TOPMOST`；★ 按钮切 `SetWindowPos(HWND_TOPMOST/HWND_NOTOPMOST)`，开启态在图标下画青色下划线 | 2026-10-04 用户指定"应用打开默认浮于各窗口最上层"（原 `false` 是步骤 8 遗留）。`Topmost` 落盘要等步骤 10 `SettingsService`，本轮固定"每次启动都开" |
| 列表 | 全自绘 | 无 UIA（N1） |
| 主窗右键菜单 | `MainWindow::ShowMainMenu()`：`CreatePopupMenu` 三项（粘贴模式 / 复制模式 / 使用帮助）+ `TrackPopupMenuEx(TPM_RETURNCMD\|TPM_LEFTALIGN\|TPM_TOPALIGN)` | 步骤 11（2026-10-04）。**严格三项**是用户裁定：置顶已有标题栏 ★，清除/复位带确认链、误触代价与开关不对等，不进菜单。项文字按当前状态生成并写明点击后果（"粘贴模式：普通（点此切到快速）"），取消即零改动。与筛选菜单同一套 `SetForegroundWindow` 前置 + `PostMessage(WM_NULL)` 收尾（主窗是 `WS_POPUP` 非激活窗）。复制模式点击后 `SaveCopyMode()` 即落盘（§8.3） |
| 帮助窗 | 独立无边框 `WS_POPUP`（`WS_EX_TOOLWINDOW\|TOPMOST`、owner=主窗），D2D 绘制 9 步 + 上一步/下一步/关闭按钮 | 入口：右键菜单「使用帮助」。420×300 逻辑px，贴主窗**左侧**（放不下回落右侧/居中，再 `FitRectToDesktop`）。模态用 `EnableWindow(主窗, FALSE)`，**不起嵌套消息循环**；首末位钳住且按钮禁用，重开回 `1 / 9`；`Esc`/关闭 还原主窗并 `SetFocus`。步骤 11 实机已验翻页（鼠标与 `VK_RIGHT`）、钳位、模态、还原；DPI 150%/200% 排版未验 |

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
 │    StorageService.EnsureDir → Settings.Load → Store.LoadFromDisk → ApplyOrder → Save
 │    TrayService.Create → ClipboardMonitor.Create(+AddListener) → ProcessPicker 就绪
 │    MainWindow.Create(WM_CREATE 内建 D2D/DWrite；失败即弹框退出)
 │    RegisterHotKey → 位置：settings 有效? 采纳 : 右缘垂直居中
 └─ while(GetMessageW) { Translate; Dispatch; }  → AppContext.Shutdown
```

### 7.2 退出清理链（顺序固定，`Exit()` 与崩溃过滤器共用）
```
ProcessPicker.Cancel()（卸钩子 + SPI_SETCURSORS）
→ UnregisterHotKey
→ Store 保存（history + settings，失败静默）
→ ClipboardMonitor.Destroy（RemoveClipboardFormatListener → DestroyWindow）
→ TrayService.Destroy（NIM_DELETE → DestroyWindow）
→ MainWindow 资源释放（ComPtr 自动 Release）→ DestroyWindow → PostQuitMessage
→ ReleaseMutex + CloseHandle(mutex) → CoUninitialize
```
`WM_ENDSESSION`（注销/关机）走同一链，避免托盘图标残留与十字光标残留。

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

> 构建与运行路径（2026-10-05 更新）：`bash build-tests.sh` 走项目专属 WSL 发行版 `superclip` 的 mingw 交叉构建（内部即 `cmake --build`，清单只有 `CMakeLists.txt` 一份），产物 `sc_tests.exe` 拷到 Windows 本机实跑（无 wine）。当前规模 **40 例 / 214 断言 / 0 失败**。

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

### 9.4 Store 不变式（新增 12 例）
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
额外：RDP 会话启动（WARP 路径）、150%/200% DPI、拔副显示器重启、高对比度主题、
      Explorer 被 kill 后托盘自愈、点选中途锁屏（光标不残留）、任务管理器观察 2 小时句柄不增
```

### 9.6 SettingsService（步骤 10 新增 4 例）
临时目录注入（目录名含秒+毫秒+线程 id+**进程内递增序号**，缺序号会撞名：两个用例共用同一 `%TEMP%` 目录时，先结束的那个 fixture 的 `remove_all` 会删掉后一个正在用的文件，表现为随机红）。

| # | 场景 | 断言 |
|---|---|---|
| G01 | 逐字节吃下 .NET v2.0.2 的真实那一行 | 9 个字段全部解析正确；原样再写一次 → 与读入的字节**完全相同**（键顺序、`null` 形态、无 BOM UTF-8） |
| G02 | 全字段往返（含负坐标与绑定进程名） | `Left:-1024 / Top:60 / 420×700 / PASTETARGET / Topmost:false / PasteMode:1 / Split:true / Filter:3` 存→取逐项相等 |
| G03 | 缺失 / `{` / `[]` / 空文件 / 空路径 / 不可写目录 | 读侧一律回默认（不抛）；写侧返回 `false` 且不影响主流程 |
| G04 | 越界与类型不符**按字段**作废 | `Left:99999999` → `hasRect=false`（其余字段仍有效）；`Width:100/Height:9000` → 尺寸回默认；`Topmost:null` → `true`；`PasteMode:true` → `0`；`FilterType:"2"` → `0`；`SplitSingleColumn:1` → `true`；未知键忽略 |

---

## 10. 构建与发布

### 10.1 CMake（要点）

> **权威来源是 `cpp/CMakeLists.txt` 本身**，本节只是要点，不再逐行照抄（历史上正是"文档抄一份、脚本抄一份、CMake 一份"三份不一致，才让主构建断链无人察觉；见 `SuperClip_审计核实与整改清单.md` §1）。
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
- `IDI_APP ICON "SuperClip.ico"`（多尺寸 16/24/32/48/256；托盘取 `SM_CXSMICON`）。
- `VERSIONINFO`：`FILEVERSION 2,0,2,0` / `PRODUCTVERSION 2,0,2,0`；`StringFileInfo`（`080404b0`）填 `ProductName=SuperClip 超级剪贴板`、`FileDescription`、`CompanyName`、`LegalCopyright`，右键属性可见（继承原 csproj 元数据要求）。
- manifest：`compatibility` Win7/8/8.1/10 GUID 全列；` dpiAware=true` + `dpiAwareness=PerMonitorV2,system`；`dependent → Microsoft.Windows.Common-Controls version 6.0.0.0`（`EM_SETCUEBANNER` 依赖）。
- `SetProcessDpiAwarenessContext` 由代码在 `wWinMain` 首行调用（早于任何窗口创建）。

### 10.3 脚本与产物
| 脚本 | 内容要点 |
|---|---|
| `build.bat` | 探测 `vcvars64.bat`（VS2022 Community/BuildTools）→ `cmake -S . -B build` → `cmake --build build --config Release` |
| `CleanAndBuild.bat` | `rmdir /s /q build` 后同上（发布前必用，防缓存） |
| `PackageRelease.bat` | 从 `src/res/app.rc` 正则抓 `FILEVERSION` → 复制 `build/Release/SuperClip.exe` + `README.md` + `CHANGELOG.md` + `installer/*.bat` → 生成 `SuperClip_v2.0.2.exe` → `Compress-Archive` 出 `release\SuperClip_v2.0.2_portable.zip`（英文命名） |
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
| 10 | `SettingsService` + 位置/模式恢复 | 重启后位置、模式、置顶、绑定进程名恢复；拔掉副显示器回默认停靠。**2026-10-04 实机结论**：§9.6 四例绿（40 例/214 断言/0 失败）；`Left100/Top120/400×520` 精确还原并原值回写；`Left9000/Top7000` 回默认停靠 `1540,216`；无文件首启＝右停靠+`topmost=True`+普通模式+全部+未绑定，退出时才建默认文件；唯一候选恢复绑定（日志+绿靶心+"已绑定：PasteTarget"）；同名两窗一律不绑（红靶心+状态栏提示）；★ 关→`WS_EX_TOPMOST` 位消失且**立即**落盘→重启仍关→可开回；点模式文字→`PasteMode` 立即落盘。**未验证**：筛选经模态菜单变更后的 `FilterType` 落盘；`SplitSingleColumn` 无 UI 入口 |
| 11 | `HelpWindow` + 右键菜单 + 气泡 | 9 步引导可翻页；菜单项文案动态显示当前模式。**2026-10-04 实机结论**：三项菜单在鼠标右键与 `VK_APPS`（锚列表区左上）两路都能弹出，4 项含分隔线、文字随状态翻转（"粘贴模式：普通（点此切到快速）"）；选「粘贴模式」`PasteMode` 0→1→0 落盘、选「复制模式」`SplitSingleColumn` false→true 落盘、取消零改动；帮助窗在主窗左侧 12 逻辑px、420×300、`enabled=False` 证明模态、点「下一步」与 `VK_RIGHT` 均可翻页、`1/9` 与 `9/9` 钳住且按钮禁用、重开回 `1/9`、`Esc` 与「关闭」都还原主窗；搜索框内右键仍是原生 `EDIT` 菜单（15 项）；主窗无回归。**未验证**：① 点选期间不弹菜单——代码有分支，但驱动无法在盲态安全右键（会点到别家窗口），未跑；② `Shift+F10` 按决议不实现；③ 帮助窗 150%/200% DPI 排版（挂步骤 6 遗留）；④ `true→false` 的反向落盘未单独复验 |
| 12 | 打包脚本 + 干净 VM 验收 | AC-7/8；`dumpbin /dependents` 仅 §附录白名单 DLL |

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

