# 更新日志

SuperClip 超级剪贴板 · C++ 重写版（Win32 + Direct2D 全自绘，单文件免运行时）· by Mr lin

版本号规则见 `agent.md` 四：每完成一轮改动升一个小版本，`app.rc` 的 `FILEVERSION`、
`src/core/Config.h` 的 `kVersionText`、界面状态栏三处同号，`PackageRelease.bat` 出包前会强制校验这一条。

## 版本历史速览

| 版本 | 日期 | 一句话 |
|---|---|---|
| **v2.0.3** | 2026-10-05 | 审计整改落地：修主构建断链（P0）、补应用图标与界面署名、交叉构建收口到 CMake 单一清单、契约文档改为实测口径 |
| v2.0.2 | 2026-10-05 | C++ 重写版首个可用版本：M1 步骤 1–11 全部落地并实机走查，步骤 12 的 A 段（发布与安装脚本）完成 |
| v2.0.1 及更早 | — | .NET / WPF 版历史，不在本仓库，见 `doc/技术方案.md` |

## 详细更新日志（按版本倒序）

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
- 走查规程加两条硬性：`doc/TESTING.md` §4 第 0 条「先优雅退出再动文件」（历史只在退出链落盘，强杀即丢），
  第 6 条「进 `doc/images/` 的图必须来自合成数据并逐张读图核对」；驱动器为新增的 `cpp/qa/readme_shots.ps1`。
- 第三方依赖归零（原 ADR 允许的 RapidJSON 与 Catch2 均未引入）。

**已知限制（本版仍未验证或未覆盖）**

- MSVC 真实出包未做：本机无 Windows SDK，全部构建证据来自 mingw 交叉编译，`dumpbin` 结案与 ≤3 MB 体积门待步骤 12 B。
- 16 px 档图标偏糊（白色圆角底板占画幅约 1/4），要更锐利需为 16 px 单独画一版。
- `IDWriteTextFormat::Clone` 不可用（需 `_WIN32_WINNT ≥ 0x0603`，基线 Win7），右对齐格式改走工厂再要一份。
- `app.rc` 的 `LegalCopyright` 仍是 `Copyright © SuperClip`，署名文案待裁决。
- **历史只在退出链落盘**（契约即"退出即落盘"）：进程被强杀或断电会丢掉自上次退出以来的全部新条目。
  是否改为入列后防抖落盘属产品语义变更，待裁决。本轮 README 取图时踩到过一次：`CloseMainWindow()` 对隐藏窗失效
  → 脚本兜底强杀 → 用户自上次落盘以来的内存条目丢失，**不可恢复**；规程已补（`doc/TESTING.md` §4 第 0 条）。

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
