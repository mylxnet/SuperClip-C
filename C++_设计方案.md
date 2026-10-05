# SuperClip C++ 版技术设计方案

> 版本：v1.0（设计冻结稿，待开工）
> 平台：Windows 7 SP1 / 10 / 11（x64）· 形态：桌面应用 · 单文件 exe · 零网络
> 依据：`SuperClip_设计规范.html`（v1.0，FR/AC 契约）+ `技术方案.md`（v2.0.2，行为权威）
> 状态：**功能契约全部继承；技术栈选型条款作废换为 C++**

---

## 0. 本文与原两份文档的关系

| 原章节 | 处置 |
|---|---|
| 设计规范 §1 技术栈、§10 项目结构、§11 构建发布、§12「技术栈」ADR | **作废重写**（已获授权换为 C++） |
| 设计规范 §2 FR-01..FR-18、§3 数据模型、§4 算法、§5 状态机、§7 UI、§8 持久化、§9 验收标准、§13 边界 | **逐条继承，语义不变** |
| 技术方案 全文 | 作为**行为权威**（含 v2.0.1/v2.0.2 已修复项 R3/R4/R5/R6），本文按其修复后取值 |

### 0.1 文档矛盾取值（固化，实现者不再自行判断）

| # | 矛盾 | 设计规范 | 技术方案 | 本文取值 |
|---|---|---|---|---|
| C1 | 托盘实现 | WinForms `NotifyIcon` | 纯 `Shell_NotifyIcon`，禁 WinForms | **`Shell_NotifyIcon`**（WinForms 与"免运行时"目标本身冲突） |
| C2 | 监听窗口宿主 | 挂主窗口 `HwndSource` | 独立隐藏窗口，与主窗生命周期解耦 | **独立隐藏窗口**（FR-15 要求收起后仍监听，必须解耦） |
| C3 | 发布剪裁 | `PublishTrimmed=true` | 禁用，NETSDK1168 致命 | **不适用**（C++ 无剪裁概念） |
| C4 | 粘贴防护窗口期 | 400ms | 1000ms + 粘贴后立即重置 | **1000ms + 立即重置**（取 R4 修复后） |
| C5 | 复位排序 | 未明确 | R6：收藏也改为时间降序 | **收藏与非收藏均按时间降序**（取 R6 修复后） |
| C6 | 目标平台 | Win10/11 | Win7 SP1 ~ Win11（P0） | **Win7 SP1 为下限**（C++ 下无额外成本） |
| C7 | 自动清理淘汰位置 | 伪码 `First(!IsFavorite)`（注释"最前=最旧"），与同文 §4.3"最新在上"、FR-04"删最旧"自相矛盾 | 同样自相矛盾（§5.7"各区最新在前"+"非收藏区最前=最旧"） | **末位淘汰**（删非收藏区末尾 = 最旧）。需求原文 FR-04 优先于伪码；`设计规范` 已出 v1.1 勘误。`技术方案.md` 尚未同步，见 §15 N8 |
| C9 | 主内容"多行 + 省略号"的实现 | 本文 §7 曾写 `TRIMMING_GRANULARITY_CHARACTER` + `MAXIMUM_LINE_COUNT=3` | 技术方案 §6.4 曾写 `LINE_BY_LINE` + `GetLineMetrics` 回退 | **两者都不可用**（Win11 26100 实测：`LINE_BY_LINE` 在 format/layout 级均 `E_INVALIDARG`；`CHARACTER`/`WORD` 无视 `count` 固定压成单行）。改为对字符数二分求"高度上限内最长前缀"再补 `…`，一次算好进 `LayoutCache`，两工具链同一条路径。**2026-10-04 后续**：上限由 3 行改为**单行**（用户指定），完整内容改由 `HoverTip` 气泡给出；`CHARACTER` 裁剪"只能压成单行"这一实测结果反而成了单行方案的依据 |
| C10 | 收藏条目的显示位置 | `FR-08`"收藏项**置顶分组显示**"、`设计规范` §7"收藏项浅黄背景高亮，且始终位于列表前部" | 与"普通列表不应被收藏项长期占位"的诉求冲突 | **收藏条目只在【收藏】视图显示**，`全部/文本/表格单元格` 三个视图一律剔除（2026-10-04 用户决议）。数组的 `[收藏区 | 非收藏区]` 分区不变式**原样保留**——持久化顺序、`Boundary()` 插入位、`MoveToBack` 沉底、FR-04 末位淘汰都依赖它，改的只是 `RebuildDisplay()` 的过滤条件；收藏区/普通区之间的分隔线随之删除（`Theme::Separator` 画刷一并移除）。副作用：点星标后条目立刻离开当前视图，故切换收藏时状态栏给 3s 去向提示（`ID_STATUS_HINT`） |
| C11 | 表格来源标注是否上屏 | 本文 §7 曾写"正文下方小字浅蓝 `#5B9BD5` 标注 `来自表格：第 X 行 第 Y 列`"（该色值本身是 2026-10-04 上午刚按用户意见从 `#1E88E5` 调浅的） | 用户复核后认为它"影响整体美观度，而且也没有什么实际意义" | **标注不再绘制**，行高不再随标注浮动（技术方案 §6.4 的 `cardHeight` 去掉 `kLabelGap + kLabelH` 项）；`kLabelH`/`kFontLabel`/`Theme::Label()`/`LabelText()`/`labelPasted_` 全部删除。**但仍是搜索字段**（用户同一次决议补充"不显示，但仍参与搜索"）：`SourceLabel()`、`foldLabel`、`RebuildDisplay()` 的第二个 `ContainsFolded` 与 `SourceRow/SourceCol` 持久化一律保留，单测 §9.3-06/07/08（标注文本）+ §9.4-14（隐藏字段命中）共同钉住 |
| C12 | 「清除」是否清掉收藏 | `FR-13`"清空所有记录（**含收藏**）" | 与 `FR-08`"收藏……**不参与清理**"直接矛盾（规范自身两处口径相反），用户裁定收藏要永久保存 | **`ClearAll()` 只删非收藏区**，收藏区原序保留并落盘，返回值改为"实际删除条数"；清除后在状态栏给 3s 提示（收藏在【全部】视图不可见，不提示会像"按了没反应"）。想彻底删一条收藏：**先取消收藏 → 再按清除**（用户 2026-10-04 选定，不做右键单条删除、不做确认框）。淘汰上限口径不变（`kMaxItems` 只数非收藏区），副作用是收藏数无上限、`history.json` 随之增长。单测 §9.4-15 |
| C13 | 启动是否置顶 | 设计方案 §8 早写 `Topmost(默认 true)`，代码里 `topmost_ = false`（步骤 8 遗留） | 用户要求"应用打开默认浮于各窗口最上层" | **按 §8 补齐实现**：`topmost_` 初值 true，`DockToWorkArea` 启动即 `HWND_TOPMOST`，★ 按钮仍可关。`Topmost` 落盘（记住用户关掉的偏好）等步骤 10 `SettingsService`，本轮固定"每次启动都开" |

---

## 1. 技术栈与依赖

| 项 | 选择 | 理由 |
|---|---|---|
| 语言 | **C++20** | 无托管运行时依赖；`std::wstring` 承接 UTF-16 剪贴板文本 |
| 编译器 | **MSVC（VS 2022 / Build Tools）** | Win32 SDK 官方工具链 |
| 运行库 | **`/MT` 静态 CRT** | 目标机免装 VC++ Redistributable（等价原"免装 .NET"诉求） |
| UI | **Win32 窗口 + Direct2D 1.0 / DirectWrite 全自绘** | 还原原 UI 规范（灰显半透明、收藏浅黄底、圆角卡片、选中描边）；exe 个位 MB |
| 文本输入 | **真 `EDIT` 子控件**（仅搜索框） | 输入法合成不可自绘兜底；文档明确禁止 `InvariantGlobalization` 的同一动机——IME 必须完好 |
| JSON | **自研极简 JSON**（`src/core/Json.h/.cpp`，约 180 行，零依赖） | 落地实况与原 ADR 不同：原选 RapidJSON，但**当前环境离线取不到包**，改为内置极简实现（决策记录见 `Json.h` 头注释）。本项目只需「对象数组」一种形态，够用；副产品是 AC-8 的第三方审计面归零 |
| SHA-256 | **BCrypt**（`bcrypt.lib`，系统自带） | 免第三方；FR-02 指定 SHA-256 |
| GUID | `CoCreateGuid` + `StringFromCLSID` | 对应 C# `Guid.NewGuid()` |
| 网络 | **零** | AC-8；依赖面清单见 §1.2 |
| 构建 | CMake + `.rc` 资源 | 版本元数据走 `VERSIONINFO` |

