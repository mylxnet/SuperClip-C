# 更新日志

SuperClip 超级剪贴板 · C++ 重写版（Win32 + Direct2D 全自绘，单文件免运行时）· by Mr lin

版本号规则见 `agent.md` 四：每完成一轮改动升一个小版本，`app.rc` 的 `FILEVERSION`、
`src/core/Config.h` 的 `kVersionText`、界面状态栏三处同号，`PackageRelease.bat` 出包前会强制校验这一条。

## 版本历史速览

| 版本 | 日期 | 一句话 |
|---|---|---|
| **v2.1.1** | 2026-10-05 | 修「启动即置顶」在真实桌面上不生效：置顶从"请求一次"改成"请求 + 事后断言 + 激活后补发"，根因是系统在本窗不在前台时静默丢掉 `WS_EX_TOPMOST` 变更；v2.1.0 的五项界面修订与快速模式规则本轮完成实机走查，README 配图换成本版实拍 |
| v2.1.0 | 2026-10-05 | 五项界面修订 + 快速模式规则：标题栏加应用图标、置顶键改形、搜索框加清除叉号、悬浮气泡挪到条目上方、修掉"点底栏署名误触下层条目收藏"的命中穿透（署名改为单击开仓库页）；快速模式选中位钉在第一行，复制完直接按空格就贴最新那条 |
| v2.0.3 | 2026-10-05 | 审计整改落地：修主构建断链（P0）、补应用图标与界面署名、交叉构建收口到 CMake 单一清单、契约文档改为实测口径 |
| v2.0.2 | 2026-10-05 | C++ 重写版首个可用版本：M1 步骤 1–11 全部落地并实机走查，步骤 12 的 A 段（发布与安装脚本）完成 |
| v2.0.1 及更早 | — | .NET / WPF 版历史，不在本仓库，见 `doc/技术方案.md` |

## 详细更新日志（按版本倒序）

### v2.1.1 · 2026-10-05

**修复**

- **启动即置顶在真实桌面上不生效**（C13 的实现缺口，被 v2.1.0 走查抓到）。根因不是解析、不是顺序，而是系统行为：
  **本窗不在前台时，`SetWindowPos(HWND_TOPMOST)` 会被静默丢弃**——返回 TRUE、`GetLastError()=0`，但 `WS_EX_TOPMOST`
  位不落。实测 `BringWindowToTop`、`SwitchToThisWindow`、由后台进程跨进程调用同样无效，**重试与延时都救不了**
  （曾 6/6 次冷启动全为 False）；一旦该窗被真实点击激活，同一条调用立刻生效。
  修法是把置顶当成"请求 + 断言"两件事：① `MainWindow::Create()` 在 `ShowWindow`/`UpdateWindow` 之后再断言一次；
  ② `WM_ACTIVATE` 里若被激活而位仍缺失，`PostMessageW(WM_APP_RAISE_TOPMOST)` **延后一条消息**补发
  （不能在该消息里直接改 z-order——`PasteTarget.cpp:76` 早就记过"系统处理完 `WM_ACTIVATE` 还会再动一次"）。
  新增自投递消息 `WM_APP_RAISE_TOPMOST = WM_APP + 6`。

**验证**

- 交叉构建 error 0；单测 41 例 / 222 断言 / 0 失败；`FileVersion=ProductVersion=2.1.1.0` 与 `kVersionText`、状态栏实拍三处同号。
- 置顶：修复后连续两次冷启动（其中一次先把前台让给靶窗）`GWL_EXSTYLE` 的 `WS_EX_TOPMOST` 位均为 1；
  走查 PREFLIGHT 亦 `topmost=True`。**`WM_ACTIVATE` 兜底分支标未验证**——修复后再没能复现出"丢带"的起始态，该分支一次都没被执行过。
