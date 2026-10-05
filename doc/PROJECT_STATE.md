# 项目状态 / 交接文档（PROJECT_STATE）

> 用途：接手这个项目时**先读这份**，它记的是"代码里看不出来"的东西——为什么这么做、哪里踩过坑、哪些结论还没验证。
> 权威分工：需求契约 = `doc/SuperClip_设计规范.html`（v1.2）；行为与算法 = `doc/DESIGN.md`；实现细节 = `doc/PROJECT.md`；
> 逐版本历史 = `CHANGELOG.md`；测试与验证边界 = `doc/TESTING.md`；部署与分发 = `doc/DEPLOY.md`。

## 1. 当前状态

| 项 | 值 |
|---|---|
| 版本 | **v2.0.3**（`app.rc` `FILEVERSION 2,0,3,0` = `Config.h::kVersionText` = 状态栏显示，三处同号） |
| 里程碑 | M1 步骤 1–11 全部落地并实机走查；步骤 12 的 **A 段**（发布与安装脚本、依赖旁证、验收操作单）完成，**B 段（MSVC 真实出包 + 干净 VM）未做** |
| 单测 | 40 例 / 214 断言 / 0 失败（交叉构建产物在 Windows 实跑） |
| 交叉构建产物 | `SuperClip.exe` 3,637,683 B（含图标 +79,872 B）—— **非发布产物**，只用于跑单测与 UI 走查 |
| 第三方依赖 | **零**（JSON 自研、单测断言器自研、SHA-256 走系统 BCrypt） |
| 仓库 | `github.com/mylxnet/SuperClip-C`（public/main），二进制不入 git |

## 2. 环境（换机器时先看这段）

- **本机没有 MSVC / Windows SDK**（无 `cl`、`dumpbin`、VS2022、Windows Kits）。所以构建与验证路径是：
  WSL 内 mingw 交叉编译 → 产物拷到 Windows 实跑。凡"MSVC 才能证明的事"一律挂着**未验证**。
- 项目专属 WSL 发行版 `superclip`（agent.md 二.2：按项目隔离，不与其它项目共用）：
  - 数据目录 `E:\public\superclip\wsl`，底包 `E:\public\ubuntu-jammy-wsl-amd64-ubuntu22.04lts.rootfs.tar.gz`
  - 初始化：`wsl --import superclip E:\public\superclip\wsl <底包>` → `bash /mnt/e/public/superclip/provision.sh`
  - `provision.sh` 做三件事：换阿里云源、`apt-get update`、装 `ca-certificates cmake make g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64`
  - 实测版本：cmake 3.22.1、`x86_64-w64-mingw32-g++ (GCC) 10-win32 20220113`、windres 2.38
- 构建中间产物落 WSL 内 `$HOME/superclip-build`（可用 `SC_BUILD` 覆盖），本地 `cpp/build-mingw/` 只收两个 exe（agent.md 二.5）。

## 3. 构建 / 测试 / 发布命令

```bash
# 交叉构建（在 WSL 项目发行版内）：产出 build-mingw/{SuperClip.exe,sc_tests.exe}
wsl -d superclip -- bash -lc "cd /mnt/e/qcode/superclip/cpp && bash build-tests.sh"
./build-mingw/sc_tests.exe            # Windows 侧跑单测（WSL 内无 wine）
```

```bat
:: MSVC 出包（需另一台装了 VS2022 + Windows SDK 的机器；本机未验证）
cpp\build.bat                       :: 自动定位 vcvars64
cpp\scripts\CleanAndBuild.bat       :: 清 build\ 后全量重建
cpp\scripts\PackageRelease.bat      :: 抓 FILEVERSION → 校验 Config.h → 出 release\SuperClip_vX.Y.Z*.zip
```

```bash
# 图标重生成（换底图后）：wsl -d superclip -- bash /mnt/e/qcode/superclip/cpp/scripts/make-icon.sh
```

## 4. 踩过的坑（现象 / 根因 / 修复 / 教训）