### 1.1 明确不引入

WPF/WinForms/WinRT/Qt/Boost/HTTP 库/遥测。**零第三方依赖**（原 ADR 允许的 RapidJSON 与 Catch2 均未引入：前者因离线取不到包改为自研极简 JSON，后者改为 `tests/test_main.cpp` 自带断言器）。

### 1.2 依赖面审计清单（AC-8 结案依据）

```
链接库（与 cpp/CMakeLists.txt 逐字一致，交叉构建与 MSVC 同一份清单）：
  user32  kernel32  shell32  gdi32  advapi32  ole32  oleaut32  uuid  bcrypt  d2d1  dwrite  wtsapi32
动态加载（LoadLibraryW+GetProcAddress，不入链、是否出现取决于运行机）：
  dwmapi（毛玻璃/窗口属性）  shcore（DPI，Win7 取不到则回落 GetDeviceCaps）
不链接也不调用：comdlg32（无文件对话框）、任何网络栈
```
实测导入表（mingw 交叉构建，`objdump -p SuperClip.exe`）：
`DWrite.dll GDI32.dll KERNEL32.dll SHELL32.dll USER32.dll WTSAPI32.dll bcrypt.dll d2d1.dll msvcrt.dll ole32.dll`
—— `uuid` 只是 GUID 数据不产生导入，`advapi32`/`oleaut32` 当前无被调符号故不在表内；`msvcrt.dll` 是 mingw 静态 CRT 残留，MSVC `/MT` 版待 `dumpbin /dependents` 结案。
全部为系统本地 DLL 导出，无 socket/WinHTTP/WinINet/Curl 符号。验收方式：链接期不引用网络库 + Windows 防火墙出站规则拦截 `SuperClip.exe` 后功能无变化。

---

## 2. 功能需求清单（继承 FR-01..FR-18）

| 编号 | 需求（原文语义） | C++ 实现载体 |
|---|---|---|
| FR-01 | 后台监听系统剪贴板，只记纯文本 | `ClipboardMonitor` + `WM_CLIPBOARDUPDATE(0x031D)` |
| FR-02 | SHA-256 去重，同内容只留一条并更新时间戳 | `Sha256::Hex(content)` + `Store::AddSingle` |
| FR-03 | JSON 持久化，历史/收藏/灰显/排序全保留 | `StorageService::Save`（按显示顺序序列化） |
| FR-04 | 非收藏 > 500 淘汰最旧 | `Store::EnforceLimit` |
| FR-05 | TSV 按行→列拆单元格，标注行列；纯文本不拆；HTML 表格以 TSV 文本为准 | `TableParser`（只读 `CF_UNICODETEXT`，天然满足"以 TSV 为准"） |
| FR-06 | 搜索实时过滤，300ms 防抖 | `SetTimer(300)` 重启式防抖 |
| FR-07 | 过滤：全部/文本/表格单元格/收藏 | `FilterType` 枚举 |
| FR-08 | ★/☆ 切换；收藏只在【收藏】视图显示（C10）；**不参与「清除」**（C12）；持久化 | `Store::ToggleFavorite` → `ApplyOrder` |
| FR-09 | 普通模式双击粘贴 → 灰显、位置不变 | `WM_LBUTTONDBLCLK` → `DoPaste(item, moveToEnd=false)` |
| FR-10 | 快速模式：进入默认选中第一条；单击仅选中；空格粘贴 → 灰显 + 沉底 + 跳下一条未粘贴 | `PasteMode::Quick` + `WM_KEYDOWN VK_SPACE` |
| FR-11 | 写剪贴板 → 激活目标窗口 → 发送 Ctrl+V | `PasteService::PasteTextAsync` |
| FR-12 | 标题栏第 2 图标切 `TopMost`；**应用打开默认即置顶**（2026-10-04 C13） | `SetWindowPos(HWND_TOPMOST/HWND_NOTOPMOST)`，`topmost_` 初值 true |
| FR-13 | 「清除」= 清空非收藏记录；**收藏条目永久保留、不受清除影响**（2026-10-04 C12，原"含收藏"作废） | `Store::ClearAll()` 只删非收藏区，返回删除条数供状态栏提示 |
| FR-14 | 「复位」= 恢复最初顺序 + 清除所有灰显 | `Store::Reset`（取值见 C5） |
| FR-15 | 托盘常驻；第 1 图标收起继续监听；第 3 图标彻底退出 | `ShowWindow(SW_HIDE)` / `AppContext::Exit` |
| FR-16 | `Ctrl + `` 呼出/隐藏；隐藏时仍监听 | `RegisterHotKey(MOD_CONTROL, VK_OEM_3=0xC0)` |
| FR-17 | 每行左侧显示当前顺序序号（1 起） | 渲染期按显示位置计算（等价原 `IndexConverter`，不用静态索引） |
| FR-18 | 开机自启：**不做** | 不注册 Run 键、不建计划任务 |

---

## 3. 数据模型

```cpp
enum class ClipType : int { Text = 0, TableCell = 1 };   // 数值序列化，兼容现有 history.json

struct ClipItem {
  std::wstring id;          // GUID（小写带连字符），构造后不变
  std::wstring content;     // 构造后不变
  ClipType     type;        // 构造后不变
  int  sourceRow = 0;       // 1 起；0 表示无（普通文本）
  int  sourceCol = 0;       // 1 起；0 表示无
  std::chrono::system_clock::time_point timestamp;  // 本地时间，构造后不变
  std::wstring hash;        // SHA-256 小写十六进制，构造后不变
  bool isFavorite = false;  // 可变 → 需通知 UI
  bool isPasted   = false;  // 可变 → 需通知 UI

  std::wstring SourceLabel() const;  // "来自表格：第 X 行 第 Y 列" 或 L""（计算属性，不存储；不上屏，仅作搜索字段）
};
```

**属性变更通知**：原 `[ObservableProperty]` 的等价物是一个极轻 observer——`Store` 变化后发一条 `StoreChange { Kind, affectedId }`，`MainWindow` 按 Kind 决定失效区域（整表重绘 / 单行重绘）。不引入信号槽框架。

**不变式约束**：`id/hash/content/timestamp/type` 只允许在构造时赋值（C++ 用 `const` 成员 + 工厂函数，或约定"无 setter"），保证去重键在生命周期内稳定。

派生枚举：`FilterType { All, Text, TableCell, Favorite }`、`PasteMode { Normal, Quick }`、`CopyMode { Normal, TableSingleColumn }`（对应原 `SplitSingleColumn`）。

---

## 4. 核心算法规范

### 4.1 去重（FR-02）
```
hash = LowerHex(SHA256(UTF8(content)))
若已存在同 hash 项 → 先移除旧项（记录其 isFavorite）
newItem.isFavorite = 旧项.isFavorite
插入到"第一个非收藏项之前"（非收藏区最前，最新在上）
```

### 4.2 TSV 表格拆分（FR-05）
```
IsTable(text):
  1. text 含 '\t'                       → 恒为表格
  2. 否则 CopyMode==TableSingleColumn 且含换行 → 表格
  3. 单行且不含 '\t'                    → 普通文本