- v2.1.0 遗留的两项走查本轮补齐（合成数据、每次点击前 `WindowFromPoint`→`GA_ROOTOWNER` 守卫，`GUARD_FAILS=0`）：
  五项界面修订逐条过（图标、图钉两态 `topmost` True→False→True、✕ 出现与清空、点底栏空白收藏数 2→2 不变、
  点署名后前台窗标题变 `mylxnet/SuperClip-C`、气泡在行之上且 `tip_left-win_left=51px≈3` 个全角字宽）；
  C14 快速模式两轮直接空格（不点任何条目）靶窗分别收到本轮最新那条与沉底后的下一条，应用日志两行 `[paste] … 按键事件数=4`。
- 顺带证伪了一个 QA 判据：**"点署名后有没有新浏览器进程"不能用来判定跳转**——浏览器开新标签复用同一进程，
  上一轮据此得出的通过是假证据；本轮改读前台窗口标题。

**调整**

- README 配图换成本版实拍：`readme-main/search/status.png` 重拍，新增 `readme-tip.png`（悬浮气泡）与
  `readme-title.png`（图钉开/关两态合成）；五张图逐张读图核对，画面内容全部来自 `cpp/qa/mksynthetic.ps1` 的合成条目。
- 走查驱动脚本改为**每次点击前先验命中**（`SafeClick`），并加 PREFLIGHT：冷启动后若自家窗口被别家盖住直接退出而非盲点。

### v2.1.0 · 2026-10-05

**新增**

- 标题栏左侧显示应用图标（18 DIP，按当前 DPI 向系统要 `.ico` 的对应档，1:1 设备像素绘制不重采样）；
  模式文字整体右移，可点区跟着走，图标本身仍是拖拽区。
- 搜索框内有文字时右端浮现 ✕，点一下清空（走 `EN_CHANGE` → 既有 300ms 防抖），悬停变手型；
  ✕ 出现时用 `EM_SETMARGINS` 让出右边界，打字不会钻到叉号底下。
- 单击状态栏署名 `by Mr lin` → 把仓库地址交给**系统默认浏览器**打开。程序自身仍零网络代码：
  不链 `wininet`/`winhttp`、不调 socket，只是 `ShellExecuteW(L"open")` 一次外部唤起（AC-8 边界见 `doc/DESIGN.md`）。

**修复**

- **底栏命中穿透**：列表视口只画到 `ClientH()-kStatusF`，但命中测试按 `contentY` 一路算到底，
  于是点底栏（含署名）会落到最下面那条的星标上，把不该动的条目收藏掉。现在 `y > ClientH()-kStatusF`
  一律返回 `HitZone::Status` 吃掉点击，署名区单独识别。

**调整**

- 置顶键改形：原来的"圆头直针"细线图钉在 24 DIP 里认不出来，换成实心帽 + 斜针 + 底横杠的斜图钉，
  未启用＝墨色、已启用＝accent 蓝并保留下划线。帮助窗第 9 步原文案写的是「点标题栏的 ★」，
  与画的一直不一致（★ 是列表里的收藏），现按新图形改为「点标题栏的图钉」；第 7 步补上搜索框 ✕ 的说明。
- 悬浮全文气泡从"条目下方"改到"条目**上方**、右移 3 个全角字宽"（字宽按气泡字体实测一个「中」，随 DPI 缩放），
  仍按工作区钳位；第一条上方放不下时回落到下方。
- **快速模式的选中位钉在第一行**（用户 2026-10-05 提出"绑定后始终自动选中第一行，按空格直接复，不要每次先点一下"）：
  切换模式、新内容入列、过滤/搜索、点选绑定完成后都会把选中位钉回显示区第一条，粘过的那条沉底后第一行自然就是下一条未粘贴。
  规则收口在新增的 `Store::AnchorQuickSelection()` 一处（普通模式什么都不做），主窗在快速模式下会把选中行滚进视口。
  **粘贴去向不变**——用户同轮选定"仍粘回当前焦点窗口"：绑定进程→该进程的窗口，未绑定→呼出前那个窗口，`FR-10` 契约原文一字未改。
  单测新增 §9.4-16，规模增至 41 例 / 222 断言。

