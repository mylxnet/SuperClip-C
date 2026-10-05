# SuperClip C++ 版技术设计方案

> 本文件原名 `C++_设计方案.md`，2026-10-05 随 agent.md 九.2 的目录标准移入 `doc/` 并改名（正文未改）。
> 仓内其它文档提到的「设计方案 / `DESIGN` / 契约级文档」都指本文。
>
> 版本：契约 v1.0（设计冻结）· 对应产品 **v2.0.3**（M1 步骤 1–12 A 段已落地，实机状态见 `doc/PROJECT_STATE.md`）
> 平台：Windows 7 SP1 / 10 / 11（x64）· 形态：桌面应用 · 单文件 exe · 零网络
> 依据：`doc/SuperClip_设计规范.html`（当前 v1.2：FR/AC 契约，含 §4.3/§4.4 的 C7/C10/C12 勘误）+ `doc/技术方案.md`（.NET v2.0.2，行为权威）
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
| C7 | 自动清理淘汰位置 | 伪码 `First(!IsFavorite)`（注释"最前=最旧"），与同文 §4.3"最新在上"、FR-04"删最旧"自相矛盾 | 同样自相矛盾（§5.7"各区最新在前"+"非收藏区最前=最旧"） | **末位淘汰**（删非收藏区末尾 = 最旧）。需求原文 FR-04 优先于伪码；`设计规范` 已出 v1.1 勘误。`doc/技术方案.md` 尚未同步，见 §15 N8 |
| C9 | 主内容"多行 + 省略号"的实现 | 本文 §7 曾写 `TRIMMING_GRANULARITY_CHARACTER` + `MAXIMUM_LINE_COUNT=3` | 技术方案 §6.4 曾写 `LINE_BY_LINE` + `GetLineMetrics` 回退 | **两者都不可用**（Win11 26100 实测：`LINE_BY_LINE` 在 format/layout 级均 `E_INVALIDARG`；`CHARACTER`/`WORD` 无视 `count` 固定压成单行）。改为对字符数二分求"高度上限内最长前缀"再补 `…`，一次算好进 `LayoutCache`，两工具链同一条路径。**2026-10-04 后续**：上限由 3 行改为**单行**（用户指定），完整内容改由 `HoverTip` 气泡给出；`CHARACTER` 裁剪"只能压成单行"这一实测结果反而成了单行方案的依据 |
| C10 | 收藏条目的显示位置 | `FR-08`"收藏项**置顶分组显示**"、`设计规范` §7"收藏项浅黄背景高亮，且始终位于列表前部" | 与"普通列表不应被收藏项长期占位"的诉求冲突 | **收藏条目只在【收藏】视图显示**，`全部/文本/表格单元格` 三个视图一律剔除（2026-10-04 用户决议）。数组的 `[收藏区 \| 非收藏区]` 分区不变式**原样保留**——持久化顺序、`Boundary()` 插入位、`MoveToBack` 沉底、FR-04 末位淘汰都依赖它，改的只是 `RebuildDisplay()` 的过滤条件；收藏区/普通区之间的分隔线随之删除（`Theme::Separator` 画刷一并移除）。副作用：点星标后条目立刻离开当前视图，故切换收藏时状态栏给 3s 去向提示（`ID_STATUS_HINT`） |
| C11 | 表格来源标注是否上屏 | 本文 §7 曾写"正文下方小字浅蓝 `#5B9BD5` 标注 `来自表格：第 X 行 第 Y 列`"（该色值本身是 2026-10-04 上午刚按用户意见从 `#1E88E5` 调浅的） | 用户复核后认为它"影响整体美观度，而且也没有什么实际意义" | **标注不再绘制**，行高不再随标注浮动（技术方案 §6.4 的 `cardHeight` 去掉 `kLabelGap + kLabelH` 项）；`kLabelH`/`kFontLabel`/`Theme::Label()`/`LabelText()`/`labelPasted_` 全部删除。**但仍是搜索字段**（用户同一次决议补充"不显示，但仍参与搜索"）：`SourceLabel()`、`foldLabel`、`RebuildDisplay()` 的第二个 `ContainsFolded` 与 `SourceRow/SourceCol` 持久化一律保留，单测 §9.3-06/07/08（标注文本）+ §9.4-14（隐藏字段命中）共同钉住 |
| C12 | 「清除」是否清掉收藏 | `FR-13`"清空所有记录（**含收藏**）" | 与 `FR-08`"收藏……**不参与清理**"直接矛盾（规范自身两处口径相反），用户裁定收藏要永久保存 | **`ClearAll()` 只删非收藏区**，收藏区原序保留并落盘，返回值改为"实际删除条数"；清除后在状态栏给 3s 提示（收藏在【全部】视图不可见，不提示会像"按了没反应"）。想彻底删一条收藏：**先取消收藏 → 再按清除**（用户 2026-10-04 选定，不做右键单条删除、不做确认框）。淘汰上限口径不变（`kMaxItems` 只数非收藏区），副作用是收藏数无上限、`history.json` 随之增长。单测 §9.4-15 |
| C13 | 启动是否置顶 | 设计方案 §8 早写 `Topmost(默认 true)`，代码里 `topmost_ = false`（步骤 8 遗留） | 用户要求"应用打开默认浮于各窗口最上层" | **按 §8 补齐实现**：`topmost_` 初值 true，`DockToWorkArea` 启动即 `HWND_TOPMOST`，★ 按钮仍可关。`Topmost` 落盘（记住用户关掉的偏好）等步骤 10 `SettingsService`，本轮固定"每次启动都开"。**v2.1.1 补一条系统事实**：本窗**不在前台**时 `SetWindowPos(HWND_TOPMOST)` 会被静默丢弃（返回 TRUE、`gle=0`、`WS_EX_TOPMOST` 不落，2026-10-05 实机坐实，重试与延时均无效），故置顶改为"请求 + 事后断言"两处落地——`Create()` 显示后再断言一次，`WM_ACTIVATE` 里若仍无带则 `PostMessage(WM_APP_RAISE_TOPMOST)` 延后补发（不能在该消息里直接改 z-order，见 `doc/PROJECT_STATE.md` §4 坑 #12） |
| C14 | 快速模式的选中位要不要跟着新内容走 | `FR-10` 只写了三个瞬间——"进入默认选中第一条""单击仅选中""空格粘贴后沉底并跳下一条未粘贴"，**没有规定列表期间新复制进来的条目是否移动选中位**；按字面实现就是新条目上屏后选中位仍停在原地，用户每次要先点一下才能贴 | 技术方案 §6 未涉及；旧 .NET 版同样只在进入时选第一条。用户 2026-10-05 提出"绑定后始终自动选中第一行，按空格直接复，不要每次先点再贴" | **Quick 模式下选中位钉在 `display_.front()`**：规则收口在 `Store::AnchorQuickSelection()` 一处（非 Quick 或显示区为空即空操作，已是首行则不发事件），由 `RebuildDisplay()` 末尾自动调用——入列、切换过滤、搜索、`Reset` 都经过它；`TogglePasteMode()` 与点选绑定回调 `AppContext::WirePicker()` 显式补调。粘完沉底后 `front()` 本就是下一条未粘贴，与 `FR-10` 原文不冲突。**粘贴去向不动**（同轮用户选定"仍粘回当前焦点窗口"）：绑定进程→该进程窗口、未绑定→呼出前窗口，`FR-11` 链与契约原文一律不改。普通模式永不自动挪选中位。UI 侧 `MainWindow::ScrollSelectionIntoView()` 在 Quick 下把钉住的行滚进视口，避免高亮在可视区外。单测 §9.4-16 |
| C15 | 连续填表要不要"绑进程 + 点条目" | 契约（FR-10/FR-11）只定义了"窗口内选中 → 粘回目标"这一条链，**没有"按住某个键点输入框就直接贴"的模式** | 技术方案未涉及。用户 2026-10-05 提出场景：Excel 复制十几个单元格 → 逐个填进另一个 Excel 或网页表单，"不想每次都先点框、再点条目、再按空格" | **新增「接力」**（v2.2.0 引入，**v2.3.0 按用户决议改为无开关**）：作用域＝**快速模式 + 主窗在屏**，主窗全程保持可见，**按住 `Alt` 用左键点目标输入框**＝把**屏幕上看见的第一行**贴进去，贴过靠 C8 沉底、C14 的规则让下一条自动上位；`` Alt+` `` 为兜底（贴到当前前台窗；**v2.3.3 起由 `Ctrl+Alt+空格` 改为此键**，理由见 §6.3）。六条取值由实现者代拍、已告知用户可翻：① 不引入新的队列指针，"取哪条"完全复用 `display_.front()`（逻辑层只剩 `RelayNext()` 一个纯函数，单测 §9.4-17；v2.2.0 的准入函数 `RelayArmable()` 已删）；② **每次取实时第一行**，中途新复制进来的内容插到最先＝先贴新的；③ 列表贴完**停住不循环**（`RelayNext()` 返回 `nullptr` 时只给一句状态栏提示、**不卸钩**；v2.2.0 的"自动解除并要求重新武装"已废）；④ **所见即所贴**：过滤态/搜索态就贴当前显示区第一行，不再要求停在【全部】视图或清空搜索框（v2.3.0 改；v2.2.0 那两条准入正是用户反馈"Alt+点没反应"的直接来源，已连同 `RelayArmable()` 一起删除）；⑤ **不做进程绑定**——刚点中的框所在窗本身就是前台窗，绑定反而破坏"前半程 Excel 后半程浏览器"的跨程序用法，也不与靶心绑定互扰；⑥ 修饰键取 `Alt` 不取 `Ctrl`：注入链本身就是 `Ctrl+V`，`Ctrl+左键` 在 Excel 是**加选多重区域**（粘贴直接报错）、在浏览器是**开新标签**，`Shift+左键` 在 Excel 是扩展选区，都排除；`Alt` 的唯一点是 `Alt+Ctrl+V`＝Excel 选择性粘贴，**v2.3.1 起由 `SendCtrlV(bool releaseCtrl)` 把 `Alt↑`（恒发）并进 `Ctrl+V` 的同一个 `SendInput` 批次化解**（原子投递，物理按键插不进中间），`Ctrl↑` 则在注入那一刻用 `GetAsyncKeyState` 复查；**必须异步态**——接力是在钩子 `PostMessage` 来的 `WM_APP` 里触发的、`Ctrl+V` 又是 60 ms 后由 `WM_TIMER` 注入的，这两种消息都不带可信的键盘状态快照，`GetKeyState` 读到的是上一条按键的遗留值。`Alt↑` 恒发而不按键态判断的理由见 §6.4（点击那一刻已单独抬过一次，注入时读到的是我们自己抬起的结果，判断会漏发）。v2.2.0/v2.3.0 那版"点击那一刻单独补发 `Alt↑`"在真人按住 `Alt` 的手势下无效，是用户报障「粘贴不到 Excel 中」的根因，详见 §5.5 与 §6.4。钩子**必须 `return 0` 放行**（与 `ProcessPicker` 的 `return 1` 吞点击**语义相反**，否则目标拿不到光标）。**契约原文未改**（改 `doc/SuperClip_设计规范.html` 需单独授权，见 §15）。v2.2.0 那版链路**已于 2026-10-05 19:44–20:02 实机走查**（七条逐条结果见 `doc/TESTING.md` §2），其粘贴链路与钩子机制在 v2.3.0 原样复用；**v2.3.0 的新作用域（可见性+模式驱动、所见即所贴、贴完不卸钩）已于同日 21:49–21:58 走查通过**，判据单在 `doc/PROJECT_STATE.md` §6 首行与 `doc/TESTING.md` §5；**但"内容真的落进目标单元格"这条至今没修好**：v2.3.1 只在**单元格编辑态**（焦点控件 `EXCEL6`）成立，**网格仅选中**（`EXCEL7`）贴不进；v2.3.2 再修一刀（批次顺序 + 扫描码）**两个假设双双证伪、连编辑态也失效**，同轮的对照测试证明**注入链在 WPS 网格上从来没成功过**（与 `Alt`、与接力都无关）。用户 2026-10-06 裁定**放弃修改、保持现状**，证据链与下一刀方向见 §6.4 与 `doc/PROJECT_STATE.md` §4 坑 #20 |

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

**AC-8 边界裁决（2026-10-05，v2.1.0）**：状态栏署名 `by Mr lin` 由"不可交互文字"改为"单击交给默认浏览器打开仓库页"。
实现只用 `ShellExecuteW(hwnd, L"open", kProjectUrl, ...)`（`shell32`，早已在链上），本进程内**不建立任何套接字、不加载任何网络栈、
不解析 HTTP**——联网动作发生在系统已选定的外部浏览器进程里，与用户自己双击一个 `.url` 快捷方式同构，本程序只是交出那个字符串。
因此 AC-8「无网络通信」对 SuperClip 本体仍然成立，结案口径不变：上面那份导入表 + 防火墙出站拦截后功能无变化。
代价与边界要如实记：① 点击后是否真的联网由浏览器和用户的网络状态决定，本程序无从知晓也无从限制；
② `kProjectUrl` 是编译期常量，写死在 `src/core/Config.h`，不发外部请求去取；③ 失败（返回 ≤32）只写 `error.log` 并在状态栏提示，不重试、不弹错误框。
若按最严解读"任何导致联网的行为都算违反 AC-8"，这条即不合规，需回退为不可点击的静态署名。

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
| FR-10 | 快速模式：进入默认选中第一条；单击仅选中；空格粘贴 → 灰显 + 沉底 + 跳下一条未粘贴。**C14 加固（2026-10-05）**：选中位**始终**钉在显示区第一行（切换模式、新内容入列、过滤/搜索、绑定完成后自动钉住），空格即粘最新一条，无需先点；粘贴去向仍按同轮决议"粘回当前焦点窗口"，契约原文未改 | `PasteMode::Quick` + `WM_KEYDOWN VK_SPACE` + `Store::AnchorQuickSelection()` |
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
| 快速 | 同上 | **选中位钉在显示区第一行**（C14：进入、新内容入列、过滤/搜索、点选绑定完成后都会重新钉住）；单击仅切换选中（高亮） | **空格**（焦点不在搜索框/下拉时） | 灰显 + **移到列表最末** + 选中位回第一行＝下一条未粘贴（支持连续空格连贴） |

AC 明确：单击在快速模式下**只选中不粘贴**，防误触。
C14（2026-10-05 用户决议）补充：钉住第一行后，"粘最新那条"不需要先点一下；**粘贴去向不变**（同轮选定"仍粘回当前焦点窗口"），
绑定进程→该进程窗口、未绑定→呼出前窗口的现有链一律不动，`FR-10` 契约原文未改。普通模式不钉。

### 5.2 标题栏图标（v2.1.0 起：左侧 1 枚应用图标 + 右侧 3 枚按钮）

| 位 | 功能 | C++ 动作 |
|---|---|---|
| ⓪ 最左 应用图标 | **纯装饰、不可点击**，与任务栏/托盘同源（`kIconIdApp` = 101） | `LoadImageW(IMAGE_ICON, px)` → `GetIconInfo`+`GetDIBits` 取预乘 BGRA → `ID2D1RenderTarget::CreateBitmap`，按 DPI 缓存、随 `rt_` 弃置（`EnsureTitleIcon()`）。左缘 `kPad`，边长 `kTitleIconDip = 18` DIP，与模式文字间隔 `kTitleIconGap = 6` |
| ① 最右起第 3 个 收起 | 最小化到托盘，继续监听 | `ShowWindow(SW_HIDE)`，三窗口均不销毁 |
| ② 中间 悬浮 | 切 `TopMost`，开启态图标高亮；**启动默认开启**（C13） | `SetWindowPos` 置顶/取消；`topmost_` 初值 true，`DockToWorkArea` 用 `HWND_TOPMOST` |
| ③ 最右 关闭 | **彻底退出** | `AppContext::Exit`：卸钩子 → 复位光标 → 注销热键 → `RemoveClipboardFormatListener` → `NIM_DELETE` → 销毁窗口 → 消息循环退出 |

**悬浮键的图形（v2.1.0 改形）**：原形态是一个居中"图钉"轮廓，实机反馈两条——和帮助窗「置顶」那一步描述的图形对不上、
且开启态不够显眼。现改为**斜图钉**：针头圆（偏左上）+ 45° 针身 + 底部短横杠，未置顶时灰描边、
置顶时填 accent 色并在下方加一条 accent 下划线（双通道表达状态，不只靠颜色）。帮助窗「置顶」页文案同步为
「点标题栏的图钉……图钉变蓝并带下划线＝已置顶」。**待实机复核**：150%/200% 下针头与下划线是否仍然清楚（沿用 §4 遗留项）。
（该页页码：v2.3.4 及以前是第 9 步，**v2.4.0 起是第 10 步**——接力页插在第 4 位，其后各页整体后移一位。）

**为什么不用 `CreateBitmapFromHICON`**：MS 的 `d2d1.h` 有这个方法，mingw 的头没有（vtable 里也不在其声明序中），
交叉构建直接报"无此成员"。走 GDI 取像素再 `CreateBitmap` 拷贝，只用已在链上的 `user32`/`gdi32`，
**不引入 `windowscodecs`**，导入表因此与 v2.0.3 逐字一致（见 §1.2）。GDI 若把第 4 字节当填充位清零，
回落用 AND 蒙版重建 alpha（边沿会硬切，但胜过整块黑底），代码在 `MainWindow.cpp` 的 `IconToPremultipliedBGRA`。

### 5.3 模式切换入口
标题栏文字「剪贴板 - 普通模式」/「剪贴板 - 快速模式」，**点击该文字切换**（非两个独立按钮）。
该文字区必须返回 `HTCLIENT`（否则点击变成拖拽，永远收不到 `WM_LBUTTONUP`）；标题栏拖拽区因此只剩"模式文字右侧 ~ 最左按钮"之间的空白（`kModeTextW = 136` 逻辑px 覆盖文字本身）。
v2.1.0 加了最左的应用图标后，模式文字的起点从 `kPad` 挪到 `kModeLeft = kPad + kTitleIconDip + kTitleIconGap`；
**命中区与绘制区必须用同一个 `kModeLeft`**，否则图标会吃掉本该切模式的点击、或点击落在文字上却判成拖拽区。

### 5.4 其他交互（继承技术方案 §5.8）
- 空格：窗口级预处理按键（快速模式且有选中项）；焦点在 `EDIT`/`COMBOBOX` 时放行原生输入。
- Esc：点选模式取消。
- 右键菜单：**三项 + 一条分隔线**。前两项为「粘贴模式：普通/快速（点此切到…）」、
  「复制模式：一般/表格（点此切到…）」，分隔线后是「使用帮助」。（v2.2.0 曾插入第三项「填表接力：关闭/已开启」，
  **v2.3.0 已删除**——接力改成无开关，见 §5.5。）
  置顶不走菜单（标题栏已有 ★），清除/复位不走菜单（带确认链的动作，误触代价与开关不对等）。
  菜单文字按当前状态动态生成并反向提示点击后果；取消（未选任何项）什么都不改。落码 `MainWindow::ShowMainMenu()`
  （`kIdMode=1 / kIdCopy=2 / kIdHelp=3`，`TPM_RETURNCMD`）。
  焦点在搜索框（真 `EDIT` 子窗）时**放行原生编辑菜单**，不弹本菜单；点选进行中（`picking()`）也不弹。
  唤出：鼠标右键（`WM_CONTEXTMENU` 带屏幕坐标）与键盘 Menu 键——`VK_APPS` 交 `DefWindowProcW` 合成 `lParam=-1,-1`，
  菜单锚到列表区左上。**`Shift+F10` 有意不实现**：本窗没有菜单栏，F10 单按进系统菜单模式的后果不可见即不可测，
  按"只把实机验过的行为上线"的口径不放行（2026-10-04 决议）。
- 无边框自绘标题栏拖拽（`WM_NCHITTEST` 返回 `HTCAPTION`），按钮区返回 `HTCLIENT` 以**排除拖拽**。
- ~~首次呼出自动绑定目标窗口（`_boundOnce` 语义保留）~~ → **已作废（2026-10-04 用户决议）**：只有点过靶心才算绑定。
  理由：老语义会让"第一次呼出时所在的窗口"变成永久粘贴目标，之后从别的应用呼出仍往那个窗口打键，
  用户看上去就是"粘贴失灵"。现在未点选时目标恒为"呼出前那个窗口"，切应用即跟随。

### 5.5 接力（C15，v2.2.0 引入 → v2.3.0 改为无开关）

**要解决的场景**：从 Excel 复制十几个单元格（→ 经 FR-05 拆成十几条独立条目），要逐个填进另一个 Excel
或网页表单。现有链是"点框 → `Ctrl+`` 呼出 → 点条目 → 空格"，一次填表要点四下。接力把它压成
**"按住 Alt 点一下框"**——用户的原话是"相当于把快速粘贴里的空格变成 ALT+鼠标左键，也不用剪贴板界面隐藏"。