Parse(text):
  normalized = text 中 "\r\n"→"\n"、"\r"→"\n"，再 Trim('\n')
  rows = split(normalized, '\n')
  for r in 0..rows.size-1:
    if rows[r] 为空 → 整行跳过，行号不前移（r 本身递增，等价"空行不产条目"）
    if rows[r] 含 '\t': cols = split(rows[r], '\t')
    else:               cols = { rows[r] }          // 单列，Col=1
    for c in 0..cols.size-1:
      if cols[c] 为空 → 跳过该单元格，但 Col 值不前移
      emit Cell{ content=cols[c], Row=r+1, Col=c+1 }
```
行/列均 **1 起**。整块顺序插入非收藏区最前（逐条头插会整体倒序——原实现踩过的坑，必须保留此设计）。

### 4.3 排序与置顶
- 单一有序 `std::vector<std::unique_ptr<ClipItem>>` 维护显示顺序（真源）。
- **顺序不变式**：数组结构 = `[收藏区 | 非收藏区]`，两区内部均**最新在前**；因此非收藏区**最前 = 最新**、**末尾 = 最旧**。
- 收藏区在前、非收藏区在后，**各自保持相对顺序**（重排用 `std::stable_partition`）。
- 新复制项插入非收藏区最前（= 最新位置）。
- 快速粘贴沉底：从原位置移除并追加到非收藏区末尾（= 最旧位置），该顺序被持久化。
- 切换收藏时重排一次。

### 4.4 自动清理（C7：末位淘汰）
```
删除超额的非收藏区尾部区间（末尾 = 最旧）；收藏永不参与
```
> 原 `设计规范` §4.4 伪码为 `First(!IsFavorite)`（注释"最前=最旧"），与 §4.3"新复制项插入非收藏区最前、最新在上"和 FR-04"删除最旧"冲突，按字面会误删最新条目。已在 `设计规范` v1.1 勘误为末位淘汰，本文取一致值。实现按"批量删除超额尾部区间"替代逐项 `while`，复杂度 O(超额数)。

### 4.5 搜索过滤
```
到期(300ms) → BuildDisplayList():
  q = Items
  q = 按 FilterType 过滤（All/Text/TableCell/Favorite）
  if kw 非空:
     kw = 全角→半角规范化(kw)                  // v2.0.2 特性，保留
     q = q.Where(content.Contains(kw, 忽略大小写) || sourceLabel.Contains(kw, 忽略大小写))
  原地同步到 DisplayItems（见 §4.6）
```
`Contains` 用宽字符大小写不敏感比较（`CompareStringEx` / `towupper` 循环），**不依赖区域设置库**，避免 .NET 版 `InvariantGlobalization` 崩溃的那类坑。

### 4.6 原地同步（替代 `SyncInPlace`）
WPF 需要它是为了避免 `ItemsSource` 重置导致选中丢失、滚动归零、整屏闪烁。C++ 下选中态与滚动偏移由我们自己持有，等价实现为：
```
刷新后：按 Id 找回原选中项 → 命中则保持选中，未命中则清空选中；
        滚动偏移保持不变，越界则钳制到 [0, maxScroll]；
        仅对内容变化的行 InvalidateRect(该行矩形)。
```
好处：`ClipStore` 与 WPF 集合彻底解耦，**可以单测**（原实现此处是测试空白）。

---

## 5. 交互与状态机

### 5.1 两种粘贴模式

| 模式 | 进入 | 选中 | 粘贴触发 | 粘贴后 |
|---|---|---|---|---|
| 普通（默认） | 标题栏模式文字点击切换 | 无强制选中 | **双击**记录 | 灰显，**位置不变** |
| 快速 | 同上 | 进入时默认选中第一条；单击仅切换选中（高亮） | **空格**（焦点不在搜索框/下拉时） | 灰显 + **移到列表最末** + 自动选中下一条未粘贴（支持连续空格连贴） |

AC 明确：单击在快速模式下**只选中不粘贴**，防误触。

### 5.2 标题栏三图标

| 位 | 功能 | C++ 动作 |
|---|---|---|
| ① 最左 收起 | 最小化到托盘，继续监听 | `ShowWindow(SW_HIDE)`，三窗口均不销毁 |
| ② 中间 悬浮 | 切 `TopMost`，开启态图标高亮；**启动默认开启**（C13） | `SetWindowPos` 置顶/取消；`topmost_` 初值 true，`DockToWorkArea` 用 `HWND_TOPMOST` |
| ③ 最右 关闭 | **彻底退出** | `AppContext::Exit`：卸钩子 → 复位光标 → 注销热键 → `RemoveClipboardFormatListener` → `NIM_DELETE` → 销毁窗口 → 消息循环退出 |

### 5.3 模式切换入口
标题栏文字「剪贴板 - 普通模式」/「剪贴板 - 快速模式」，**点击该文字切换**（非两个独立按钮）。
该文字区必须返回 `HTCLIENT`（否则点击变成拖拽，永远收不到 `WM_LBUTTONUP`）；标题栏拖拽区因此只剩"模式文字右侧 ~ 最左按钮"之间的空白（`kModeTextW = 136` 逻辑px 覆盖文字本身）。

### 5.4 其他交互（继承技术方案 §5.8）
- 空格：窗口级预处理按键（快速模式且有选中项）；焦点在 `EDIT`/`COMBOBOX` 时放行原生输入。
- Esc：点选模式取消。
- 右键菜单：**严格三项**（2026-10-04 用户选定）——「粘贴模式：普通/快速（点此切到…）」、「复制模式：一般/表格（点此切到…）」、
  「使用帮助」。置顶不走菜单（标题栏已有 ★），清除/复位不走菜单（带确认链的动作，误触代价与开关不对等）。
  菜单文字按当前状态动态生成并反向提示点击后果；取消（未选任何项）什么都不改。落码 `MainWindow::ShowMainMenu()`。
  焦点在搜索框（真 `EDIT` 子窗）时**放行原生编辑菜单**，不弹本菜单；点选进行中（`picking()`）也不弹。
  唤出：鼠标右键（`WM_CONTEXTMENU` 带屏幕坐标）与键盘 Menu 键——`VK_APPS` 交 `DefWindowProcW` 合成 `lParam=-1,-1`，
  菜单锚到列表区左上。**`Shift+F10` 有意不实现**：本窗没有菜单栏，F10 单按进系统菜单模式的后果不可见即不可测，
  按"只把实机验过的行为上线"的口径不放行（2026-10-04 决议）。
- 无边框自绘标题栏拖拽（`WM_NCHITTEST` 返回 `HTCAPTION`），按钮区返回 `HTCLIENT` 以**排除拖拽**。
- ~~首次呼出自动绑定目标窗口（`_boundOnce` 语义保留）~~ → **已作废（2026-10-04 用户决议）**：只有点过靶心才算绑定。
  理由：老语义会让"第一次呼出时所在的窗口"变成永久粘贴目标，之后从别的应用呼出仍往那个窗口打键，
  用户看上去就是"粘贴失灵"。现在未点选时目标恒为"呼出前那个窗口"，切应用即跟随。

---

## 6. 系统集成详细设计

### 6.1 运行期三窗口拓扑

| 窗口 | 类名/样式 | 职责 |
|---|---|---|
| `MainWindow` | `WS_POPUP`，无边框，380×600，默认贴右缘垂直居中 | 列表交互、`WM_HOTKEY` |
| `SuperClipMonitor` | `WS_POPUP`，0×0，`SetWindowPos(-32000,-32000)` | `WM_CLIPBOARDUPDATE(0x031D)` |
| `SuperClipTray` | `WS_POPUP`，0×0，同上 | 托盘回调 `WM_APP+1 (0x8001)`、`TaskbarCreated` |

**硬约束**：后两个必须是**真实 top-level 窗口，禁止 `HWND_MESSAGE` message-only 窗口**——message-only 窗口不接收广播，`TaskbarCreated`（Explorer 重启后托盘图标自愈）会丢失。三窗口同属一条 UI 线程。

### 6.2 剪贴板监听
- `AddClipboardFormatListener(monitorHwnd)`；失败仅记日志、不致命（手动粘贴仍可用，功能降级而非崩溃）。
- 收到 `WM_CLIPBOARDUPDATE` → **双层自写入过滤**（§6.4）→ 异步重试读取。
- 读取：`OpenClipboard` → `GetClipboardData(CF_UNICODETEXT)` → `GlobalLock` 拷出 → `GlobalUnlock/CloseClipboard`。非文本/空文本直接放弃，不浪费重试。
- **重试**：`SetTimer(25ms)`，最多 6 次。原因同原方案——Excel 等源程序复制瞬间独占剪贴板。每次重试间必须照常泵消息（不可 `Sleep` 阻塞线程）。
- 释放：`RemoveClipboardFormatListener` → 销毁窗口。

### 6.3 全局热键
```
RegisterHotKey(mainHwnd, 1, MOD_CONTROL, VK_OEM_3 /* 0xC0 */)
WM_HOTKEY(0x0312) → ToggleVisibility()：可见则 Hide，否则记录呼出前窗口 + Show + Activate + 列表获焦
退出时 UnregisterHotKey
```

### 6.4 粘贴机制（FR-11）与双层防护
```
DoPaste(item, moveToEnd):
  _internalPaste    = true                 // 第一层：标志位
  _lastPastedContent = item.content        // 第二层：内容比对兜底
  PasteService::PasteText(item.content, GetPasteTarget())
  Store::PasteDone(item, moveToEnd)
  粘贴完成后立即重置两个防护 + 1000ms 守护定时器兜底   // C4