### v2.0.3 · 2026-10-05

**新增**

- 应用图标 `cpp/src/res/SuperClip.ico`（16/24/32/48/256 多尺寸，256 档 PNG 压缩），`app.rc` 以 `101 ICON` 接入，
  ID 与 `Config.h::kIconIdApp` 对齐；主窗口类补 `hIcon`/`hIconSm`，任务栏与 Alt-Tab 不再用通用图标。
- 生成流水线固化为 `cpp/scripts/make-icon.sh`（底图 `src/res/icon-1024-source.png` 入库），重跑产物 md5 一致。
- 状态栏右侧显示 `v2.0.3  by Mr lin`（版本号在署名之前），新增 `Theme::MetaRight()` 右对齐文本格式。
- `PackageRelease.bat` 出包前校验 `Config.h` 与 `app.rc` 版本号一致，不一致直接拒绝打包。
- 项目专属 WSL 发行版 `superclip`（数据在 `E:\public\superclip\wsl`，初始化脚本 `E:\public\superclip\provision.sh`，
  阿里云源），构建中间产物全部留在 WSL 内，本地只收最终 exe。
- 新增 `.gitattributes`：`*.sh` 锁 LF、`*.bat`/`*.ps1` 锁 CRLF、图标与截图按 binary 处理。
- 新增文档四件套：`doc/PROJECT_STATE.md`（状态与交接）、`doc/TESTING.md`（测试记录）、`doc/DEPLOY.md`（部署手册）、本 `CHANGELOG.md`。

**修复**

- **P0：MSVC/CMake 主构建输入集不完整**（`SC_COMMON_SOURCES` 缺 `Settings.cpp`，`SC_APP_SOURCES` 缺
  `PasteService.cpp`/`ProcessPicker.cpp`/`HelpWindow.cpp`，链接库缺 `wtsapi32`）。成因是"文档一份、脚本一份、
  CMake 一份"三份清单漂移。交叉构建实测：补前 22 处 undefined reference，补后链接通过。
- 帮助窗第 3 页文案「先用 ↑ ↓ 选中，再按空格粘贴」与 ADR「单击选中 + 空格粘贴」不符 → 改为「单击选中一条」，已实机走查读图核对。
- `Sha256.cpp` 注释宣称"失败则拒绝入列"，与实现（返回空串、条目照常入列）不符 → 注释改为如实描述。

**调整**

- 交叉构建改走 `cmake --build`（`build-tests.sh` 内不再手抄源/库清单），源与库清单从此只有 `cpp/CMakeLists.txt` 一份。
- 两份契约文档（`doc/DESIGN.md`、`doc/PROJECT.md`）改为实测口径：JSON 选型由 RapidJSON 改为自研极简实现、
  单测由 Catch2 改为自带断言器、删除与 manifest 冲突的 `VS_DPI_AWARE`、依赖面清单按"链接库/动态加载/不调用"三类重列、
  测试规模更正为 40 例 / 214 断言 / 0 失败、附录 A.4 链接库清单与 `CMakeLists.txt` 逐字一致。
- 文档目录重组：设计/技术/审计文档移入 `doc/`，根目录只留 `README.md`、`agent.md`、`cpp/`、`doc/`。
- README 按使用者向模板重写（这是什么 / 核心特性 / 界面 / 快速开始 / 技术栈 / 数据与隐私 / 项目结构 / 常用操作 /
  故障排查 / 更新日志 / 许可证 + 末尾 English 段），并配 4 张实拍图 `doc/images/readme-{main,search,help,status}.png`；
  图内列表内容全部由新增的 `cpp/qa/mksynthetic.ps1` 合成，逐张读图核对后才入库。
- 走查规程加两条硬性：`doc/TESTING.md` §4 第 0 条「先让实例退出再动文件」（理由是活实例会用内存态盖掉刚还原的文件），
  第 6 条「进 `doc/images/` 的图必须来自合成数据并逐张读图核对」；驱动器为新增的 `cpp/qa/readme_shots.ps1`。