| 环节 | 实现 |
|---|---|
| 作用域（**没有开关**） | `AppContext::SyncRelayHook()` 是唯一裁决口：`pasteMode()==Quick && window_->IsVisible() && !picker_.picking()` 成立就装钩子，否则卸。调用点是**每一个能改变这三项的入口**：`Initialize()`（落盘的快速模式）、`MainWindow::ToggleVisibility()` 收起分支、`ShowAndFocus()`、`HitZone::BtnMinimize`、`SavePasteMode()`（标题栏模式文字与右键菜单两条切换都经它）、`TogglePick()` 两条路径、`picker_.onPicked/onCanceled`、`OnSessionUnlock()`、`OnEndSession()` |
| 装钩子 | `RelayService::Arm(mainWnd, inst)`：`SetWindowsHookExW(WH_MOUSE_LL)`；**幂等重装**——已挂着就先 `UninstallHook()` 再重挂。原因：低层钩子被系统按 `LowLevelHooksTimeout` 静默摘掉后**没有任何查询 API**，症状只是"Alt+点不灵且不报错"，重挂是唯一自愈路径。重装不写日志，只有状态跃迁写 |
| 触发 | 钩子 `OnMouse`：没按 Alt 直接 `return 0` 放行 → 300 ms 内重复（Alt+双击）忽略 → `WindowFromPoint` → `GetAncestor(GA_ROOT)`（**不是 `GA_ROOTOWNER`**：Excel 模态对话框用 ROOTOWNER 会退到主框架窗，`Ctrl+V` 落到框外）→ 是自家窗则忽略 → `PostMessageW(WM_APP_RELAY_TRIGGER, root)`，**`return 0` 放行** |
| 取条与粘贴 | `AppContext::RelayStep(target)`：校验目标仍有效 → `Store::RelayNext()`＝**所见即所贴的 `display_.front()`**（过滤态/搜索态按屏幕上第一条算，v2.2.0 的「必须【全部】视图 + 搜索框为空」两条准入已随 `RelayArmable()` 一起删除）→ `MainWindow::PasteForRelay(item, target)` → `DoPaste(item, /*moveToEnd*/true, target)` |
| 贴完（第一行是灰条） | **不卸钩、不算失败**：只 `LogInfo` 一行 + 状态栏提示「没有未粘贴的条目，复制新的内容即可继续」。用户复制一条新的回来，`display_.front()` 立刻就是它（C14）。v2.2.0 在这里是 `EndRelay()` 自动解除，已被否掉 |
| 结束 | 只有三种：切到普通模式 / 收起主窗 / 会话与进程终结（锁屏 `WTS_SESSION_LOCK`、注销 `WM_ENDSESSION`、`Shutdown()` 的 `Disarm(L"退出清理")`）。**闲置 5 min 定时器（`kRelayIdleMs`/`ID_RELAY_IDLE`）与 `Touch()` 已删除**，呼出主窗也不再解除接力（它本来就是接力的前置条件）；`HideForRelay()` 与武装/解除的托盘气泡一并删除，接力全程**主窗保持可见** |
| 兜底键 | `` Alt+` ``（`kHotkeyIdRelay=2`，`MOD_ALT` + `VK_OEM_3`，先带 `kModNoRepeat` 注册、失败退回不带再试）→ `OnRelayHotkey(GetForegroundWindow())`，给没有侧键的用户与钩子被第三方拦下的场合。**v2.3.3 起由 `Ctrl+Alt+空格` 改为此组合**（用户决议：三键组合按着别扭；而单修饰键的 `Alt+空格` 是 Windows 全局「窗口系统菜单」键，`RegisterHotKey` 会把它从所有程序手里静默抢走，`Ctrl+空格` 又撞输入法中英切换——`` Alt+` `` 与呼出键同键位只差修饰键，零冲突，理由见 §6.3）。注册现状不变，但同样只在 `relay_.armed()` 时生效；**注册已实测成功，"真人按下能否触发"未实测** |