WndProc(WM_CLIPBOARDUPDATE):
  if (_internalPaste) { _internalPaste = false; return; }
  读到文本后：if (text == _lastPastedContent) return;
  否则入列
```

`PasteText` 三步：
1. **写剪贴板**（`GlobalAlloc`+`SetClipboardData(CF_UNICODETEXT)`，先 `EmptyClipboard`）。目标为空或就是本进程主窗口 → 到此为止（防粘回自己）。
2. **夺回前台三段式**（绕开 Windows 前台锁）：`AllowSetForegroundWindow(targetPid)` → `SetForegroundWindow` + `SetActiveWindow` → 仍失败则 `SwitchToThisWindow(targetHwnd, TRUE)`。随后 **60ms 定时器**等待焦点稳定（Excel 切换有微延迟），期间 UI 不冻结。
3. **模拟键入**：`SendInput` 依序发 `VK_CONTROL↓ → VK_V(0x56)↓ → VK_V↑ → VK_CONTROL↑`。原方案用 `keybd_event`，本文改 `SendInput`（前者已被微软标记过时，行为等价，语义不变）。

全程 `try/catch` + 句柄守卫，失败静默，绝不影响程序运行。

### 6.5 目标窗口捕获与进程绑定
- `_lastExternalWindow`：`ShowWindow()` 唤起主窗前 `GetForegroundWindow()`（时机比 `Deactivated` 可靠）。
- 点选（靶心）：`SetSystemCursor(LoadCursorW(IDC_CROSS), OCR_NORMAL)` **系统级**替换光标 → 隐藏主窗 → `SetWindowsHookEx(WH_MOUSE_LL, ...)`（装在 UI 线程，回调可直接改状态）。
- 回调 `WM_LBUTTONDOWN` → `WindowFromPoint` → `GetAncestor(GA_ROOT)`（忽略目标程序内部子控件）→ 排除自身 → 记录 `_boundWindow` → `GetWindowThreadProcessId` + 查进程名（**仅进程名，不含文档标题**）→ `EndPick`。
- 复位：`UnhookWindowsHookEx` → `SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, 0)` **一键复位全部系统光标**（比逐个还原更可靠，杜绝十字残留）→ 恢复窗口。
- 降级链：钩子安装失败 → 直接绑 `_lastExternalWindow` 并在状态栏提示；Esc / 再点靶心 / 热键 / 8s 超时 / 锁屏 / 会话结束
  → 同一条 `Cancel()`：卸钩子、复位光标、恢复主窗，**已有绑定保持不变**（取消的是这次点选，不是上次的绑定）。
  注：点选期间主窗是隐藏且无前台，Esc 与"再点靶心"实际收不到按键/点击，可观测的取消入口是热键与 8s 超时。
- 状态指示：靶心红=未绑定、绿=已绑定；状态栏左侧显示绑定进程名，右侧显示 `vX.Y.Z  by Mr lin`（版本号在署名之前，agent.md 四.3；文本取自 `Config.h::kVersionText` + `kAppSignature`，右对齐格式 `Theme::MetaRight()`）。
- `GetPasteTarget()`：`IsWindow(_boundWindow)` 存活则用它，否则回退 `_lastExternalWindow`。
  **R5 语义调整（2026-10-04 用户决议）**：绑定窗口已关闭时**解绑**（靶心转红、状态栏清空），
  不再自动改绑呼出前窗口——那等于凭空造出一个用户没做过的绑定，会把粘贴打到非当前应用。


### 6.6 系统托盘（纯 Win32）
- `Shell_NotifyIconW`：`NIM_ADD/MODIFY/DELETE`，`NOTIFYICONDATA` 带 `uCallbackMessage = WM_APP+1`。
- 图标：`LoadImageW(exe, MAKEINTRESOURCEW(kIconIdApp=101), IMAGE_ICON, SM_CXSMICON…)`，失败回退 `LoadIcon(nullptr, IDI_APPLICATION)` 并记 `LogWarn`。
- `WM_LBUTTONDBLCLK` → 打开主窗口；`WM_RBUTTONUP` → `SetForegroundWindow(自身)` 后 `CreatePopupMenu`+`AppendMenuW`+`TrackPopupMenuEx(TPM_RETURNCMD)`（前置置顶是标准范式，保证点击菜单外可消失）→「打开 / 退出」。
- **Explorer 重启自愈**：`RegisterWindowMessageW(L"TaskbarCreated")`，收到即重新 `NIM_ADD`。
- 释放：`NIM_DELETE` + 销毁托盘窗口。

### 6.7 单实例与异常兜底
```
CreateMutexW(nullptr, TRUE, L"Global\\SuperClip_SingleInstance_9F3A2B1C")
若 ERROR_ALREADY_EXISTS：
   FindWindowW(nullptr, L"SuperClip") → IsIconic 则 ShowWindow(SW_RESTORE) → SetForegroundWindow → 本进程退出
