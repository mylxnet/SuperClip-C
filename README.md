# SuperClip

> **SuperClip** · C++20 + 纯 Win32 + Direct2D 全自绘 · 单机剪贴板历史管理器
>
> 复制过的内容它替你留着：一个全局热键呼出列表，选中即粘回你要的地方。不联网、不装运行库、数据只在你自己的机器上。

Windows 7 SP1 – 11（x64） · 版本 [v2.0.3](CHANGELOG.md) · 交付 单文件 exe · 网络 [零（需求红线 AC-8）](doc/DESIGN.md) · 授权 见[许可证](#许可证)

[English](#english) | [中文](#这是什么)

---

## 这是什么

- **复制过的东西还能再粘**。系统剪贴板只存最后一条，SuperClip 把最近的都留着（上限 500 条），随时翻出来重新粘。
- **不用切换窗口**。`Ctrl` + `` ` `` 在任何应用里呼出列表，选中就粘回你刚才的光标位置，用完自动收起。
- **能从 Excel/WPS 一次粘一格**。复制一片区域，它按单元格拆成一条条，逐格粘贴，不用再手动切。
- **常用的那几条可以收藏**。收藏项不会被自动淘汰、也不会被「清除」误删。
- **完全不联网**。程序里没有任何网络代码，历史只写在 `%APPDATA%\SuperClip\` 的一个 JSON 文件里。

## 核心特性

| 特性 | 描述 |
|---|---|
| 自动记录 | 后台监听剪贴链，文本按 SHA-256 去重后置顶；上限 500 条，末位淘汰，收藏项不参与淘汰 |
| 表格拆分 | 复制表格区域时拆成逐格条目并记住来源行列；来源标注不占界面，但仍能被搜到 |
| 两种粘贴模式 | **普通**＝双击一条即粘贴并收起；**快速**＝列表常驻，空格/回车连续粘贴，粘过的条目灰显沉底 |
| 目标绑定 | 默认粘回「呼出前那个窗口」；点工具栏的靶心（「绑定」）进入点选模式，可把目标固定到某个进程 |
| 搜索 / 过滤 / 收藏 | 300ms 防抖子串搜索；`全部 / 文本 / 表格单元格 / 收藏` 四个视图；收藏只在【收藏】视图出现且不被清除 |
| 界面 | 380×600 无边框自绘窗口（可拖拽、可缩放、可置顶）、悬停浮现全文气泡、9 步使用帮助 |
| 系统集成 | 托盘常驻、`Ctrl+`` 全局热键、单实例、Per-Monitor V2 DPI 感知、退出即落盘 |
| 免运行库 | 静态链接 CRT，目标机不需要 .NET，也不需要 VC++ 运行库 |

## 界面

| ![主窗列表](doc/images/readme-main.png) | ![搜索过滤](doc/images/readme-search.png) |
|---|---|
| 主窗口：序号、内容预览、时间、收藏星标，底栏显示版本与署名 | 搜索框输入即过滤（300ms 防抖），列表实时收窄 |

| ![使用帮助](doc/images/readme-help.png) | ![底栏署名](doc/images/readme-status.png) |
|---|---|
| 9 步使用帮助，按钮或 ← → 翻页，`Esc` 关闭 | 底栏放大 2×：状态提示在左，`v2.0.3  by Mr lin` 在右 |

## 快速开始

### 使用者（拿到即用）

1. 下载 `SuperClip_v2.0.3_portable.zip`（Releases 页），解压到任意目录。
2. 双击 `SuperClip.exe`。托盘出现图标即已在后台记录，无需其他设置。
3. 在任何应用里按 `Ctrl` + `` ` ``（数字 1 左边那个键）呼出列表，双击一条粘到光标处。

想装进开始菜单（可选，需要管理员）：运行 `installer\install.bat`，它只做三件事——把 exe 复制到
`%ProgramFiles%\SuperClip\`、建开始菜单与桌面快捷方式、告诉你数据位置。**不写注册表、不做开机自启**（需求 FR-18）。
需要开机自启请自己在「启动」文件夹里放那个快捷方式，那是你系统的设置，不是程序的行为。

卸载：以管理员运行 `installer\uninstall.bat`。它只删程序目录和两条快捷方式，
**永远不动 `%APPDATA%\SuperClip`**——那里是你的真实历史，删了不可恢复。

### 开发者（从源码构建）

```bash
# 路径 A：Windows + VS2022（发布用的正式构建）
cd cpp
build.bat            # 自动定位 vcvars64.bat → cmake -S . -B build → --config Release
```

```bash
# 路径 B：WSL / Linux 上的 mingw 交叉编译（没有 MSVC 时的验证路径，不是发布构建）
cd cpp
bash build-tests.sh  # 产出 build-mingw/{SuperClip.exe, sc_tests.exe}，把 exe 拿回 Windows 跑

# 在 Windows 侧执行测试，预期"用例 40，断言 214 项，失败 0"
build-mingw\sc_tests.exe
```

两条路径的源清单与链接库清单**都只由 `cpp/CMakeLists.txt` 提供一份**，脚本不再手抄。
其余构建、打包、验收说明见 [doc/PROJECT_STATE.md](doc/PROJECT_STATE.md) 与 [doc/DEPLOY.md](doc/DEPLOY.md)。

## 技术栈

| 层 | 选型 | 说明 |
|---|---|---|
| 语言 | C++20 | 无异常路径依赖，Win7 基线（`_WIN32_WINNT = 0x0601`） |
| 窗口与消息 | 纯 Win32（user32/kernel32） | 三窗口拓扑：主窗 + 隐藏监听窗 + 帮助窗，自有消息循环 |
| 渲染 | Direct2D + DirectWrite | 全部界面自绘，只有搜索框是原生 `EDIT`（IME 输入正常） |
| 系统集成 | `RegisterHotKey` / `Shell_NotifyIcon` / WTS 会话通知 | 锁屏、注销时立即取消点选，不残留系统光标 |
| 数据 | 自研极简 JSON（约 180 行） | 字段与 .NET v2.0.2 完全一致，两版可直接接管同一份历史 |
| 哈希 | BCrypt（`bcrypt.dll`） | SHA-256 去重；系统组件，不引第三方库 |
| 构建 | CMake 3.20 + MSVC，或 mingw-w64 交叉 | 静态 CRT，交付物单文件 |
| 测试 | `tests/test_main.cpp` 自带极简断言器 | 40 例覆盖纯逻辑层；UI 与粘贴行为靠实机走查 |

**明确不引入**：.NET / Qt / WinUI / wxWidgets、任何网络库、任何第三方 JSON 或测试框架。

## 数据与隐私

- 全部数据只在三个本地文件里：`%APPDATA%\SuperClip\history.json`（条目）、`settings.json`（位置与偏好）、`error.log`（诊断）。
- 程序**不含任何网络代码**：不发起连接、不上传、不遥测。这条是需求红线 AC-8，交付前的干净 VM 断网验收见 [doc/DEPLOY.md](doc/DEPLOY.md)。
- 不写注册表，不装服务，不做开机自启。
- 单条内容超过 256KB 会被截断后入列；总量 500 条，超出按末位淘汰，收藏项不参与。
- 「清除」只清非收藏项；卸载不删数据；要彻底清空请自己删那个目录。
- 里面是你复制过的所有内容（可能含密码、令牌）。不要把 `history.json` 发给别人，也不要放进同步盘。

## 项目结构

```
README.md                    本文（面向使用者）
CHANGELOG.md                 版本历史（逐版本的新增/优化/修复/调整）
cpp/                         主代码目录
  CMakeLists.txt             源清单与链接库清单的唯一权威
  build.bat                  MSVC 构建入口
  build-tests.sh             WSL/Linux mingw 交叉构建
  src/                       core（纯逻辑）· native（Win32 RAII）· services · ui · app · res · util
  tests/test_main.cpp        纯逻辑单测（40 例）
  tools/PasteTarget.cpp      粘贴闭环走查用的极简目标程序
  qa/*.ps1                   实机走查驱动（内容一律 ASCII）
  scripts/                   发布与验收脚本（CleanAndBuild / PackageRelease / ReleaseChecklist / make-icon）
  installer/                 install.bat · uninstall.bat
doc/                         文档（另有一个 images/ 子目录放本文配图）
  SuperClip_设计规范.html     需求契约（FR/AC 条目，v1.2）
  DESIGN.md                  契约级设计：FR 映射、算法规范、矛盾取值 C1–C13、ADR
  PROJECT.md                 实现级设计：接口签名、消息路由、测试方案、里程碑判据
  PROJECT_STATE.md           当前状态、环境搭建、踩过的坑、架构决策的理由
  TESTING.md                 测了哪些用例、实际结果、已知限制
  DEPLOY.md                  部署与运维：安装/卸载/数据接管/故障排查
  审计核实与整改清单.md        逐条审计结论与整改落地情况
  技术方案.md                 .NET 旧版行为说明（其 §1 技术栈章节已作废）
  images/                    本文用到的界面实拍图
```

## 常用操作

| 想做什么 | 怎么做 |
|---|---|
| 呼出 / 收起列表 | `Ctrl` + `` ` ``；或双击托盘图标 |
| 记一条内容 | 在任何应用里正常复制（`Ctrl+C`），不需要额外动作 |
| 粘回刚才的光标处 | 普通模式下双击那条 |
| 连续粘多条 | 点标题栏的模式文字（或右键选【粘贴模式】）切到快速 → `↑`/`↓` 选中 → 按空格；粘过的会灰显沉底 |
| 找内容 | 直接敲关键字，300ms 后自动过滤；搜索框内按回车把焦点交回列表 |
| 只看表格单元格 / 只看收藏 | 工具栏第一个按钮选视图（全部 / 文本 / 表格单元格 / 收藏） |
| 收藏一条 | 点那一行右侧的星标。收藏项只在【收藏】视图出现，不会被清除、不会被淘汰 |
| 清掉历史 | 工具栏「清除」——只清非收藏项，底栏会告诉你清了几个、留了几个 |
| 恢复原始顺序 | 工具栏「复位」：按时间重排并清掉灰显标记 |
| 换个粘贴目标 | 点工具栏的靶心（「绑定」）进入点选 → 再点目标窗口。红色＝未绑定（粘回收起前的窗口），绿色＝已绑定该进程；点选期间再按一次靶心或 `Esc` 取消 |
| 看被截断的全文 | 鼠标停在条上稍等，气泡浮现全文（最多 2000 字） |
| 移动 / 改大小 / 置顶 | 拖标题栏空白处；拖窗口边缘；点标题栏的置顶按钮 |
| 看使用教程 | 窗口内右键 →「使用帮助」，或按 `Apps` 键唤出菜单；`←`/`→` 翻页，`Esc` 关闭 |
| 退出 | 标题栏 ✕ 或托盘菜单「退出」（退出即落盘，收起≠退出，收起后仍在记录） |

## 故障排查

| 症状 | 原因与处理 |
|---|---|
| `Ctrl+`` 没反应 | 该组合键被别的程序占了。日志里会写「热键被占用」；此时用**双击托盘图标**呼出，其余功能不受影响 |
| 托盘图标不见了 | Explorer 重启过；正常情况下程序收到 `TaskbarCreated` 广播会自己重挂。若日志有「NIM_ADD 失败」则托盘整体不可用 |
| 列表一直是空的 | `%APPDATA%\SuperClip` 不可写，或被同名**文件**占位。这时程序按设计只走内存态、日志也不落盘；清掉占位文件后重启 |
| 窗口跑到屏幕外 | 多显示器拔掉后坐标越界。越界值会自动回默认停靠；也可以直接删 `settings.json` 让它重建 |
| 高 DPI 下位置/尺寸不对 | 位置以 DIP 存储、按当前缩放换算；150%/200% 的排版复核尚未结案，先固定缩放使用 |
| 粘不到目标窗口 | 多半是绑定了别的目标（靶心是绿的）。点靶心取消，或让目标窗口先成为「呼出前那个窗口」 |
| 想确认它没联网 | 给它加一条出站阻止规则再连续复制粘贴 100 次，防火墙日志与 `error.log` 都应为干净——完整验收单在 `doc/DEPLOY.md` 与 `cpp/scripts/ReleaseChecklist.md` |

更多见 [doc/DEPLOY.md](doc/DEPLOY.md#7-故障排查)。

## 更新日志

详见 [CHANGELOG.md](CHANGELOG.md)。当前版本 **v2.0.3**。

## 许可证

当前版本**还没有正式的开源许可证文件**。界面底栏的署名是 `v2.0.3  by Mr lin`，
而 `cpp/src/res/app.rc` 的 `LegalCopyright` 仍是 `Copyright © SuperClip`（是否补署名待项目所有者裁决）。
采用哪种许可证（MIT / GPL / 仅闭源分发）也要由所有者确定后再补 `LICENSE` 文件，
并把 `LegalCopyright` 一并改成同一措辞。

---

## English

**What is this** — SuperClip is a local clipboard-history manager for Windows. It records what you copy,
keeps the most recent 500 entries (favorites never expire), and pastes any of them back into the window you
were working in. One global hotkey (`Ctrl` + `` ` ``) brings up the list; nothing else needs to be open.

**Tech stack** — C++20, plain Win32 message loop, everything drawn with Direct2D/DirectWrite.
Hand-rolled ~180-line JSON reader, BCrypt for SHA-256, statically linked CRT.
No .NET, no Qt, no runtime prerequisites, **no network code at all**.

**Quick start** — unzip `SuperClip_v2.0.3_portable.zip`, run `SuperClip.exe`, press `Ctrl` + `` ` ``.
Optionally run `installer\install.bat` as administrator for Start Menu / Desktop shortcuts.
Uninstalling never deletes your history.

**Data & privacy** — everything lives in `%APPDATA%\SuperClip\` (`history.json`, `settings.json`, `error.log`).
No uploads, no telemetry, no registry writes, no autostart. Your clipboard may contain secrets, so treat that
folder like a password file.

**Build from source** — `cd cpp && build.bat` (VS2022) or `bash build-tests.sh` for the mingw cross-build;
unit tests run on Windows: 40 cases / 214 assertions / 0 failures.

**License** — no license file has been chosen yet. The app's status bar signs "v2.0.3  by Mr lin", while the
version resource in `app.rc` still reads "Copyright © SuperClip"; both the terms and that wording are the
project owner's call.