**为什么不并进 5.1 的模式表**：接力不是第三种"粘贴模式"——它不改 `PasteMode`、不改列表选中位语义，
只是一个**外部触发器 + 一条取数规则**。做成模式会让 C14 的锚定规则多一个分叉；而 v2.3.0 干脆把它做成
**快速模式自带的能力**，作用域就是"看得见列表的那段时间"。

**代价与边界**：钩子常驻时间从"武装到解除的一小段"变成"只要快速模式且列表开着就挂着"。这是用户决议
的直接后果，接受它的理由有两条——钩子回调只做取窗与投递（`LowLevelHooksTimeout` 安全），以及
`SyncRelayHook()` 的重装语义让"被别的程序摘掉/被系统摘掉"不再是静默失效。**实机走查已通过**（2026-10-05 21:49–21:58，
六条判据逐条时间戳见 `doc/TESTING.md` §2，判据单见 `doc/PROJECT_STATE.md` §6 首行与 `doc/TESTING.md` §5）：
启动即就绪、搜索态贴所见第一行、全灰显不卸钩、收起即卸·呼出即回、切模式即卸·即回、锁屏卸·解锁自动装回·兜底热键·
双击去重·退出清理，全程七次点击过命中守卫零误投。**没被证到的有两条**：① "钩子真的被系统摘掉后自愈"——无法主动制造
被摘态，只能靠 `Arm()` 幂等重装的设计推理；② **粘贴内容真的落进了目标单元格**——见下面这段订正。
**⚠ 2026-10-05 v2.3.1 订正（原文曾据日志宣称"真表格落点已由用户自用实证"，该结论不成立）**：
20:34（v2.2.0）与 22:12:54–22:15:55（v2.3.0）两批自用日志里的
`[relay] 贴第 1 条 → XLMAIN|工作簿1 * - WPS 表格` + `[paste] … 焦点控件=EXCEL7|工作簿1`、`事件数=4`，
**只能证明"取的是第 1 行、按键批次投给了那个焦点控件"，不能证明内容落进了单元格**。事实相反：用户在 22:5x 报障
「`Alt+左键` 粘贴不到 Excel 中」「没反应」「能贴出东西，但每次都是同一条」——接力的 `Ctrl+V` 一条都没落地，
他手动 `Ctrl+V` 贴出的是剪贴板里**最后一次成功写入**的残留（`history.json` 读数：条目 137→138、`IsPasted` 计数
13→37、`display_.front()` 已是新条目 → 采集、标记、沉底都在工作，坏的只有注入那一步）。根因与修法见 §6.4
「v2.2.0 为接力补的两处」第 1 条的 v2.3.1 订正段。仍然成立的实证只有：贴完不卸钩（22:12:45→22:12:51 连续 10 条
「第一行已是灰条…本次不动作」期间无一条 `卸下`，22:12:54 新复制一条后同一个钩子立刻动作）、点选起止的卸下/装回
（22:13:57.356「点选开始」同毫秒卸下 → 22:13:58.982「点选命中，绑定 `ET`」→ 22:13:59.007 就绪）。
同一段日志还记录了否掉 v2.2.0 模型的直接经过：20:31:59 武装 → 20:32:03「接力解除：列表已贴完
（第一行是灰条），接力自动结束」，20:34:29 又一次「接力解除：呼出主窗，接力已关闭」——**贴完即解除**与
**呼出即解除**正是他当时被绊到的两处。
**教训（已进 `doc/PROJECT_STATE.md` §4）：涉及修饰键的注入功能，日志全绿与脚本走查全绿都不构成"贴进去了"的证据。**
唯一可接受的判据是**目标程序里的内容真的变了**，外加日志出现 `同批抬键=Alt`、`事件数=5`。

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
RegisterHotKey(mainHwnd, kHotkeyId=1, MOD_CONTROL, VK_OEM_3 /* 0xC0 */)
WM_HOTKEY(0x0312) → ToggleVisibility()：可见则 Hide，否则记录呼出前窗口 + Show + Activate + 列表获焦
                                                              （v2.3.0：这一步同时改变接力的作用域——收起即卸钩、呼出即装回，见 §5.5）