| # | 现象 | 根因 | 修复 | 教训 |
|---|---|---|---|---|
| 1 | CMake/MSVC 主构建必断（`LNK2019` / undefined reference 22 处） | 同一件事有**三份互不一致的清单**（文档 A.4、`build-tests.sh`、`CMakeLists.txt`），CMake 那份漏了 4 个源与 `wtsapi32` | 补源补库；`build-tests.sh` 改为驱动 `cmake --build`，清单只留 `CMakeLists.txt` 一份 | 清单只能有一处权威；文档和脚本"照抄一份"就是漂移的温床 |
| 2 | UI 走查两次"通过"其实是假阳性（截到的是桌面） | 帮助窗**创建即隐藏**，`FindWindowW` 拿到句柄≠窗口可见 | 驱动脚本改为轮询 `IsWindowVisible` 后才截图 | 可见性判据只认 `IsWindowVisible`；页间 md5 不同不等于翻页成功 |
| 3 | 有意保留的用户历史留底被误删，不可恢复 | 清理 Temp 用了 `rm -rf sc-*` 通配 | 无（已丢） | 清理只删**本轮自己新建的那一个目录**；历次留底一律不动 |
| 4 | 还原备份后无法证明"测前用户没复制过" | 只比对了 md5，没比条目数 | 走查规程加一条：还原前打印 live 与备份各自条目数 + 是否含本轮 token，数不对就停下问 | 见 `doc/TESTING.md` 的数据防护规程 |
| 5 | `.bat` 报 `'dp0"' 不是内部或外部命令` 一类碎片错误 | `cmd.exe` 在 cp936 下把 UTF-8 中文的 lead byte 与**下一个字符**配成一对 | 所有 `.bat`/`.ps1` 内容改纯 ASCII，中文只留在 `.md` 与 C++ 源里 | 编码问题会吃掉标点，报错点离病因很远 |
| 6 | Git Bash 里 `awk '{print $5,$9}'`、`$env:APPDATA` 被吞 | 外层 shell 先做了变量展开 | 复杂命令写成 Temp 下的脚本文件再执行；或转义 `\$` | 跨 shell 传参一律避免裸 `$` |
| 7 | 构建日志 `Clock skew detected`，一度以为已解决 | 源在 `/mnt/e`（Windows mtime）与 WSL 时钟瞬时差，方向会变（实测 ±2 s 到 13 s） | 无根治；改为"改完码要看有没有 `Building CXX object` 行，别只看 `RC=0`" | 跨文件系统比时间不可靠 |
| 8 | `IDWriteTextFormat::Clone` 编译不过 | 它要求 `_WIN32_WINNT ≥ 0x0603`，项目基线是 Win7（`0x0601`） | 右对齐格式改为向工厂再要一份 | Win7 基线会砍掉一批"看起来人人都有"的 API |
| 9 | 首跑 CMake 报 `Manually-specified variables were not used: CMAKE_C_COMPILER` | `project()` 只声明 CXX | 脚本不再传该变量 | 交叉工具链参数要按项目实际语言传 |
| 10 | **走查强杀实例，丢了用户自上次落盘以来的新条目（不可恢复）** | 历史只在退出链 `SaveToDisk()` 落盘；主窗收起时 `CloseMainWindow()` 返回 false，脚本走到 `Stop-Process -Force` 兜底 | 走查规程加第 0 条：先 `ShowWindow(SW_SHOW)` 再 `CloseMainWindow()`（或 `PostMessageW(WM_CLOSE)`），**确认进程退出后才备份** | 见 `doc/TESTING.md` §4 第 0 条；"只在退出时落盘"本身是契约行为，但它让任何强杀都变成数据丢失，已作为风险登记待裁决 |

## 5. 架构决策的"为什么"（只记代码读不出来的）