```
- `SetUnhandledExceptionFilter` + `_set_append_to_environment/terminate_handler` +（可选 `_CxxSetUnhandledExceptionFilter`）三层兜底。
- 未处理异常 → 追加写 `%AppData%\SuperClip\error.log`（时间 + 模块 + 栈回溯 `StackWalk64/SymFromAddr`）+ 弹窗；UI 线程 SEH 保护的消息处理尽量吞掉以保持存活。

### 6.8 Win32 API 依赖清单（对齐技术方案 §8）

| 功能域 | API |
|---|---|
| 剪贴板监听 | `AddClipboardFormatListener` / `RemoveClipboardFormatListener` |
| 剪贴板读写 | `OpenClipboard` / `EmptyClipboard` / `SetClipboardData` / `GetClipboardData` / `IsClipboardFormatAvailable` / `GlobalAlloc/Lock/Unlock/Free` |
| 前台控制 | `GetForegroundWindow` `SetForegroundWindow` `SetActiveWindow` `AllowSetForegroundWindow` `SwitchToThisWindow` `ShowWindow` `IsIconic` `FindWindowW` `IsWindow` `GetWindowThreadProcessId` `GetAncestor` |
| 键盘模拟 | `SendInput`（`VK_CONTROL` / `VK_V` / `KEYEVENTF_KEYUP`） |
| 全局热键 | `RegisterHotKey` / `UnregisterHotKey` |
| 托盘 | `Shell_NotifyIconW` `RegisterWindowMessageW("TaskbarCreated")` `LoadImageW` |
| 弹出菜单 | `CreatePopupMenu` `AppendMenuW` `TrackPopupMenuEx` `DestroyMenu` `GetCursorPos` |
| 低层鼠标钩子 | `SetWindowsHookExW(WH_MOUSE_LL)` `UnhookWindowsHookEx` `CallNextHookEx` `WindowFromPoint` |
| 光标 | `LoadCursorW` `SetSystemCursor` `SystemParametersInfoW(SPI_SETCURSORS / SPI_GETWHEELSCROLLLINES)` |
| 置顶/窗口 | `SetWindowPos` `GetSystemMetrics` `MonitorFromWindow`/`GetMonitorInfo`（多显示器工作区） |
| 哈希/唯一 ID | `BCryptOpenAlgorithmProvider/Hash`、`CoCreateGuid` |
| 原子写 | `ReplaceFileW` / `MoveFileExW` |
| DPI | `SetProcessDpiAwarenessContext(PER_MONITOR_V2)`，Win7/8 回退 `SetProcessDPIAware` |

---

## 7. UI 规范与自绘实现

```
┌──────────────────────────────────────────────────┐
│  剪贴板 - 普通模式            [收起] [悬浮] [关闭]  │ ← 模式文字可点击切换
├──────────────────────────────────────────────────┤
│  [ 🔍 搜索框........ ]                             │ ← EDIT 子控件（IME 完好）
│  [ 全部▾ ] [ 清除 ] [ 复位 ] [ 🎯绑定 ]            │ ← 工具栏
├──────────────────────────────────────────────────┤
│  1  文本内容预览…                   12:30:00  ☆   │ ← 星标独占最右一列、垂直居中
│  2  400.00                          12:29:00  ☆   │ ← 表格单元格：来源标注不上屏（仅参与搜索）
│  3  普通文本…                       12:28:00  ☆   │
│  …（已粘贴项灰显 + 半透明；收藏项只在【收藏】视图出现）│
├──────────────────────────────────────────────────┤
│  已绑定：EXCEL            v2.0.3  by Mr lin       │ ← 状态栏 22px：版本号在署名之前
└──────────────────────────────────────────────────┘
```

**渲染管线**：`ID2D1HwndRenderTarget` + `IDWriteTextFormat/Layout`。单一后端，不做 GDI+ 双实现；D2D 创建失败则记 `error.log` 并提示"需 Win7 SP1 或更高"，这是唯一降级路径。`WM_PAINT` 内 `BeginDraw/EndDraw`，脏区失效（状态变化只 `InvalidateRect` 受影响行矩形），命中测试用行矩形缓存数组（数据变化时重建）。

**度量**（96dpi 基准，运行期乘 `DpiY/96`）：标题栏 36 / 工具栏自适应 / 列表 * / 状态栏 22。

**样式状态 → 绘制参数**（原 WPF `DataTrigger` 的等价表，不做样式系统）

| 状态 | 绘制 |
|---|---|
| 普通 | 白底、圆角 6px、边框灰 `#C9CFD8`（2026-10-04 用户指定，替代原契约的 `#272C36`；工具栏按钮与气泡边框同色） |
| 选中（快速模式） | 描边 `#00897B`（保持青色，否则看不出当前行） |
| 收藏 | 底色 `#FFF8E1`；**只在【收藏】视图出现**，`全部/文本/表格单元格` 三个视图一律不显示（2026-10-04 用户决议，原"置顶分组 + 分隔线"作废，见 §0.1 C10）；**不参与「清除」也不参与末位淘汰**（C12） |
| 已粘贴 | 前景转灰 + 整行不透明度 0.55 |
| 悬停 | 浅底 `#F5F7FA` |

**行内内容**：序号（当前显示位置 +1）→ 主内容（**固定单行**、超出用 `…`；2026-10-04 用户指定，原"最多 3 行"作废。**不用 `DWRITE_TRIMMING`**——实测 `LINE_BY_LINE` 被 `dwrite.dll` 拒绝、`CHARACTER` 无论 `count` 都只给单行，改为对字符数二分求"单行内最长前缀 + `…`"，见技术方案 §6.4 与本文 C9）→ 时间戳 `HH:mm:ss` → ★/☆ 按钮（**独占最右一列**：整列高度都是命中区，字形在该列内**垂直居中**；2026-10-04 用户指定，原"右上角与时间同高"作废）。

**来源标注不再上屏**（2026-10-04 用户决议，见 §0.1 C11）：`来自表格：第 X 行 第 Y 列` 一行小字原先画在正文下方，因"影响整体美观、也没有实际意义"取消；行高因此不再随标注浮动（所有行等高）。但 `ClipItem::SourceLabel()` 与 `foldLabel` **保留**，`Store::RebuildDisplay()` 仍用它做搜索命中——用户在同一决议里明确"不显示，但仍参与搜索"，所以搜 `来自表格` / `第 2 行` 依旧能捞到对应单元格（单测 §9.4-14 钉住该行为）。`SourceRow/SourceCol` 继续读写与持久化。

**悬浮全文气泡**：仅对"被截断"的行生效。光标停在行上 400ms 后，在该行下沿浮现自绘气泡（`HoverTip`，白底 + `#C9CFD8` 边框 + 正文 `#272C36`），显示条目**完整内容**（保留换行，最长 2000 字，超出补 `…`；宽 ≤320 / 高 ≤240 逻辑px，超框按放得下的最长前缀裁剪）。`WS_EX_TOPMOST|TOOLWINDOW|NOACTIVATE|TRANSPARENT`：不抢焦点、点击穿透。滚轮、数据变化、鼠标离开、点击、窗口收起即刻收起；未截断的行不弹。

**占位提示**：搜索框为空时在 `EDIT` 上方叠加自绘文字，非空时失效隐藏（对应原 `EmptyToVisibleConverter`）。

**帮助窗**：`HelpWindow` 无边框模态，9 步静态引导（呼出热键、自动记录、双模式、收藏、绑定、复制模式、搜索、清除/复位、置顶）。
实现口径（2026-10-04 步骤 11 实机确认）：`WS_POPUP` + `WS_EX_TOOLWINDOW|TOPMOST`、主窗的 owned window，
420×300 逻辑px，**贴主窗左侧**（左侧放不下才回落右侧/居中，最后 `FitRectToDesktop` 钳回工作区）；
模态靠 `EnableWindow(主窗, FALSE)` 实现，**不起嵌套消息循环**（全程序只有一个泵，见技术方案 §6.1）；
翻页 `上一步/下一步` 在首末位钳住并禁用按钮，`N / 9` 计数画在标题带右侧 44 逻辑px 位内，重新打开回到 `1 / 9`；
`Esc` 与「关闭」都还原主窗可用并把焦点交回。实机已验：鼠标点击与 `VK_RIGHT` 翻页、钳位、模态、还原；
**未验**：150%/200% DPI 下的排版（沿用步骤 6 的遗留项）。

**交互细节保留**：标题栏按钮区不触发拖拽；搜索框上右键保留原生编辑菜单；筛选下拉选中值手动同步到 Store（原实现为规避双向绑定失步，C++ 下天然单向下发）。

---

## 8. 持久化规范

### 8.1 文件布局（与 .NET 版同目录，可直接接管老用户数据）
```
%AppData%\SuperClip\
├── history.json     剪贴板历史，顺序 = 当前显示顺序（含沉底）
├── settings.json    用户设置（v2.0.2 新增项，保留）
└── error.log        全局异常日志（追加）
```