RegisterHotKey(mainHwnd, kHotkeyIdRelay=2, MOD_ALT|kModNoRepeat, VK_OEM_3)   // 兜底键（v2.3.3 起；v2.2.0–v2.3.2 是 MOD_CONTROL|MOD_ALT + VK_SPACE）
WM_HOTKEY → OnRelayHotkey(GetForegroundWindow())：贴当前前台窗
退出时 UnregisterHotKey（两把各自独立注销，没注册上的那把不参与）
```
- **`kModNoRepeat(0x4000)` 是 Win7 不认识的标志**：先带它注册，失败**退回不带它的重试**，两把都失败才
  `LogWarn` 并放行（不弹错误框）。所以"接力热键到底注册上没有"在界面上**不可见**，走查时得读日志。
- **兜底键为什么是 `` Alt+` `` 而不是 `Alt+空格`**（v2.3.3，用户最初要的是后者）：`RegisterHotKey(MOD_ALT, VK_SPACE)`
  能注册成功（Windows 只保护 `Win+L` 与 `Ctrl+Alt+Del`），但它会把 `Alt+空格`＝「窗口系统菜单」**从所有程序手里
  静默抢走**，而 SuperClip 不给任何提示；`Ctrl+空格` 撞输入法中英切换。`` Alt+` `` 与呼出键同键位、只差修饰键。
  与 `Shift+F10` 当初"后果不可见即不可测所以不做"是同一口径。
- 兜底键占用风险：`` Alt+` `` 与输入法/其它软件抢键的情况**未实测**（见 §15 N12）；"真人按下能否触发"同样**未实测**。