- **点选才算绑定、空格才粘贴**：防误触；单击只做选中，避免鼠标一碰就把内容怼进别人窗口。
- **收藏只在【收藏】视图显示、且不参与「清除」**（C10/C12）：用户裁定收藏要永久保存；数组的
  `[收藏区 | 非收藏区]` 分区不变式**原样保留**，改的只是 `RebuildDisplay()` 的过滤条件——
  持久化顺序、`Boundary()` 插入位、`MoveToBack` 沉底、FR-04 末位淘汰都依赖这个不变式。
- **来源标注不上屏、只参与搜索**（C11）：列表要干净，但按进程名搜得到。
- **启动即置顶**（C13）；**监听窗口禁 message-only**（否则丢 `TaskbarCreated` 广播）；**键入模拟用 `SendInput`**。
- **零网络是硬红线**（AC-8）：连"文件对话框"都不引 `comdlg32`；`dwmapi`/`shcore` 走 `LoadLibraryW` 动态加载，不入链。
- **自研极简 JSON**：原 ADR 选 RapidJSON，落地时环境离线取不到包，改为约 180 行的对象数组解析器；
  副产品是第三方审计面归零。决策记录在 `src/core/Json.h` 头注释。
- **UI 层不做条件编译打桩**：无头环境跑不了 D2D/剪贴板/托盘，这部分只能实机走查，跑不了就标**未验证**。

## 6. 待办与遗留

| 项 | 状态 | 备注 |
|---|---|---|
| 步骤 12 B：MSVC 出包 + `dumpbin /dependents` + ≤3 MB 体积门 + 干净 VM 的 AC-7/AC-8 | **未做** | 本机无 SDK；操作单在 `cpp/scripts/ReleaseChecklist.md` §3 |
| 150%/200% DPI 无错位复核 | 待用户改缩放 | 步骤 6 遗留；帮助窗 DPI 排版挂同一个包 |
| N9 连贴按键竞态 | 未闭环 | `WM_WTSSESSION_CHANGE` 注入已实测可取消；`WM_ENDSESSION` 无法注入验证 |
| 靶心红/绿像素复核 + 真实注销场景 | 部分收窄 | 红色（未绑定）已由 2026-10-05 实拍图给出像素证据；**绿色（已绑定）仍待**；真实注销场景仍待。步骤 9 |
| **历史只在退出链落盘**：进程被强杀 / 断电会丢掉自上次退出以来的全部新条目 | 待裁决 | 契约写的就是"退出即落盘"，所以这是**按规格的行为**，不是 bug；但 2026-10-05 的走查事故证明代价是丢真实历史。两个可改点：① 入列后 2 s 防抖合并写盘（整文件重写，n≤500 时约几十 KB）；② `uninstall.bat` 改成先 `taskkill`（不带 `/f`）等退出链走完、再兜底 `/f`。**均涉语义变更，未动代码** |
| 16 px 图标档偏糊 | 已知限制 | 需单独画一版极简图 |
| `LegalCopyright` 文案 | 待裁决 | 现为 `Copyright © SuperClip` |
| `advapi32`/`oleaut32` 能否从链接清单删除 | 未动 | 导入表里当前无它们，属冗余项；删除属"清理"不属"修错" |
| 一次性 QA 脚本去留 | 待定 | `cpp/qa/` 现 26 个 `.ps1`（本轮新增 `mksynthetic.ps1`、`readme_shots.ps1`），多数是历轮探针；有复用价值的已并入走查规程 |
| 远端 CI | 未做 | 路径已与本地统一（`cmake --build`），剩"选哪个镜像 + 配置远端属发布动作需授权" |

## 7. 红线（不要碰）

- 严禁引入任何 HTTP/网络库或远程调用（AC-8）。
- 不写注册表自启（FR-18）；`install.bat` 只放 `.lnk`。
- `uninstall.bat` **绝不删 `%APPDATA%\SuperClip`**（用户真实剪贴板历史，删了不可恢复）。
- `cpp/build-mingw/` 下的截图含**真实剪贴板内容**，禁止入库；README 用图必须先确认只含合成数据。
- 未实机验证的结论必须标注**未验证**，不得凭代码推断宣称通过。
- 推送、建 tag、发 Release 属发布类动作，需用户触发词授权。
