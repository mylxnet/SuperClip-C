# SuperClip

> **SuperClip** · C++20 + 纯 Win32 + Direct2D 全自绘 · Windows 本地剪贴板历史管理器
>
> 复制过的内容它替你留着：**一次复制多条，每条按需粘贴**——随时贴回任何窗口，不用回头再复制。

**平台** Windows 10 / 11（x64）（[设计下限 Win7 SP1](doc/DESIGN.md)，未实机验证） · **交付** 单文件 exe、免运行库 · **网络** [零（需求红线 AC-8）](doc/DESIGN.md) · **版本** [v2.5.0](CHANGELOG.md) · **下载** [Releases](https://github.com/mylxnet/SuperClip-C/releases)

[English](#english) | [中文](#这是什么)

---

## ✨ 这是什么

- **它解决什么问题**：Windows 的剪贴板一次只记得住**最后一条**。而真实场景往往是**连着复制好几条**，再分别贴到不同地方——系统只留得住最后一条，前面那些只能反复「复制一次、粘贴一次」再走一遍。
- **它怎么做**：把最近复制过的内容**都留着**（上限 500 条，收藏的永不淘汰），所以你可以**一次连着复制多条**；之后 `Ctrl` + `` ` `` 在任何应用里呼出列表，**哪条需要就贴哪条**——选中即贴回你刚才的光标位置，同一条想贴几次就贴几次，**不必再回去复制一遍**。
- **连续粘贴不用重复操作**：切到快速模式，选中位始终停在第一行。在别的程序里复制一条，它自动排到第一行，直接按空格就贴出去；贴过的灰显沉底，下一条自动上位。
- **填表能接力**：快速模式下按住 `Alt` 点哪个输入框，就把列表第一行贴进那个框，一行一行填下去，手不用离开键盘。
- **从 Excel / WPS 一次粘一格**：复制一片区域，它按单元格拆成一条条，逐格粘贴。
- **完全不联网**：程序里没有任何网络代码，历史只写在你自己机器的 `%APPDATA%\SuperClip\` 里。

## 🎯 核心特性

| 特性 | 描述 |
|---|---|
| 多条留存 | 后台监听剪贴板，**连着复制的内容逐条留下、不被后一条顶掉**；文本按 SHA-256 去重后置顶，上限 500 条，超出按末位淘汰，收藏项不参与淘汰 |
| 随时呼出 | 全局热键 `` Ctrl+` ``，在任何应用里调出列表；热键被占用时可双击托盘图标 |
| 按需粘贴 | 需要哪条就选哪条，贴回「呼出前那个窗口」的光标处；同一条可反复贴，用完自动收起 |
| 连续粘贴（快速模式） | 列表常驻，选中位始终钉在第一行；按空格连续粘贴，粘过的条目灰显沉底 |
| 填表接力（无开关） | 快速模式且列表在屏幕上时，`Alt` + 左键点目标输入框＝把看见的第一行贴进去，贴过的沉底、下一条上位；`` Alt+` `` 是同效兜底 |
| 表格拆分 | 复制表格区域自动拆成逐格条目并记住来源行列；来源标注不占界面，但仍能被搜到。**按行优先，空行与空格子会被跳过** |
| 目标绑定 | 默认粘回「呼出前那个窗口」；点工具栏靶心可把目标固定到某个进程 |
| 搜索 / 过滤 / 收藏 | 300ms 防抖子串搜索；`全部 / 文本 / 表格单元格 / 收藏` 四个视图；收藏只在【收藏】视图出现，且不被清除、不被淘汰 |
| 界面 | 380×600 无边框自绘窗口（可拖拽、可缩放、可置顶），标题栏带应用图标；悬停 0.4 秒在该条上方浮现全文气泡；内置 9 步使用帮助 |
| 系统集成 | 托盘常驻、单实例、Per-Monitor V2 DPI 感知、变更即落盘 |
| 免运行库 | 静态链接 CRT，目标机不需要 .NET，也不需要 VC++ 运行库 |

## 📸 界面

| ![主窗列表](doc/images/readme-main.png) | ![搜索过滤](doc/images/readme-search.png) |
|---|---|
| 主窗口：标题栏左侧应用图标，序号、内容预览、时间、收藏星标，底栏显示版本与署名 | 搜索框输入即过滤（300ms 防抖），右端 ✕ 清空，列表实时收窄 |

| ![使用帮助](doc/images/readme-help.png) | ![底栏署名](doc/images/readme-status.png) |
|---|---|
| 9 步使用帮助，按钮或 `←` `→` 翻页，`Esc` 关闭 | 底栏放大 2×：状态提示在左，版本号 + `by Mr lin` 在右（单击署名打开仓库页） |

| ![悬浮全文气泡](doc/images/readme-tip.png) | ![标题栏两态](doc/images/readme-title.png) |
|---|---|
| 悬停 0.4 秒在该条**上方**浮现全文气泡，未截断的行不弹 | 标题栏两态：上＝置顶开（accent 色图钉 + 下划线），下＝置顶关（墨色图钉）；右侧 `✕` 是彻底退出 |

> 图内列表内容全部是走查脚本生成的**合成条目**，不含任何真实剪贴板数据。

## 🚀 快速开始

### 使用者（拿到即用）

1. 到 [Releases](https://github.com/mylxnet/SuperClip-C/releases) 下载 `SuperClip_v2.5.0_portable.zip`，解压到任意目录。
2. 双击 `SuperClip.exe`。托盘出现图标即已在后台记录，无需其他设置。
3. 在任何应用里按 `Ctrl` + `` ` ``（数字 1 左边那个键）呼出列表，双击一条粘到光标处。

> **前提**：Windows 10 / 11（x64），免运行库（静态链接 CRT，无需 .NET / VC++ 运行库）。
> 附件由 **mingw-w64 交叉构建**（开发机无 Visual Studio 与 Windows SDK），体积约 3.47 MB，略超技术方案的 3 MB 目标线；构建与发布的完整说明见 [doc/PROJECT_STATE.md](doc/PROJECT_STATE.md)。

**装进开始菜单（可选，需要管理员）**：运行 `installer\install.bat`，它只做三件事——把 exe 复制到 `%ProgramFiles%\SuperClip\`、建开始菜单与桌面快捷方式、告诉你数据位置。**不写注册表、不做开机自启**。需要开机自启请自己在「启动」文件夹里放那个快捷方式，那是你系统的设置，不是程序的行为。

**卸载**：以管理员运行 `installer\uninstall.bat`。它只删程序目录和两条快捷方式，**永远不动 `%APPDATA%\SuperClip`**——那里是你的真实历史，删了不可恢复。

### 开发者（从源码构建）

```bash
# 路径 A：Windows + VS2022（发布用的正式构建）
cd cpp
build.bat                    # 自动定位 vcvars64.bat → cmake -S . -B build → --config Release
scripts\PackageRelease.bat   # 出版本化 exe + portable zip
```

```bash
# 路径 B：WSL / Linux 上的 mingw 交叉编译（没有 MSVC 时的验证路径）
cd cpp
bash build-tests.sh          # 产出 build-mingw/{SuperClip.exe, sc_tests.exe}

# 在 Windows 侧执行测试，预期"用例 40，断言 219 项，失败 0 项 → 全部通过"
build-mingw\sc_tests.exe
```

其余构建、打包、验收说明见 [doc/PROJECT_STATE.md](doc/PROJECT_STATE.md) 与 [doc/DEPLOY.md](doc/DEPLOY.md)。

## 🛠️ 技术栈

| 层 | 选型 | 说明 |
|---|---|---|
| 语言 | C++20 | 无异常路径依赖，Win7 基线（`_WIN32_WINNT = 0x0601`） |
| 窗口与消息 | 纯 Win32（user32 / kernel32） | 三窗口拓扑：主窗 + 隐藏监听窗 + 帮助窗，自有消息循环 |
| 渲染 | Direct2D + DirectWrite | 全部界面自绘，只有搜索框是原生 `EDIT`（IME 输入正常） |
| 系统集成 | `RegisterHotKey` / `Shell_NotifyIcon` / WTS 会话通知 | 锁屏、注销时立即取消点选，不残留系统光标 |
| 低版本兼容 | 全部 `GetProcAddress` 探测 + 回退链 | 新 API 逐级回退，`MOD_NOREPEAT` 注册失败即退回可连发 |
| 数据 | 自研极简 JSON（约 180 行） | 字段与 .NET v2.0.2 完全一致，两版可直接接管同一份历史 |
| 哈希 | BCrypt（`bcrypt.dll`） | SHA-256 去重；系统组件，不引第三方库 |
| 构建 | CMake 3.20 + MSVC，或 mingw-w64 交叉 | 静态 CRT，交付物单文件 |
| 测试 | `tests/test_main.cpp` 自带极简断言器 | 40 例覆盖纯逻辑层；UI 与粘贴行为靠实机走查 |

产物只导入 10 个系统 DLL（`bcrypt` `d2d1` `DWrite` `GDI32` `KERNEL32` `msvcrt` `ole32` `SHELL32` `USER32` `WTSAPI32`），**其中网络库 0 个**——这是「不联网」的静态旁证。

**明确不引入**：.NET / Qt / WinUI / wxWidgets、任何网络库、任何第三方 JSON 或测试框架。

## 🔒 数据与隐私

- 全部数据只在两个本地文件里：`%APPDATA%\SuperClip\history.json`（条目）、`error.log`（诊断）。
- **不记忆任何设置**（v2.5.0 起）：视图、粘贴模式、置顶、绑定目标、窗口位置与尺寸都只在本次运行内有效，**重启回到默认**（全部视图 / 普通粘贴 / 置顶 / 右缘停靠 / 未绑定）。程序不再读写 `settings.json`；若该文件曾由旧版本生成，它仍会留在原处，只是不再被读取。
- 程序**不含任何网络代码**：不发起连接、不上传、不遥测。
- 不写注册表，不装服务，不做开机自启。
- **粘贴会覆盖系统剪贴板**：实现是「把该条写进剪贴板 → 发 `Ctrl+V`」，所以你原先复制的内容会被替换掉。这是设计固有语义，不是缺陷，但值得先知道。
- 单条内容超过 256KB 会被截断后入列；总量 500 条，超出按末位淘汰，收藏项不参与。
- 「清除」只清非收藏项；卸载不删数据；要彻底清空请自己删那个目录。
- 里面是你复制过的所有内容（可能含密码、令牌）。不要把 `history.json` 发给别人，也不要放进同步盘。

## 📁 项目结构

```
README.md                    本文（面向使用者）
CHANGELOG.md                 版本历史（逐版本的新增/优化/修复/调整）
cpp/                         主代码目录
  CMakeLists.txt             源清单与链接库清单的唯一权威
  build.bat                  MSVC 构建入口
  build-tests.sh             WSL/Linux mingw 交叉构建
  src/                       core（纯逻辑）· native（Win32 RAII）· services · ui · app · res · util
  tests/test_main.cpp        纯逻辑单测（40 例 / 219 断言）
  tools/PasteTarget.cpp      粘贴闭环走查用的极简目标程序
  qa/*.ps1                   实机走查驱动（内容一律 ASCII）
  scripts/                   发布与验收脚本（CleanAndBuild / PackageRelease / ReleaseChecklist / make-icon）
  installer/                 install.bat · uninstall.bat
doc/                         文档（另有一个 images/ 子目录放本文配图）
  SuperClip_设计规范.html     需求契约（FR/AC 条目，v1.2）
  DESIGN.md                  契约级设计：FR 映射、算法规范、矛盾取值 C1–C15、ADR
  PROJECT.md                 实现级设计：接口签名、消息路由、测试方案、里程碑判据
  PROJECT_STATE.md           当前状态、环境搭建、踩过的坑、架构决策的理由
  TESTING.md                 测了哪些用例、实际结果、已知未验证清单
  DEPLOY.md                  部署与运维：安装/卸载/数据接管/故障排查/回滚
  审计报告.md                第三方视角的问题清单（S/N/T 编号）
  审计核实与整改清单.md        逐条审计结论与整改落地情况
  技术方案.md                 .NET 旧版行为说明（其 §1 技术栈章节已作废）
  images/                    本文用到的界面实拍图
```

## 🖱️ 常用操作

| 想做什么 | 怎么做 |
|---|---|
| 呼出 / 收起列表 | `Ctrl` + `` ` ``；或双击托盘图标 |
| 记一条内容 | 在任何应用里正常复制（`Ctrl+C`），不需要额外动作 |
| 粘回刚才的光标处 | 普通模式下双击那条 |
| 连续粘多条 | 点标题栏的模式文字（或右键选【粘贴模式】）切到快速 → **选中位始终钉在第一行**：新复制进来的内容会自动成为选中项，直接按空格就粘最新那条；粘过的会灰显沉底，选中位回到第一行＝下一条未粘贴 |
| 连续填表（接力） | **不用开启**：处于快速模式且列表在屏幕上时，**`Alt`+左键**点目标程序的输入框＝把**你看见的第一行**贴进那个框，贴过的灰显沉底、下一条自动上位；在别的程序里正常复制，新内容自然回到第一行接着贴。`` Alt+` `` 同效（贴在当前前台窗）。全部贴完不会自动停，只在状态栏提示；收起列表、切回普通模式、锁屏或退出即停止 |
| 找内容 | 直接敲关键字，300ms 后自动过滤；搜索框内按回车把焦点交回列表；框里有字时右端出现 ✕，点一下清空 |
| 只看表格单元格 / 只看收藏 | 工具栏第一个按钮选视图（全部 / 文本 / 表格单元格 / 收藏） |
| 收藏一条 | 点那一行右侧的星标。v2.4.2 起**条目不当场消失**：那一行留在原位、星标立刻翻转，**下一次列表刷新**才按新状态移出【全部】等页面；取消收藏对称处理。收藏项不会被清除、不会被淘汰 |
| 清掉历史 | 工具栏「清除」——只清非收藏项，底栏会告诉你清了几个、留了几个 |
| 恢复原始顺序 | 工具栏「复位」：按时间重排并清掉灰显标记 |
| 换个粘贴目标 | 点工具栏的靶心（「绑定」）进入点选 → 再点目标窗口。红色＝未绑定（粘回收起前的窗口），绿色＝已绑定该进程；点选期间再按一次靶心或 `Esc` 取消 |
| 看被截断的全文 | 鼠标停在条上稍等（约 0.4 秒），气泡在**这条的上方**浮现全文（最多 2000 字） |
| 移动 / 改大小 / 置顶 | 拖标题栏空白处；拖窗口边缘；点标题栏的图钉——图钉变蓝并带下划线＝已置顶（启动默认就是置顶） |
| 打开项目页 | 单击底栏的 `by Mr lin`，交给系统默认浏览器打开本仓库页（程序自身不联网） |
| 看使用教程 | 窗口内右键 →「使用帮助」，或按 `Apps` 键唤出菜单；`←`/`→` 翻页，`Esc` 关闭（共 9 步） |
| 退出 | 标题栏 ✕ 或托盘菜单「退出」。收起≠退出，收起后仍在记录 |

## 🐛 故障排查

下表同时收录**已知限制**（实测过的，不是待办）。更完整的实机结论见 [doc/DEPLOY.md](doc/DEPLOY.md) 与 [doc/PROJECT_STATE.md](doc/PROJECT_STATE.md)。

| 症状 | 原因与处理 |
|---|---|
| `` Ctrl+` `` 没反应 | 该组合键被别的程序占了。日志里会写「热键被占用」；此时用**双击托盘图标**呼出，其余功能不受影响。界面上暂无提示（T5 未落地） |
| `` Alt+` `` 没反应 | 同上；也可能你不在快速模式、或列表已收起——接力此时按设计不生效 |
| 接力点了输入框但没贴进去 | 先看是不是 WPS/Excel 的**单元格**：点单元格只是选中、没进编辑态，**已知贴不进去**，双击进入单元格编辑态再点即可；记事本、浏览器输入框均正常。其余场合看 `%APPDATA%\SuperClip\error.log` 里 `relay` 那几行——日志说"贴第 N 条"只代表它**发了**，不代表目标收到了 |
| 表格粘贴出来条目数比格子少 | 拆分**跳过空行与空单元格**，源表有空格子时条目会少。缓解办法：接力前按「一格一条」复制 |
| `` Alt+` `` 在界面上找不到 | 这把键只在文档里说明，界面不提示（v2.4.0 曾加、v2.4.1 按决议删）。功能可用，不影响使用 |
| 托盘图标不见了 | Explorer 重启过；正常情况下程序收到 `TaskbarCreated` 广播会自己重挂。若日志有「NIM_ADD 失败」则托盘整体不可用 |
| 列表一直是空的 | `%APPDATA%\SuperClip` 不可写，或被同名**文件**占位。这时程序按设计只走内存态、日志也不落盘；清掉占位文件后重启 |
| 窗口跑到屏幕外 | 位置不再被记忆（v2.5.0 起）：每次启动都按默认停靠在工作区右缘，不会遗留上一轮的越界坐标 |
| 高 DPI 下位置 / 尺寸不对 | 位置以 DIP 存储、按当前缩放换算；150% / 200% 的排版复核**尚未结案**，先固定缩放使用 |
| 属性面板 / 任务管理器里的「文件说明」是乱码 | 你手上是 **≤ v2.3.3** 的产物。根因是资源文件缺编码声明，**v2.3.4 已修**；已出包的 exe 改不了，重新构建即可 |
| 粘不到目标窗口 | 多半是绑定了别的目标（靶心是绿的）。点靶心取消，或让目标窗口先成为「呼出前那个窗口」 |
| 在 Windows 7 / 8.1 上打不开 | **从未在真机上跑过**。代码以 Win7 SP1 为下限，但 Win7 上 `d2d1.dll`/`DWrite.dll` 来自平台更新 KB2670838，缺它程序在加载器阶段就报 `0xc0000135`；界面硬写的字体 `Microsoft YaHei UI` 也是 Win8+ 字族 |
| 想确认它没联网 | 给它加一条出站阻止规则再连续复制粘贴 100 次，防火墙日志与 `error.log` 都应为干净——完整验收单在 [doc/DEPLOY.md](doc/DEPLOY.md) |

## 📜 更新日志

详见 [CHANGELOG.md](CHANGELOG.md)。当前版本 **v2.5.0**（2026-10-06）。

## 📄 许可证

当前版本**还没有正式的开源许可证文件**，仓库里也没有 `LICENSE`。界面底栏的署名是 `v2.5.0  by Mr lin`，
而 `cpp/src/res/app.rc` 的 `LegalCopyright` 是 `Copyright © SuperClip`（是否补署名待项目所有者裁决）。
采用哪种许可证（MIT / GPL / 仅闭源分发）要由所有者确定后再补 `LICENSE`，并把 `LegalCopyright` 改成同一措辞。
在此之前：**代码公开可读，但没有授予任何使用、修改、再分发的权利**。

---

## English

**What is this** — SuperClip is a local clipboard-history manager for Windows. The system clipboard only
remembers the last thing you copied, so copying several items in a row and then pasting them in different
places means re-copying each one. SuperClip keeps the most recent 500 entries (favorites never expire),
so you can **copy many things in a row and then paste each one on demand** — press `Ctrl` + `` ` `` anywhere,
pick an entry, and it pastes back at your cursor. In quick mode it also does *form relay*: hold `Alt` and
click an input field to paste the top row into it, row after row.

**Tech stack** — C++20, plain Win32 message loop, everything drawn with Direct2D/DirectWrite.
Hand-rolled ~180-line JSON reader, BCrypt for SHA-256, statically linked CRT.
No .NET, no Qt, no runtime prerequisites, **no network code at all** — the binary imports exactly 10 system
DLLs and none of them is a networking library.

**Quick start** — download `SuperClip_v2.5.0_portable.zip` from
[Releases](https://github.com/mylxnet/SuperClip-C/releases), unzip, run `SuperClip.exe`, press `Ctrl` + `` ` ``.
Verified on Windows 10/11 x64; Windows 7 SP1 – 8.1 is the *designed* floor but has **never been run on real
hardware**. The attached exe is a **mingw-w64 cross-build** (the dev machine has no Visual Studio or Windows
SDK), about 3.47 MB. Optionally run `installer\install.bat` as administrator for Start Menu / Desktop
shortcuts. Uninstalling never deletes your history.

**Data & privacy** — everything lives in `%APPDATA%\SuperClip\` (`history.json`, `error.log`).
No uploads, no telemetry, no registry writes, no autostart. Settings are not persisted: every run starts
from defaults (all-items view, normal paste mode, topmost on, right-edge dock, no bound target), and a
leftover `settings.json` is never read. Pasting **does** overwrite the system clipboard (that is how `Ctrl+V`
is delivered). Your clipboard may contain secrets, so treat that folder like a password file.

**Build from source** — `cd cpp && build.bat` (VS2022) or `bash build-tests.sh` for the mingw cross-build;
unit tests run on Windows: 40 cases / 219 assertions / 0 failures.

**License** — no license file has been chosen yet. The app's status bar signs "v2.5.0  by Mr lin", while the
version resource in `app.rc` reads "Copyright © SuperClip"; both the terms and that wording are the project
owner's call. Until a `LICENSE` lands, the source is publicly readable but **no rights are granted**.