### 6.4 粘贴机制（FR-11）与双层防护
```
DoPaste(item, moveToEnd, targetOverride = nullptr):      // v2.2.0 加第三参：接力传钩子点中的窗，普通链为 nullptr
  _internalPaste    = true                 // 第一层：标志位
  _lastPastedContent = item.content        // 第二层：内容比对兜底
  PasteService::PasteText(item.content, targetOverride ? targetOverride : GetPasteTarget())
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
   **v2.3.1 起，需要抬起的修饰键并进同一个 `SendInput` 批次**；**v2.3.2 起批次内顺序是 `Ctrl↓ → Alt↑ → V↓ → V↑ → Ctrl↑`（5 或 6 事件），且每个事件都补了 `wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)`**——这是对技术方案 §5.2「只送虚拟键码、不送 scancode」的一次**有意偏离**，已在 `Keyboard.h` 注释里就地登记。
   **⚠ 这两处改动在真人手验里都没能修好目标缺陷，而且 v2.3.2 比 v2.3.1 退了一步**（连唯一能成功的"单元格编辑态"也失效）；用户 2026-10-06 裁定**保持现状、不回退**。事实与下一刀方向见下面这段与 `doc/PROJECT_STATE.md` §4 坑 #20。

全程 `try/catch` + 句柄守卫，失败静默，绝不影响程序运行。

**v2.2.0 为接力补的两处（普通链同样经过它们）**：
1. 注入前除了"用户按着 Ctrl 就补发 Ctrl up"（原有防御），**再判一次 `IsAltDownAsync()`，按住 Alt 就补发 Alt up**
   —— 否则 Excel 收到的是 `Alt+Ctrl+V`＝选择性粘贴对话框。Alt 必须用 `GetAsyncKeyState`（异步态），
   因为接力是在钩子 `PostMessage` 来的 `WM_APP` 里注入的，`GetKeyState` 读的是上一条按键的遗留状态、不可信。
   **⚠ v2.3.1 订正：这一条按原写法（点击那一刻单独发一批 `Alt↑`）在真人手势下无效。** 用户报障
   「`Alt+左键` 粘贴不到 Excel 中／没反应／能贴出东西但每次都是同一条」，根因是 `Ctrl+V` 由 **60 ms 后的
   `ID_FOCUS_WAIT` 定时器**注入，而 `Alt` 是点击那一刻抬的——C15 的手势是"**按住** `Alt` 点"，60 ms 后手指还压着，
   目标实际收到 `Alt+Ctrl+V`（选择性粘贴），于是**日志全绿、表格毫无变化**（旧 `SendCtrlV()` 只有 4 个事件，
   日志 `事件数=4` 即自证批次里没有 `Alt↑`）。修法：`SendCtrlV(bool releaseCtrl)` 把 `Alt↑`（**恒发**）与按需的 `Ctrl↑`
   **并进同一个 `SendInput` 批次**（原子投递，物理按键插不进中间），并由新增的 `PasteService::InjectCtrlV()` 在
   **注入那一刻**用新增的 `IsCtrlDownAsync()` 复查 `Ctrl`（`WM_TIMER` 不带键盘状态快照，`GetKeyState` 在这里同样不可信）。
   **`Alt↑` 为什么不按键态判断**：点击那一刻已经单独抬过一次，注入时再读 `GetAsyncKeyState(VK_MENU)` 很可能读到的是
   **我们自己抬起的结果**（用户手指其实还压着，且按住不放的 typematic 重发会把键态重新压下去）→ 判断会漏发、批次退回 4 事件＝等于没修。
   重复 `keyup` 幂等，孤立的 `Alt↑`（前面没有 `Alt↓`）不触发 Excel 的 keytip 态——**这三条副作用均未实测**。点击那一刻的两次单独抬键保留，只为夺前台那段路干净。
   **⚠ v2.3.2 再订正（v2.3.1 的修复只解决了一半，且上面那三条副作用里的"孤立 `Alt↑` 不触发 keytip"被现场证伪成嫌疑）**：
   真人手验给出的分界是**焦点控件类型**——`EXCEL6|`（单元格编辑框）能贴，`EXCEL7|工作簿1`（WPS 网格＝单元格仅被选中）贴不进。
   据此同时修两个假设：H1 认为我们送出的**干净 `Alt` 点按**让表格进了 keytip 菜单态、随后的 `Ctrl+V` 被当菜单加速键吞掉
   （v2.3.1 的批次里 `Alt↑` 正排在 `Ctrl↓` 前面，等于先干净放开 Alt）；H2 认为 `wScan=0` 不被网格的 accelerator 处理接受。
   改法：批次顺序改成 `Ctrl↓ → Alt↑`（`Ctrl` 已按下时抬 `Alt` 是和弦、不构成独立点按）、每事件补扫描码、
   **删掉点击那一刻的单独 `Alt↑`**（`PasteService.cpp` 里只留"用户按着 Ctrl 就补发 Ctrl up"）。
   **结果：H1、H2 双双证伪，而且退了一步**——八次触发日志全是 `同批抬键=Ctrl↓→Alt↑(含扫描码)`、`事件数=5`，
   `EXCEL7` 与 `EXCEL6` 两种焦点态都出现过，用户回「还是没有进去」。
   **同轮那个不涉及 `Alt` 的对照测试才是本轮最大的收获**：在 WPS 里单击选中空单元格（不进编辑态）→ SuperClip 快速模式选中一条
   → 按**空格**做普通粘贴，用户答「**没成功过**」。即 **C++ 版的注入链在 `EXCEL7` 上从来就没成功过**，与接力、与 `Alt`、
   与这两处改动都无关。两边权威实现只剩**打包方式**一个差别：.NET 旧版是 `keybd_event` **四次独立调用**，
   C++ 版从第一版起就是**一次 `SendInput` 投整批**。机理猜测（**未验证**）：整批投递时系统在目标取到 `WM_KEYDOWN(V)` 之前
   就把整批异步键态更新完，网格若自查实时键态就当成一次裸 `V`；编辑框走标准键盘翻译、不查键态，所以 v2.3.1 在编辑态能进。
   **下一刀方向**：拆成多次 `SendInput` 并在事件之间加短延时（`Ctrl↓`+`Alt↑` 仍须同批以保原子性，`V↓` 单独一批、
   按住 20–30 ms 再抬），同时**回退扫描码**。**用户 2026-10-06 裁定放弃修改、保持现状，代码原样保留未回退。**
2. **目标已在前台就不再夺前台**：判据 `GetForegroundWindow() != target_` 才走三段式。接力的目标窗是用户
   刚点中的，本来就是前台窗，再抢一次焦点会打断目标自己的焦点切换。

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

**两把 `WH_MOUSE_LL` 钩子的对照**（v2.2.0 起 `ProcessPicker` 与 `RelayService` 同时在仓库里，**二者语义相反，不可互相"借鉴"返回值**）

| | 点选（靶心绑定） | 接力（填表） |
|---|---|---|
| 目的 | 用户点哪个窗就绑哪个窗 | 用户点哪个框就往哪贴 |
| 钩子返回值 | **`return 1` 吞掉点击**（绝不能改动用户的选区/焦点） | **`return 0` 放行**（目标必须拿到光标，否则粘贴没有落点） |
| 需要修饰键 | 否 | 是（`Alt` 按下才响应） |
| 回调里做的事 | 取窗 → 判定 → 卸钩 → `PostMessage` | 取窗 → 判定 → `PostMessage`（**不**卸钩，一次贴完接着贴下一条） |
| 超时约束 | 同：必须在系统 `LowLevelHooksTimeout` 内返回，超时**不是变慢而是系统摘钩并吞掉这次鼠标输入** | 同 |
| 生命周期 | 一次点选即卸 | **无开关**：快速模式 + 主窗在屏期间常驻（`SyncRelayHook()` 幂等重装以自愈）；切普通模式 / 收起主窗 / 点选开始 / 锁屏 / 注销 / 退出 卸下。**已删**闲置 5 min 定时器与"呼出主窗即解除" |

三条实现约束两把钩子共享（本仓库已在点选上实机跑通）：钩子回调**没有用户数据参数** → 进程内静态 `instance_` 转发；
回调运行在**装载钩子的 UI 线程** → 状态可无锁读写；回调里**只做取窗口 + `PostMessage`**，绝不在里面打键、读写剪贴板或弹窗。


### 6.6 系统托盘（纯 Win32）
- `Shell_NotifyIconW`：`NIM_ADD/MODIFY/DELETE`，`NOTIFYICONDATA` 带 `uCallbackMessage = WM_APP+1`。
- 图标：`LoadImageW(exe, MAKEINTRESOURCEW(kIconIdApp=101), IMAGE_ICON, SM_CXSMICON…)`，失败回退 `LoadIcon(nullptr, IDI_APPLICATION)` 并记 `LogWarn`。
- `WM_LBUTTONDBLCLK` → 打开主窗口；`WM_RBUTTONUP` → `SetForegroundWindow(自身)` 后 `CreatePopupMenu`+`AppendMenuW`+`TrackPopupMenuEx(TPM_RETURNCMD)`（前置置顶是标准范式，保证点击菜单外可消失）→「打开 / 退出」。
- **Explorer 重启自愈**：`RegisterWindowMessageW(L"TaskbarCreated")`，收到即重新 `NIM_ADD`。
- **气泡通知 `ShowBalloon(title, text)`（v2.2.0 为接力新增，**v2.3.0 起暂无调用方**）**：复制 `nid_` 后 `uFlags = NIF_INFO`、
  `szInfoTitle`/`szInfo`、`dwInfoFlags = NIIF_INFO` → `NIM_MODIFY`。当初为什么必须走气泡而不是状态栏：接力武装时
  **主窗是隐藏的**，状态栏没人看得见——v2.3.0 接力不再藏窗，这个理由随之消失，方法**保留**给 T5「热键被占用要可见提示」
  （契约决议表里那条至今没实现，见 §14）。**已实测**（2026-10-05 19:47，`cpp/build-mingw/relay-balloon.png`）：Win11 把
  `NIF_INFO` 转成 toast，正文逐字正确；**标题位取的是 exe 资源里的 `FileDescription`，那份串当时是乱码**（构建配置缺陷，
  根因与三档修法见 `doc/PROJECT_STATE.md` §4 坑 #15 —— **v2.3.4 已按 A 档修掉**：`app.rc` 首行加 `#pragma code_page(65001)`，
  产物 `FileDescription` 按码点读回＝`超级剪贴板`；那张截图是修复前拍的，仍留作缺陷存证，**toast 标题的实机观感未重拍**）。
  通知设置能否把它静默掉，只在**本机**验过一档。
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
| 键盘模拟 | `SendInput`（`VK_CONTROL` / `VK_V` / `KEYEVENTF_KEYUP`）；异步态判定 `GetAsyncKeyState(VK_MENU)`（接力注入前抬 Alt，见 §6.4） |
| 全局热键 | `RegisterHotKey` / `UnregisterHotKey` |
| 托盘 | `Shell_NotifyIconW` `RegisterWindowMessageW("TaskbarCreated")` `LoadImageW`；v2.2.0 气泡 `NIM_MODIFY + NIF_INFO`（`szInfoTitle`/`szInfo`/`NIIF_INFO`，仍在 `shell32`） |
| 标题栏图标像素 | `LoadImageW(IMAGE_ICON, LR_SHARED)` `GetIconInfo` `GetObjectW` `GetDIBits` `DeleteObject` →（D2D）`ID2D1RenderTarget::CreateBitmap`（v2.1.0；全部落在已链接的 user32/gdi32/d2d1 内，不引 `windowscodecs`） |
| 署名单击开仓库页 | `ShellExecuteW(..., L"open", kProjectUrl, ...)`（v2.1.0；边界裁决见 §1.2，本进程不联网） |
| 搜索框内嵌清除叉号 | `EM_SETMARGINS(EC_RIGHTMARGIN)`（有字时给右端留出叉号位）`SetWindowTextW(L"")`（点叉号清空）——都在 `EDIT` 的子类过程 `EditProc` 内 |
| 弹出菜单 | `CreatePopupMenu` `AppendMenuW` `TrackPopupMenuEx` `DestroyMenu` `GetCursorPos` |
| 低层鼠标钩子 | `SetWindowsHookExW(WH_MOUSE_LL)` `UnhookWindowsHookEx` `CallNextHookEx` `WindowFromPoint`（点选与接力共用同一套钩子约束，见 §6.5 对照表；接力另用 `GetAncestor(GA_ROOT)`，已列在"前台控制"行） |
| 光标 | `LoadCursorW` `SetSystemCursor` `SystemParametersInfoW(SPI_SETCURSORS / SPI_GETWHEELSCROLLLINES)` |
| 置顶/窗口 | `SetWindowPos` `GetSystemMetrics` `MonitorFromWindow`/`GetMonitorInfo`（多显示器工作区） |
| 哈希/唯一 ID | `BCryptOpenAlgorithmProvider/Hash`、`CoCreateGuid` |
| 原子写 | `ReplaceFileW` / `MoveFileExW` |
| DPI | `SetProcessDpiAwarenessContext(PER_MONITOR_V2)`，Win7/8 回退 `SetProcessDPIAware` |