### 8.2 history.json 兼容性（**硬要求**）
```json
[ { "Id":"3f2a...", "Content":"excel_value", "Type":1,
    "SourceRow":2, "SourceCol":3,
    "Timestamp":"2026-09-18T10:00:00.123",
    "Hash":"ab12...64hex", "IsFavorite":false, "IsPasted":true } ]
```
- 字段名/大小写/顺序照抄；`Type` 按数值（0/1）；`Id` 小写带连字符 GUID。
- `Timestamp` 写本地时间 `YYYY-MM-DDTHH:MM:SS.mmm`（无时区后缀）；**读时兼容带毫秒/带时区后缀/无后缀三种形态**。
- 原 .NET 版由 `System.Text.Json` 默认将非 ASCII 转义为 `\uXXXX`，C++ 版写侧用不转义的 UTF-8、**读侧必须同时吃 `\u` 转义与明文**（自研 reader 的 `\u` 分支已实现，含代理对拼接，见 `src/core/Json.cpp`；**该分支无专门单测**，互通由实机读旧 `history.json` 验证）。
- 内存 UTF-16 ↔ 文件 UTF-8 显式转换，禁止依赖 locale。

### 8.3 settings.json（继承 v2.0.2）
`Left/Top`、`Width/Height`(默认 380/600)、`BoundProcessName`、`Topmost`(默认 true)、`PasteMode`(0 普通)、`FilterType`(0)、`SplitSingleColumn`(false)。持久化时机：窗口关闭存位置/大小/置顶；模式、筛选、复制模式变更即存；绑定目标变更存进程名，启动时按进程名尝试找回窗口句柄恢复绑定。

实现细则（2026-10-04 步骤 10 落定，实机已验）：

- **坐标单位是 DIP（96dpi 基准）**，与 .NET 版一致；读时 `MulDiv(x, dpi, 96)` 还原成物理像素，写时 `MulDiv(x, 96, dpi)` 折回。键顺序、`BoundProcessName` 无绑定时写 `null` 的形态都照抄，保证同一份文件两版交替读写不产生差异。
- **逐字段作废，不整份丢弃**：文件缺失/空/解析失败/非对象 → 全默认；单个字段类型不符或越界（坐标 `|x|>1,000,000`、尺寸不在 `[1,8192]`、`PasteMode` 只吃 0/1、`FilterType` 只吃 0..3）→ 只有该字段回默认并记 `error.log`，未知键忽略。理由：一个坏键不该抹掉用户的窗口尺寸。
- **还原规则**：有有效矩形且 `FitRectToDesktop` 通过 → 直接落到该物理矩形；矩形完全不在任何显示器上（拔掉副屏）→ 回默认停靠（贴工作区右缘、垂直居中）。`FitRectToDesktop` **只把露不出去的那一边推回工作区**，不得无条件对齐到工作区左上角——实测那样会把用户存的 `100,120` 抹成 `0,0` 并回写。
- **恢复绑定**：按进程名枚举候选顶层窗（跳过不可见、有 owner、`WS_EX_TOOLWINDOW`、无标题、自身窗口）。**唯一候选才绑**；同名多窗口一律不自动绑，靶心保持红色并给状态栏提示"X 有 N 个窗口，未自动绑定，请点靶心重新选择"（2026-10-04 用户决议：绑错窗口比不绑更糟，与 §5.4"点选才算绑定"同源）；零候选（那个应用没开）静默，进程名留着下次启动再试。
- **落盘时机的实测差异**：★ 置顶、模式、筛选、绑定变更**即写**，位置/尺寸在退出时写，所以运行中直接读文件看到的 `Left/Top` 可能仍是本次启动时那份。
- `SplitSingleColumn` 的**写入入口已落地（步骤 11，2026-10-04）**：右键菜单「复制模式」项切换并即写盘（`SaveCopyMode()`）。
  实机已验该键 false→true 落盘；**反向（true→false）同样走这一条路径，但未单独复验**。表格拆条的 UI 开关仍无独立按钮，只有这一处菜单入口。

### 8.4 原子性与容错
- 写：序列化为紧凑单行 → `history.json.tmp` → 主文件存在则 `ReplaceFileW`，否则 `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`；任一步失败静默并尝试清理 `.tmp`。**持久化失败绝不中断主流程。**
- 读：文件不存在 / 空 / 解析失败 / 任何异常 → 返回空列表。**损坏的 history.json 不得导致启动失败**（代价是历史丢失，剪贴板历史非关键数据，与原方案同一取舍）。
- 启动：加载 → 置顶重排一次 → 规范化保存 → 刷新显示。
- Save/Load 在 UI 线程同步执行（≤500 条、百 KB 级，实测可忽略）；如提高上限再改防抖后台写（原 R7）。

---

## 9. 线程与并发模型

**单 UI 线程 + 定时器让出**，无工作线程、无锁：

| 组件 | 线程 | 说明 |
|---|---|---|
| 监听/托盘窗口 `WndProc` | UI 线程 | 消息在宿主线程泵出 |
| 剪贴板读取重试 | UI 线程（分段） | `SetTimer(25ms)` 替代 `await Task.Delay`，重试间照常泵消息 |
| 粘贴流程 | UI 线程（分段） | 60ms 定时器等待焦点切换 |
| `WH_MOUSE_LL` 回调 | UI 线程 | 钩子装在 UI 线程，回调编组回本线程，可直接改 UI 状态 |
| 定时器清单 | UI 线程 | 搜索防抖 300ms、粘贴守护 1000ms、读取重试 25ms×6、焦点等待 60ms |
| 磁盘 IO | UI 线程同步 | 同 §8.4 |

`_internalPaste`、`_lastPastedContent`、`_boundWindow` 等共享状态**无锁访问的前提是全部读写在同一 UI 线程串行化**；三个窗口必须都在 UI 线程创建，否则该假设失效。

---

## 10. 项目结构

```
SuperClip/                            # 本仓库（C++ 重写版；下面是 2026-10-05 的实际落地结构，非规划图）
├─ cpp/
│  ├─ CMakeLists.txt                  # 源清单与链接库清单的唯一来源
│  ├─ build.bat                       # MSVC 出包（自动定位 vcvars64）
│  ├─ build-tests.sh                  # WSL mingw 交叉构建：内部即 cmake --build，不再手抄清单
│  ├─ src/
│  │  ├─ main.cpp                     # 单实例、消息循环、托盘与窗口装配入口
│  │  ├─ app/AppContext.h/.cpp        # 生命周期、服务编排、ExitApp 清理链
│  │  ├─ core/ Config.h · IStoreStorage.h（纯头）· ClipItem · Store · TableParser · Text
│  │  │        Time · Sha256 · Settings · Json（自研极简 JSON，.h/.cpp 成对）
│  │  ├─ services/ ClipboardMonitor · PasteService · StorageService · TrayService · ProcessPicker
│  │  ├─ native/ AppDirs · Clipboard · HiddenWindow · SystemInfo（dwmapi/shcore 走 LoadLibrary）
│  │  │        ComPtr.h · Foreground.h · Keyboard.h · Uuid.h · WinUtil.h（RAII 与纯内联工具）
│  │  ├─ ui/ MainWindow · ListRenderer · Theme · HoverTip · HelpWindow
│  │  ├─ res/ app.rc（VERSIONINFO + 101 ICON）· app.manifest（PerMonitorV2 + comctl6）· SuperClip.ico
│  │  └─ util/ Log.h/.cpp
│  ├─ tests/test_main.cpp             # 40 例，自带极简断言器（无 Catch2）
│  ├─ tools/PasteTarget.cpp           # 粘贴闭环走查用的极简目标程序
│  ├─ qa/*.ps1                        # 实机走查驱动（内容一律 ASCII）
│  ├─ scripts/                        # 发布与验收脚本（CleanAndBuild / PackageRelease / ReleaseChecklist）
│  └─ installer/ install.bat · uninstall.bat
└─ doc/                               # 设计契约、项目状态、测试与审计记录
```

依赖规则（继承技术方案 §2.2）：`ui → core → services → native` 单向；`Store` 不含任何窗口类型；服务层不反向引用 UI（`CopyMode` 开关由 UI 层写入服务）；**Win32 符号只出现在 `native/`、`services/`、`ui/` 的白名单清单内**，便于按 §1.2 审计"零网络"。