- 第三方依赖归零（原 ADR 允许的 RapidJSON 与 Catch2 均未引入）。

**已知限制（本版仍未验证或未覆盖）**

- MSVC 真实出包未做：本机无 Windows SDK，全部构建证据来自 mingw 交叉编译，`dumpbin` 结案与 ≤3 MB 体积门待步骤 12 B。
- 16 px 档图标偏糊（白色圆角底板占画幅约 1/4），要更锐利需为 16 px 单独画一版。
- `IDWriteTextFormat::Clone` 不可用（需 `_WIN32_WINNT ≥ 0x0603`，基线 Win7），右对齐格式改走工厂再要一份。
- `app.rc` 的 `LegalCopyright` 仍是 `Copyright © SuperClip`，署名文案待裁决。
- **文档结论订正（历史落盘时机）**：本轮先误记为"只在退出链落盘、强杀即丢"，复查代码后确认实为
  **变更即落盘**（`Store.cpp` 六处 `storage_.Save`，`.tmp` + `ReplaceFileW` 原子替换；`AppContext.cpp:267`
  的 `SaveToDisk` 只是退出兜底）。据此改写了 `README.md`、`doc/DEPLOY.md` §5、`doc/PROJECT_STATE.md` 坑 #10 与
  待办表、`doc/TESTING.md` §4 第 0 条。取图轮的 `Stop-Process -Force` 因此**不丢历史**；真实暴露窗只有
  "建快照→还原"之间约 2 分钟（其间跑合成数据实例），还原后按 `Id` 逐条比对 135 条 0 缺失 0 多余；
  日志不记复制事件，那两分钟内是否发生过真实复制**无法事后证明**——这是本节唯一保留的未结项。

### v2.0.2 · 2026-10-05

**新增**

- C++20 + 纯 Win32 + Direct2D/DirectWrite 全自绘的重写版：`core`（Text/Time/Sha256/ClipItem/TableParser/Json/Store/Settings）、
  `native`（AppDirs/Clipboard/HiddenWindow/SystemInfo）、`services`（ClipboardMonitor/PasteService/StorageService/TrayService/ProcessPicker）、
  `ui`（Theme/ListRenderer/HoverTip/HelpWindow/MainWindow）、`app/AppContext`。
- 主窗：380×600 自绘列表、序号、时间、收藏星、卡片圆角、选中描边、灰显半透明、悬浮全文气泡、真 `EDIT` 搜索框（IME 完好）。
- 交互：搜索（全角/半角与大小写折叠）、过滤视图、收藏、清除（只删非收藏）、复位、双击/空格粘贴、快速模式沉底、靶心点选绑定目标进程。
- 粘贴链路：写剪贴板 + 夺前台 + `SendInput` 模拟 Ctrl+V，自粘贴回环双层防护，1000 ms 守护定时器兜底。
- 设置持久化 `settings.json`（与 .NET v2.0.2 逐字节互换读写）、历史 `history.json`（同 schema，读侧兼容 `\u` 转义与明文）。
- 帮助窗 9 步引导 + 托盘右键菜单（粘贴模式/复制模式动态文案）+ 单实例互斥体 + `TaskbarCreated` 托盘自愈 + 锁屏取消点选。
- 发布脚本：`cpp/build.bat`、`cpp/scripts/CleanAndBuild.bat`、`PackageRelease.bat`、`cpp/installer/{install,uninstall}.bat`、
  `cpp/scripts/ReleaseChecklist.md`（含干净 VM 的 AC-7/AC-8 验收操作单）。
- 逻辑层单测 40 例（自带极简断言器，零第三方），交叉构建产物在 Windows 实跑通过。

**调整**

- 与旧 .NET 版的行为差异集中记录在 `doc/DESIGN.md` §0.1（C1–C13 矛盾取值）与 §14（T1–T6 决议）：
  点选才算绑定、收藏只在【收藏】视图且不被清除、来源标注不上屏只参与搜索、启动即置顶等。