---

## 7. UI 规范与自绘实现

```
┌──────────────────────────────────────────────────┐
│  [图标]  剪贴板 - 普通模式       [收起] [悬浮] [关闭] │ ← 图标纯装饰；模式文字可点击切换
├──────────────────────────────────────────────────┤
│  [ 🔍 搜索框............              ✕ ]         │ ← EDIT 子控件（IME 完好）；✕ 仅框内有字时出现
│  [ 全部▾ ] [ 清除 ] [ 复位 ] [ 🎯绑定 ]            │ ← 工具栏
├──────────────────────────────────────────────────┤
│  1  文本内容预览…                   12:30:00  ☆   │ ← 星标独占最右一列、垂直居中
│  2  400.00                          12:29:00  ☆   │ ← 表格单元格：来源标注不上屏（仅参与搜索）
│  3  普通文本…                       12:28:00  ☆   │
│  …（已粘贴项灰显 + 半透明；收藏项只在【收藏】视图出现）│
├──────────────────────────────────────────────────┤
│  已绑定：EXCEL     v2.1.0  by Mr lin（可点击）    │ ← 状态栏 22px：版本号在署名之前
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

**悬浮全文气泡**：仅对"被截断"的行生效。光标停在行上 400ms 后浮现自绘气泡（`HoverTip`，白底 + `#C9CFD8` 边框 + 正文 `#272C36`），显示条目**完整内容**（保留换行，最长 2000 字，超出补 `…`；宽 ≤320 / 高 ≤240 逻辑px，超框按放得下的最长前缀裁剪）。`WS_EX_TOPMOST|TOOLWINDOW|NOACTIVATE|TRANSPARENT`：不抢焦点、点击穿透。滚轮、数据变化、鼠标离开、点击、窗口收起即刻收起；未截断的行不弹。
**位置（v2.1.0 改）**：主位改到该条**上方**，水平从行左缘**右移 3 个全角字宽**（`kTipOffsetChars = 3`，一个字宽用 `GetTextExtentPoint32W(L"中")` 现场量，随 DPI 走，不写死像素）——原先贴在行下沿会盖住下面几条，而正文向右缩进 3 字正好和列表的文字起点对齐。第一条上方放不下时才回落到行下方，回落位置同样钳进工作区。横向仍 `min(行右界, 工作区右缘-宽)` 后再 `max(左缘)`，越界即钳。

**占位提示与清除叉号**：搜索框为空时在 `EDIT` 上方叠加自绘文字「搜索…」，非空时失效隐藏（对应原 `EmptyToVisibleConverter`）。
**v2.1.0 起**：框内有字时右端再画一个 ✕（边长 `kSearchClearDip = 16` DIP、距右内缘 `kSearchClearInset = 6` DIP，2px 笔、`#6B7485`）。
它必须画在 `EditProc` 的 `WM_PAINT` 里而不是主窗：`EDIT` 是真子窗，永远盖在父窗之上，父窗画的叉号会被文字盖掉。
同时用 `EM_SETMARGINS(EC_RIGHTMARGIN)` 给文本留出叉号位，避免长串末尾被叉压住；悬停时 `WM_SETCURSOR` 换手型，
`WM_LBUTTONUP` 落在叉区内直接 `SetWindowTextW(hwnd, L"")` 清空（清空走的是原生编辑路径，后面接的 `EN_CHANGE` 自然触发既有 300ms 搜索防抖，不另开一条清除链）。

**帮助窗**：`HelpWindow` 无边框模态，10 步静态引导（呼出热键、自动记录、双模式、**填表接力**、收藏、绑定、复制模式、搜索、清除/复位、置顶；接力页为 v2.4.0 新增，插在双模式之后，故第 4 步及以后的页码整体后移一位）。
实现口径（2026-10-04 步骤 11 实机确认）：`WS_POPUP` + `WS_EX_TOOLWINDOW|TOPMOST`、主窗的 owned window，
420×300 逻辑px，**贴主窗左侧**（左侧放不下才回落右侧/居中，最后 `FitRectToDesktop` 钳回工作区）；
模态靠 `EnableWindow(主窗, FALSE)` 实现，**不起嵌套消息循环**（全程序只有一个泵，见技术方案 §6.1）；
翻页 `上一步/下一步` 在首末位钳住并禁用按钮，`N / 9` 计数画在标题带右侧 44 逻辑px 位内，重新打开回到 `1 / 9`；
`Esc` 与「关闭」都还原主窗可用并把焦点交回。实机已验：鼠标点击与 `VK_RIGHT` 翻页、钳位、模态、还原；
**未验**：150%/200% DPI 下的排版（沿用步骤 6 的遗留项）。