---

## 11. 构建与发布

### 11.1 编译配置
```
cl.exe /std:c++20 /EHsc /MT /O2 /GL /W4 /utf-8 /sdl /guard:cf
link /SUBSYSTEM:WINDOWS /LTCG
     user32 kernel32 shell32 gdi32 advapi32 ole32 oleaut32 uuid bcrypt d2d1 dwrite wtsapi32
```
链接库以 `cpp/CMakeLists.txt` 为准（此处只是要点；`dwmapi`/`shcore` 走 `LoadLibraryW` 动态加载不入链，`comdlg32` 本项目不调用）。
- `/MT` 静态 CRT → 目标机免装 VC++ 运行库（AC-7）
- `/utf-8` → 源文件中文常量与执行字符集一致，杜绝乱码
- manifest 内声明 `PerMonitorV2` DPI 与 comctl32 v6
- 产物体积预期 **1~3 MB**（原 .NET 版 130 MB，R2 消除）。旁证：mingw 交叉构建实测 `-O1` 3.51 MB、`-O3` 3.56 MB，均未达 3 MB；MSVC `/O2 /GL /MT` 版待步骤 12 B 实测

### 11.2 脚本体系（平移，行为一致）
| 脚本 | 作用 |
|---|---|
| `build.bat` | 检查 MSVC/CMake → 配置+构建 Release |
| `CleanAndBuild.bat` | 删 `build/` 再构建（发布前推荐） |
| `PackageRelease.bat` | CleanAndBuild → 从资源/头文件抓版本 → 汇集 exe + README + install/uninstall.bat → `release\SuperClip_vX.Y.Z_portable.zip`（**英文命名**，附件名不支持中文） |
| `installer\install.bat` | 复制到 `%ProgramFiles%\SuperClip\` + 开始菜单/桌面 `.lnk` |
| `installer\uninstall.bat` | `taskkill` → 删目录与快捷方式 |

版本元数据：`.rc` `VERSIONINFO` 提供 `FileVersion`/`ProductVersion`，`StringFileInfo` 填 Product/Company/Copyright/Description（右键属性可见）。产物保留固定名 `SuperClip.exe`（进程名、installer、单实例 `FindWindow` 均依赖固定名）+ 版本化分发副本 `SuperClip_v{Ver}.exe`。

### 11.3 与原方案的兼容性
同目录同文件名读写 `history.json`/`settings.json`，C++ 版与 .NET 版可互换使用同一份数据（§8.2 为硬约束）。

---

## 12. 质量保障机制（继承 + 补强）

| 机制 | 位置 | 说明 |
|---|---|---|
| 剪贴板读取重试 6×25ms | ClipboardMonitor | 对抗 Excel 短暂独占 |
| 自粘贴双层防护 + 立即重置 + 1000ms 兜底 | MainWindow/AppContext | 杜绝"粘贴→触发监听→重复入列"回环（C4） |
| 原子持久化 | StorageService | tmp + `ReplaceFileW`，崩溃不损坏主文件 |
| 前台切换三段兜底 | PasteService | `AllowSetForegroundWindow` → `SetForegroundWindow/SetActiveWindow` → `SwitchToThisWindow` |
| 粘贴目标失效回退 | AppContext | `IsWindow` 校验 + 自动改绑（R5） |
| 托盘自愈 | TrayService | `TaskbarCreated` 重挂 |
| UI 不闪烁、选中/滚动不丢 | ListRenderer/Store | §4.6 原地同步 |
| 全局异常兜底双通道 | main.cpp | `error.log` 留痕 + 尽量存活 |
| 逐级降级不崩溃 | 多处 | 监听注册失败、托盘失败、钩子失败、光标替换失败均静默降级 |

### 12.1 测试
`tests/test_main.cpp` 自带的极简断言器（`CHECK`/`CHECK_EQ` + 计数汇总），**零第三方**（原计划的 Catch2 未引入）。
下表为 2026-10-05 交叉构建产物在 Windows 实跑后的**实际**分组与数量（原规划 33 例的口径已过期）：

| 用例组 | 数量 | 覆盖 |
|---|---|---|
| §9.2 `TableParser` IsTable/Parse | **13** | 表格识别、拆分、空单元格/空行、`\r\n`/`\r` 规范化、`CopyMode` 两态、列号不前移 |
| §9.3 `Sha256::Hex` + `ClipItem::SourceLabel` | **8** | 已知向量、空内容不计哈希、中文稳定小写 hex、不同内容不同哈希、长文本；普通文本无标注、单元格标注、无行列防御 |
| §9.4 `Store` 状态机 | **15** | 去重与收藏态迁移、插入位＝收藏数量、末位淘汰（C7）、收藏永不淘汰、稳定分区保序、表格整块不倒序、快速沉底（非收藏／收藏区末位 C8）、存盘重载顺序一致、`Reset` 清灰显＋时间降序（C5）、全角/大小写折叠搜索、过滤+搜索+选中保持、收藏仅在【收藏】视图（C10）、标注不上屏仍参与搜索（C11）、清除保留收藏（C12） |
| §9.6 `Settings` 往返 | **4** | .NET 文件读入并逐字节写回、全字段往返（含绑定进程名）、缺失/损坏回落默认、越界与类型不符按字段作废 |
| **合计** | **40 例 / 214 断言 / 0 失败** | —— |

**UI 层不做条件编译式打桩**：需要真实 Windows 桌面实机验证（无法在本环境完成的部分，交付时逐项标注"未验证"）。

---

## 13. 验收标准映射

| # | 标准 | C++ 验证方式 |
|---|---|---|
| AC-1 | Excel 复制后自动拆单元格并标注行列 | 单测（TSV 输入）+ 实机 Excel 复现 |
| AC-2 | 双击/空格粘贴到目标光标处；快速粘贴沉底变灰 | 实机：记事本、Excel、浏览器三类目标 |
| AC-3 | 重启后历史/收藏/灰显/**排序**全保留；按「清除」后**收藏条目仍在**（C12） | 实机 + `history.json` 与 .NET 版互读 |
| AC-4 | 悬浮置顶开/关，且**启动默认开**（C13） | 实机（`GWL_EXSTYLE` 的 `WS_EX_TOPMOST` 位 + ★ 图标下划线） |
| AC-5 | 每行显示当前顺序序号 | 实机（过滤/搜索后序号随显示位置变化） |
| AC-6 | 普通双击、快速空格均命中光标处 | 实机 |
| AC-7 | 任意 Win x64 免运行时双击即用、常驻托盘 | 干净虚拟机（未装 .NET/VC 运行库）测试 |
| AC-8 | 全程零网络 | 链接期无网络库符号 + 防火墙出站拦截后功能无变化 |

---

## 14. 关键决策记录（ADR）

| 决策 | 结论 | 理由 |
|---|---|---|
| **技术栈** | **C++20 + Win32/Direct2D，静态 CRT** | 用户改选；消除 .NET Core 3.1 EOL（R1）与 130 MB 体积（R2）；免运行时目标不变 |
| UI 路线 | 纯自绘（路线 A） | 用户选定；只有自绘能 1:1 还原灰显半透明/浅黄收藏底/圆角卡片等 UI 规范 |
| 文本输入 | 搜索框用真 `EDIT` | IME 合成不可靠自绘 |
| 托盘 | 纯 `Shell_NotifyIcon` | C1 |
| 监听窗口 | 独立隐藏 top-level 窗口 | C2；且**禁 message-only**（否则丢 `TaskbarCreated` 广播） |
| 键入模拟 | `SendInput` 取代 `keybd_event` | 行为等价，后者已过时 |
| JSON | 自研极简 JSON（约 180 行），读侧兼容 `\u` 转义与明文 | 与 .NET 版数据互通；原 ADR 的 RapidJSON 因离线取不到包作废 |
| 哈希 | BCrypt | 系统自带，无第三方 |
| 粘贴语义 | 写剪贴板 + 夺前台 + 模拟 Ctrl+V | 继承，兼容性最好 |
| 快速模式触发 | 单击选中 + 空格粘贴 | 继承，防误触 |
| 清除语义 | 只清非收藏区，收藏永久保留 | 原取值"全清（含收藏，选项 B）"于 2026-10-04 被 C12 推翻（与 FR-08"不参与清理"同源） |
| 复位语义 | 清灰显 + 收藏与非收藏均按时间降序 | C5 |
| 排序持久化 | 按显示顺序序列化（含沉底） | 继承 AC-3 |
| 开机自启 | 不做 | 用户明确不要 |
| 分发形态 | 单文件静态链接 win-x64 | 继承"免运行时"目标 |
| 淘汰方向 | 末位淘汰 | C7，FR-04 需求原文优先 |
| T1 单实例命名空间 | `Local\SuperClip_SingleInstance_9F3A2B1C` | `Global\` 需 `SeCreateGlobalPrivilege`，标准用户创建会失败；语义变为**每登录会话单实例** |
| T2 点选点击处理 | 吞掉该次点击（钩子返回 1） | 避免点中 Excel 单元格/按钮改变目标选区；代价是该点击不再激活目标窗口 |
| T3 单条内容上限 | 截断至 256 KB 入列 | 保护 UI 排版与磁盘体积；**偏离** FR-01 字面"记录复制的纯文本内容"，已记录 |
| T4 目标窗口最小化 | 粘贴前 `SW_RESTORE` | 否则 `SetForegroundWindow` 对最小化窗口无效，内容落到错误位置 |
| T5 热键被占用 | 托盘气泡提示（`NIM_MODIFY`+`NIF_INFO`） | 静默失败会让用户以为程序坏了 |
| T6 列表方向键导航 | **不做** | 契约未要求，快速模式仅"单击选中 + 空格粘贴" |
| 搜索比较方式 | 入列时预计算折叠键（全角→半角 + ASCII 大小写），运行期纯 `find` | 内容不可变 → 缓存天然安全；避免每字符 O(n) 折叠；明确不对西里尔/希腊字母折叠（中文场景无损） |
| 灰显实现 | 前景/底色**预混合**，不用 `PushLayer` | `PushLayer` 会使 ClearType 降级为灰度抗锯齿，中文发糊 |
| 筛选下拉 | `TrackPopupMenuEx` 弹出菜单，不用 `COMBOBOX` | 与自绘风格一致、免主题割裂、代码更短 |
| 搜索框占位提示 | `EM_SETCUEBANNER` | 替代原 `EmptyToVisibleConverter` 的叠加绘制，IME 与焦点行为由系统保证 |
| 图标绘制 | D2D 几何路径（窗口按钮/靶心），★☆ 用字形 | Win7 无 `Segoe MDL2 Assets`，跨版本字形宽度不稳 |
| 栈回溯 | `CaptureStackBackTrace`，不链接 `dbghelp` | 免符号搜索路径与潜在网络符号下载，保持 AC-8 |

---

## 15. 边界条件与风险

**继承原边界**：仅纯文本（图片/文件/富文本忽略）；连续相同复制只留一条；空单元格拆分时跳过；焦点在搜索框时空格输入空格不触发粘贴；严禁网络库。

**换栈后的新增/残留风险**

| # | 风险 | 影响 | 缓解 |
|---|---|---|---|
| N1 | 自绘列表无 UIA 无障碍 | 读屏软件不可用（WPF 免费提供） | 需求未要求；如需要再实现 `IRawElementProviderSimple`，约 2~4 天。**高对比度已覆盖**：`SPI_GETHIGHCONTRAST` 探测后改用 `GetSysColor` 系统色并关闭预混合灰显 |
| N2 | 每显示器 DPI 缩放与字体度量需自管 | 高分屏下文字/间距比例异常 | manifest `PerMonitorV2` + Win7 回退；布局统一走 96dpi 基准缩放函数 |
| N3 | GDI/D2D 对象、钩子、剪贴板句柄泄漏 | 长时间常驻内存增长 | 全部句柄走 RAII 守卫；退出清理链固定顺序；用任务管理器长测 |
| N4 | `SwitchToThisWindow`、`SetSystemCursor` 属未公开/半公开 API | 未来 Windows 行为变化 | 仅作兜底，前两段通常已生效（原 R9 同） |
| N5 | 目标窗口为管理员权限或全屏 | 粘贴可能落到错误窗口或静默失败 | UIPI 限制，非提权进程无法注入；与原方案同等边界，失败静默 |
| N6 | Win7 无 SP1 / 无平台更新的极老机器 D2D 不可用 | 启动失败 | 自检失败写日志并明确提示；Win7 下限已含 SP1（C6） |
| N7 | UI 层工时集中在列表渲染与交互编排 | 交付周期约为 .NET 版 2.5~3 倍 | §16 里程碑分批可验证，M4 单独留缓冲 |
| N8 | `技术方案.md` §5.7/§6.1 仍留有 C7 同源矛盾表述（"非收藏区最前 = 最旧"、"超限从非收藏区最前淘汰"） | 若有人按该文档字面实现会误删最新条目 | `设计规范` v1.1 已勘误；本文按末位淘汰。**`技术方案.md` 尚未同步**，待授权后一并改正 |
| N9 | 步骤 8 实机观察：目标为 WPF 应用（PowerShell ISE）时，"粘贴后重新唤起主窗再按空格"有 2/9 次**完全未进入粘贴分支**（`error.log` 无该次记录），重试即成功；自建 Win32 宿主未复现 | 快速连贴在个别第三方应用上可能"按一下没反应" | 疑与再夺前台瞬间按键归属有关，**未定论**；步骤 12 前用多目标多样本复测，若确证则在 `ID_FOCUS_WAIT` 后校验前台归属并重发一次 |

**已随换栈消除**：R1 .NET EOL、R2 130 MB 体积、R3/R4/R5/R6（历史缺陷已在本文取修复后语义）、NETSDK1168 剪裁限制、`InvariantGlobalization` 与 IME 崩溃类风险。

---

## 16. 里程碑

| 阶段 | 交付 | 可验证 FR/AC |
|---|---|---|
| M1 | 三窗口 + 消息循环 + 单实例 + 监听 + 热键 + 托盘 + 异常兜底 | FR-01/02/03/04/15/16 |
| M2 | `TableParser`/`Store`/`Sha256` + 21+12 例单测全绿 | AC-1 逻辑层 |
| M3 | 自绘窗口与列表、搜索防抖、过滤、收藏、清除、复位、序号、悬浮 | AC-3/4/5、FR-06..08/12..14/17 |
| M4 | 跨进程粘贴（双层防护、三段夺前台、目标捕获、进程绑定点选） | AC-2/6、FR-09/10/11 |
| M5 | `settings.json`、DPI、帮助窗、打包与 installer 脚本 | AC-7/8，出包 |

M1+M2 完成即有一份"无界面但逻辑可测"的可运行内核；M4 是风险最高阶段（前台锁、UIPI、目标程序差异），单独留缓冲。

---

## 17. 交付声明

本文是 SuperClip C++ 版的完整实现契约：§2 的 FR 语义、§4 的算法、§5 的状态机、§6 的 Win32 集成、§7 的 UI、§8 的持久化与数据互通、§12 的测试与 §13 的验收条款，任一开发者或 AI 依此即可在 Windows + MSVC 环境从零重建可运行应用，无需参考既有源码。§0.1 的 **12 项矛盾取值（C1–C7、C9–C13；C8 记在技术方案 §3.3）**、§14 的 **T1–T6 决议**与全部 ADR 为强制约定，实现者不得自行改回。模块级接口、消息路由表、降级矩阵与构建脚本细节见配套文档 `C++_技术方案.md`。