**状态栏右侧「vX.Y.Z  by Mr lin」**：署名区在 v2.1.0 起可点击（`HitZone::BtnSignature` → `OpenProjectPage()`，见 §1.2 的 AC-8 边界裁决）。
可点矩形按 `IDWriteFactory::CreateTextLayout` 实度量出的文字宽度算，右边贴 `ClientW()-kPad`，**不按字数估**，否则手型光标和命中区会错位。
> 同一处还修掉一个**命中穿透缺陷**（用户报"点 Mr lin 结果改了下面条目的收藏"）：`HitTest` 早先只看 `contentY`，
> 列表视口虽然只画到 `ClientH()-kStatusF`，但落在底栏的点击仍被算进最下面那一条 → 误切收藏。
> 现在底栏带先行判定：`y > ClientH()-kStatusF` 时只可能返回 `BtnSignature` 或 `Status`，`Status` 在 `WM_LBUTTONUP` 里显式 `break` 什么都不做。
> 这是独立于"署名要不要可点"的缺陷，即便署名保持静态也必须修。

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
│  │  ├─ services/ ClipboardMonitor · PasteService · StorageService · TrayService · ProcessPicker · RelayService（v2.2.0）
│  │  ├─ native/ AppDirs · Clipboard · HiddenWindow · SystemInfo（dwmapi/shcore 走 LoadLibrary）
│  │  │        ComPtr.h · Foreground.h · Keyboard.h · Uuid.h · WinUtil.h（RAII 与纯内联工具）
│  │  ├─ ui/ MainWindow · ListRenderer · Theme · HoverTip · HelpWindow
│  │  ├─ res/ app.rc（VERSIONINFO + 101 ICON）· app.manifest（PerMonitorV2 + comctl6）· SuperClip.ico
│  │  └─ util/ Log.h/.cpp
│  ├─ tests/test_main.cpp             # 42 例，自带极简断言器（无 Catch2）；用例数由 Run() 自增统计，不手抄
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

`app.rc` 的**第一条指令必须是 `#pragma code_page(65001)`**（v2.3.4 起，坑 #15）：该文件是无 BOM 的 UTF-8，
windres 与 MSVC `rc.exe` 在没有这条声明时按**构建机的 ANSI 代码页**逐字节读入、再把每个字节宽化成一个 UTF-16 码元。
中文 Windows（CP936）恰好能把 `超级剪贴板` 的 15 字节配回 5 个汉字，所以缺陷在开发机上看不见；本项目交叉构建走
WSL（locale C/POSIX → CP1252），资源里存的就是 `è¶…çº§å‰ªè´´æ…`，属性面板/任务管理器的「文件说明」与 Win10/11
toast 的标题位（都取 `FileDescription`）随之显示乱码。两个工具链都认这条 pragma，故一处声明两边同时生效
——这也是当初三档修法里选 A 档的原因（B 档 `-J utf-8` 只救 mingw，C 档改纯 ASCII 等于放弃中文描述）。
校验口径：**只认从产物里按码点读回的 `VersionInfo`**，不认控制台文本（cp936 管道会把结论骗反）。

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
| §9.4 `Store` 状态机 | **17** | 去重与收藏态迁移、插入位＝收藏数量、末位淘汰（C7）、收藏永不淘汰、稳定分区保序、表格整块不倒序、快速沉底（非收藏／收藏区末位 C8）、存盘重载顺序一致、`Reset` 清灰显＋时间降序（C5）、全角/大小写折叠搜索、过滤+搜索+选中保持、收藏仅在【收藏】视图（C10）、标注不上屏仍参与搜索（C11）、清除保留收藏（C12）、快速模式选中位钉第一行（C14）、**接力取第一行与贴完即止（C15）** |
| §9.6 `Settings` 往返 | **4** | .NET 文件读入并逐字节写回、全字段往返（含绑定进程名）、缺失/损坏回落默认、越界与类型不符按字段作废 |
| **合计** | **42 例 / 228 断言 / 0 失败**（2026-10-05 21:08 交叉构建产物在 Windows 实跑，21:26 复跑结果一致） | —— |

**UI 层不做条件编译式打桩**：需要真实 Windows 桌面实机验证（无法在本环境完成的部分，交付时逐项标注"未验证"）。

---

## 13. 验收标准映射

| # | 标准 | C++ 验证方式 |
|---|---|---|
| AC-1 | Excel 复制后自动拆单元格并标注行列 | 单测（TSV 输入）+ 实机 Excel 复现 |
| AC-2 | 双击/空格粘贴到目标光标处；快速粘贴沉底变灰；**快速模式选中位钉在第一行**（C14，不点条目直接空格即贴最新一条） | 实机：记事本、Excel、浏览器三类目标 |
| AC-3 | 重启后历史/收藏/灰显/**排序**全保留；按「清除」后**收藏条目仍在**（C12） | 实机 + `history.json` 与 .NET 版互读 |
| AC-4 | 悬浮置顶开/关，且**启动默认开**（C13） | 实机（判据只认 `GWL_EXSTYLE` 的 `WS_EX_TOPMOST` 位，不认 API 返回值；图钉键的置顶态下划线见 `doc/images/readme-title.png` 两态）。v2.1.1：冷启动连续两次位为 1。v2.1.2（17:41）：**兜底分支也已实测**——后台启动丢带（位 0）时，外部跨进程调用返回 TRUE 仍不落，本窗被激活后 800 ms 内由 `WM_APP_RAISE_TOPMOST` 补发到位 |
| AC-5 | 每行显示当前顺序序号 | 实机（过滤/搜索后序号随显示位置变化） |
| AC-6 | 普通双击、快速空格均命中光标处 | 实机 |
| AC-7 | 任意 Win x64 免运行时双击即用、常驻托盘 | 干净虚拟机（未装 .NET/VC 运行库）测试 |
| AC-8 | 全程零网络 | 链接期无网络库符号 + 防火墙出站拦截后功能无变化 |
| AC-9 | **（v2.2.0 新增、v2.3.0 改为无开关、v2.3.1 修注入批次，非规范原文条款）接力**：快速模式且主窗在屏时，按住 Alt 点输入框＝把**屏幕上看见的第一行**贴进去并沉底、下一条自动上位；贴完只提示、不卸钩；切普通模式/收起主窗/锁屏/注销/退出即卸下 | 逻辑层**已通过**（§9.4-17，42 例/228 断言/0 失败，2026-10-05 21:08 Windows 实跑；v2.3.1 重跑仍 42/228/0）。v2.2.0 版的粘贴链路与钩子机制**已实机走查**（19:44–20:02 七条）；**v2.3.0 的新作用域也已于 2026-10-05 21:49–21:58 走查通过**（六条判据逐条时间戳见 `doc/TESTING.md` §2，判据单见 §5），并在**同日 22:12:30–22:15:55 由用户在真 WPS 表格上自用补上两块实证**：连续 10 条"第一行已是灰条…本次不动作"期间无一条 `卸下`、新条目上位后同一钩子立刻动作；点选开始即卸下（22:13:57.356）→ 点选命中绑定 `ET` → 就绪（22:13:59.007）→ 继续动作，把走查脚本拍不到的"点选起止"补齐。**⚠ 但"内容真的落进单元格"这条被证伪并已修（v2.3.1）**：用户报障「`Alt+左键` 粘贴不到 Excel 中／没反应／能贴出东西但每次都是同一条」，根因是抬 `Alt` 发生在点击那一刻、`Ctrl+V` 由 60 ms 后的定时器注入且旧批次只有 4 个事件（不含 `Alt↑`）→ 目标实收 `Alt+Ctrl+V`（选择性粘贴），**日志全绿而表格无变化**；20:34 与 22:12–22:15 两批"真表格落点"日志因此**不构成落点实证**（详见 §5.5 订正段）。v2.3.1 把 `Alt↑`（恒发）与按需的 `Ctrl↑` 并进 `Ctrl+V` 的同一个 `SendInput` 批次，**真人手验结果是"半通过"**：单元格**编辑态**（焦点控件 `EXCEL6`）能贴、**网格仅选中**（`EXCEL7`）贴不进（判据 ②「目标单元格内容真的变」只在编辑态成立，日志 `同批抬键=Alt`、`事件数=5` 都拿到了）。v2.3.2 再修一刀（批次顺序改 `Ctrl↓→Alt↑` + 每事件补扫描码 + 删掉点击那一刻的单独 `Alt↑`）**两个假设双双证伪、连编辑态也失效**；同轮那个不涉及 `Alt` 的对照测试（普通空格粘贴在 WPS 网格里「没成功过」）证明**注入链在 `EXCEL7` 上从来没成功过**，与接力无关。用户 2026-10-06 裁定**放弃修改、保持现状（未回退）**，AC-9 的"贴进真表格单元格"这一条**记为未通过**，证据链与下一刀方向见 §6.4 与 `doc/PROJECT_STATE.md` §4 坑 #20。**v2.3.3 另把兜底键改成 `` Alt+` ``**（只改键位、产品逻辑未动；注册已实测成功，真人按下能否触发未实测，见 N12）。仍挂未验证：注销 `WM_ENDSESSION`、`paste_.busy()` 叠贴丢弃、钩子被系统摘除后的自愈、恒发 `Alt↑` 的三条副作用（重复 keyup / Excel keytip 闪烁 / 抬掉目标程序自己按住 Alt 的用途）。规范原文（`doc/SuperClip_设计规范.html`）**未写入本条**，改动契约原文需单独授权 |

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
| 搜索框占位提示 | **`EditProc` 的 `WM_PAINT` 自绘**（原记 `EM_SETCUEBANNER` 与落码不符，2026-10-05 订正） | 实测未用系统 cue banner：自绘才能控制颜色/字号与 `EDIT` 底色一致，且 v2.1.0 的清除叉号本来就必须画在同一条 `WM_PAINT` 路径里 |
| 图标绘制 | D2D 几何路径（窗口按钮/靶心），★☆ 用字形 | Win7 无 `Segoe MDL2 Assets`，跨版本字形宽度不稳 |
| 标题栏应用图标 | `LoadImageW` → GDI 取像素 → `ID2D1RenderTarget::CreateBitmap` | mingw 头无 `CreateBitmapFromHICON`；走 GDI 不引 `windowscodecs`，导入表保持与 v2.0.3 逐字一致（§5.2、§1.2） |
| 置顶键形态 | 斜图钉（针头圆 + 45° 针身 + 底横杠），开启态填 accent 色并加下划线 | 用户 2026-10-05 指定：原形态与帮助窗描述不符且开启态不明显；状态用"填色 + 下划线"双通道表达，不单靠颜色 |
| 搜索框清除方式 | 框内有字时右端 ✕，点击即清空（`SetWindowTextW(L"")`） | 清空后由原生 `EN_CHANGE` 接既有 300ms 防抖，不另开第二条清除链；留白用 `EM_SETMARGINS` 解决，避免压字 |
| 悬浮气泡位置 | 行的**上方**，水平右移 3 个全角字宽（字宽现场量） | 用户 2026-10-05 指定：原贴行下沿会盖住下面几条；3 字宽和列表正文起点对齐 |
| 署名单击 | `ShellExecuteW("open", kProjectUrl)` 交给默认浏览器 | 用户 2026-10-05 指定；AC-8 边界裁决见 §1.2 —— 本进程零网络代码，联网发生在外部浏览器 |
| 底栏命中判定 | 状态栏带**优先**判定，带内只返回 `BtnSignature`/`Status`，`Status` 不改任何状态 | 缺陷修复：`contentY` 会把底栏点击算进最下面一条，误切收藏（用户报"点 Mr lin 改了下面条目收藏"） |
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
| N8 | `doc/技术方案.md` §5.7/§6.1 仍留有 C7 同源矛盾表述（"非收藏区最前 = 最旧"、"超限从非收藏区最前淘汰"） | 若有人按该文档字面实现会误删最新条目 | `设计规范` v1.1 已勘误；本文按末位淘汰。**`doc/技术方案.md` 尚未同步**，待授权后一并改正 |
| N9 | 步骤 8 实机观察：目标为 WPF 应用（PowerShell ISE）时，"粘贴后重新唤起主窗再按空格"有 2/9 次**完全未进入粘贴分支**（`error.log` 无该次记录），重试即成功；自建 Win32 宿主未复现 | 快速连贴在个别第三方应用上可能"按一下没反应" | 疑与再夺前台瞬间按键归属有关，**未定论**；步骤 12 前用多目标多样本复测，若确证则在 `ID_FOCUS_WAIT` 后校验前台归属并重发一次 |

| N10 | **接力的 LL 钩子可能被第三方拦截**：杀软/远控软件同样装 LL 钩子；且 `LowLevelHooksTimeout` 一到系统就**摘钩并吞掉那一次鼠标输入**（表现为"按 Alt 点了没反应，而且那一下点击还丢了"） | 接力静默失效，用户以为功能没做 | 回调只做"取窗口 + `PostMessage`"把超时风险压到最低；兜底键 `` Alt+` ``（v2.3.3 起）不依赖钩子；**未实机验证** |
| N11 | **表格拆分行优先且跳过空行与空单元格**（`TableParser.cpp:23,30`），带空格子的复制**列表里少一格**，按序逐格贴会与表格格位错开 | 填表时"贴错列"，比不贴更糟 | 本轮**不改解析器**（改则动 FR-05 契约与既有历史语义）。缓解：接力前按"一格一条"复制。**待裁决**：是否在帮助窗写清这条 |
| N12 | 兜底键与输入法/第三方抢键（**v2.3.3 起键位是 `` Alt+` ``**，此前是 `Ctrl+Alt+空格`）；`kModNoRepeat` Win7 不识别（已做"失败退回不带它重试"） | 兜底键注册不上，而**失败只写进日志、界面无感**；即使注册上了，界面上也没有任何地方告诉用户这把键存在 | **旧键位已实测可用**：2026-10-05 走查中按 `Ctrl+Alt+空格` 之后日志出现 `[relay] 贴第 1 条` + `[paste]` 两行并贴出本轮合成 token（注册失败只写 warn、不会有 paste 行，所以这两行就是反证，而不是读了注册的返回值）。**新键位只证到"注册成功"**：2026-10-06 换版上机后启动日志最后 40 行内 `[W]`/`[E]` 0 行 → 两把 `RegisterHotKey` 都返回 TRUE。**仍挂**：① 真人按 `` Alt+` `` 能否触发接力**未实测**（`MOD_NOREPEAT` 行为、按住不放开会不会连发）；② 第三方软件/输入法抢键的场景没测（旧键位也只排除了"本机当下没被占"）；③ `kModNoRepeat` 的 Win7 分支未跑；④ **改键后 v2.3.0 那条"兜底热键仍能贴"的判据需重跑**（驱动器 `relay.ps1 -HotkeyRelay` 已改发新键，本轮未跑） |

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

本文是 SuperClip C++ 版的完整实现契约：§2 的 FR 语义、§4 的算法、§5 的状态机、§6 的 Win32 集成、§7 的 UI、§8 的持久化与数据互通、§12 的测试与 §13 的验收条款，任一开发者或 AI 依此即可在 Windows + MSVC 环境从零重建可运行应用，无需参考既有源码。§0.1 的 **14 项矛盾取值（C1–C7、C9–C15；C8 记在技术方案 §3.3）**、§14 的 **T1–T6 决议**与全部 ADR 为强制约定，实现者不得自行改回。模块级接口、消息路由表、降级矩阵与构建脚本细节见配套文档 `doc/PROJECT.md`。**C15 与 §5.5/AC-9 是 v2.2.0 新增、v2.3.0 改为无开关**：规范原文 `doc/SuperClip_设计规范.html` 里**尚未写入接力条款**（改契约原文需单独授权），本文档不构成对规范的修改。
