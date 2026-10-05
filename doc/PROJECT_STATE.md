# 项目状态 / 交接文档（PROJECT_STATE）

> 用途：接手这个项目时**先读这份**，它记的是"代码里看不出来"的东西——为什么这么做、哪里踩过坑、哪些结论还没验证。
> 权威分工：需求契约 = `doc/SuperClip_设计规范.html`（v1.2）；行为与算法 = `doc/DESIGN.md`；实现细节 = `doc/PROJECT.md`；
> 逐版本历史 = `CHANGELOG.md`；测试与验证边界 = `doc/TESTING.md`；部署与分发 = `doc/DEPLOY.md`。

## 1. 当前状态

| 项 | 值 |
|---|---|
| 版本 | **v2.1.1**（`app.rc` `FILEVERSION 2,1,1,0` = `Config.h::kVersionText` = 状态栏显示，三处同号） |
| 里程碑 | M1 步骤 1–11 全部落地并实机走查；步骤 12 的 **A 段**（发布与安装脚本、依赖旁证、验收操作单）完成，**B 段（MSVC 真实出包 + 干净 VM）未做**；v2.1.0 的**五项界面修订 + 快速模式选中位钉第一行**已于 2026-10-05 实机走查通过（判据与证据见 `doc/TESTING.md` §2 末三行）；v2.1.1 修掉「启动即置顶」的实现缺口（§4 坑 #12） |
| 单测 | 41 例 / 222 断言 / 0 失败（交叉构建产物在 Windows 实跑，2026-10-05 17:1x） |
| 交叉构建产物 | `SuperClip.exe` 3,649,181 B / MD5 `ac7538b35e7fe311a1eb13544d1ac881` —— **非发布产物**，只用于跑单测与 UI 走查 |
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
| 10 | 走查把真实历史回滚到快照，且**第一轮把病因判断错了**（误记为"强杀丢了未落盘的内存条目"） | ① 程序其实是**变更即落盘**（`Store.cpp` 六处 `storage_.Save`），我按 `SaveToDisk` 的唯一调用点就下了"只在退出链写盘"的结论，属于只看一处调用点就推断全局；② 真正的风险是反方向的：**还原文件时若实例还活着，它下一次变更就会用内存态把还原盖掉**；③ 收起态主窗 `CloseMainWindow()` 返回 false，脚本兜底走了 `Stop-Process -Force` | 走查规程第 0 条改为"先退出再动文件"（理由已订正），并保留 `ShowWindow(SW_SHOW)` + `CloseMainWindow()`/`PostMessageW(WM_CLOSE)` 的正道；本轮实测：快照 135 条与还原后逐 `Id` 比对 0 缺失 0 多余，唯一差异是还原之后新增的 6 处收藏翻转（同步写盘的正常行为） | 结论：强杀**不**丢历史；真正的暴露窗是"快照→还原"之间那约 2 分钟（14:39 建快照 → 14:41:41 重启读回，期间跑的是合成数据实例），那段时间内的真实复制会丢，且日志不记复制事件、事后无法证明。教训：**推断持久化/生命周期语义必须把全部调用点列完**，不能靠单个函数的调用次数 |
| 11 | v2.1.0 首构建 3 处编译错误：`ID2D1RenderTarget` 没有 `CreateTextLayout`、没有 `CreateBitmapFromHICON`、`std::min(long, int)` 无匹配 | mingw 的 `d2d1.h` 比 MS 的少几个方法（真机 vtable 里在，头没同步）；`RECT` 成员是 `LONG`，和 `int` 混进同一个模板函数就不匹配 | 量文字改走 `IDWriteFactory::CreateTextLayout`（排版与 DPI 无关，工厂建出的 layout 可喂任何目标）；图标改 `GetIconInfo`+`GetDIBits` 取预乘 BGRA 再 `CreateBitmap`，**不引 `windowscodecs`**，导入表与 v2.0.3 逐字一致；`RECT` 参与算术前先显式 `static_cast<int>` | Win7 基线砍 API（见 #8）之外还有一条：**mingw 头 ≠ MSVC 头**，交叉构建通过不代表 RC.exe 那套也这么写，反之亦然；遇到"某方法不存在"先翻 `/usr/x86_64-w64-mingw32/include/` 再决定绕法 |
| 12 | **C13「启动即置顶」不生效**：`settings.json` 里 `Topmost=true`，冷启动后 `GWL_EXSTYLE` 的 `WS_EX_TOPMOST` 位却是 0，而 `SetWindowPos(HWND_TOPMOST)` **返回 TRUE、`GetLastError()=0`** | 目标窗口**不在前台**时，系统会**静默丢弃**这条 z-order 带变更（前台带限制的同一家族）。实测：`BringWindowToTop` 返回 TRUE 无效果、`SwitchToThisWindow` 无效、由后台进程跨进程调用同样无效；一旦该窗被真实点击激活，同一条调用立刻生效。**重试与延时都救不了**（曾 6/6 次冷启动全 False） | 显示链补两处：① `ShowWindow`+`UpdateWindow` 之后再断言一次 `SetWindowPos`（同进程、刚建窗，多数场景已足够）② `WM_ACTIVATE` 里若 `WA_ACTIVE` 且带没落上，`PostMessageW(WM_APP_RAISE_TOPMOST)` **延后一条消息**再补发——延后是必须的，`PasteTarget.cpp:76` 早就记过"不能在 `WM_ACTIVATE` 里直接改窗口状态，系统处理完这条还会再动一次" | ① **Win32 的返回值 TRUE 不等于生效**，凡是"系统可能不答应"的调用（z-order、前台、剪贴板）都要用**事后断言**而不是信任返回值；② 走查脚本必须以 `GWL_EXSTYLE` 位为判据，不能以 API 返回值为判据；③ 本轮实测：修复后连续两次冷启动（含把前台让给靶窗的那次）`topmost_bit=True` |

## 5. 架构决策的"为什么"（只记代码读不出来的）

- **点选才算绑定、空格才粘贴**：防误触；单击只做选中，避免鼠标一碰就把内容怼进别人窗口。
- **收藏只在【收藏】视图显示、且不参与「清除」**（C10/C12）：用户裁定收藏要永久保存；数组的
  `[收藏区 | 非收藏区]` 分区不变式**原样保留**，改的只是 `RebuildDisplay()` 的过滤条件——
  持久化顺序、`Boundary()` 插入位、`MoveToBack` 沉底、FR-04 末位淘汰都依赖这个不变式。
- **来源标注不上屏、只参与搜索**（C11）：列表要干净，但按进程名搜得到。
- **启动即置顶**（C13；实现上要绕开"非前台时系统丢带"，见 §4 坑 #12）；**监听窗口禁 message-only**（否则丢 `TaskbarCreated` 广播）；**键入模拟用 `SendInput`**。
- **零网络是硬红线**（AC-8）：连"文件对话框"都不引 `comdlg32`；`dwmapi`/`shcore` 走 `LoadLibraryW` 动态加载，不入链。
- **署名可点 = AC-8 的边界，不是豁免**（v2.1.0）：单击 `by Mr lin` 只做 `ShellExecuteW("open", 常量 URL)`，
  本进程零套接字零 HTTP 栈，联网动作发生在系统选定的外部浏览器里——与用户自己双击一个 `.url` 同构。
  这条写在 `doc/DESIGN.md` §1.2 的边界裁决里，含最严解读下应回退为静态署名的说明。**规范 HTML 原文未改**（改动契约原文需单独授权）。
- **自研极简 JSON**：原 ADR 选 RapidJSON，落地时环境离线取不到包，改为约 180 行的对象数组解析器；
  副产品是第三方审计面归零。决策记录在 `src/core/Json.h` 头注释。
- **UI 层不做条件编译打桩**：无头环境跑不了 D2D/剪贴板/托盘，这部分只能实机走查，跑不了就标**未验证**。

## 6. 待办与遗留

| 项 | 状态 | 备注 |
|---|---|---|
| 步骤 12 B：MSVC 出包 + `dumpbin /dependents` + ≤3 MB 体积门 + 干净 VM 的 AC-7/AC-8 | **未做** | 本机无 SDK；操作单在 `cpp/scripts/ReleaseChecklist.md` §3 |
| v2.1.0 五项界面修订的实机走查 | **已通过**（2026-10-05 17:01–17:07，合成数据、全程带命中守卫） | ① 标题栏图标：`doc/images/readme-title.png` 上图左端可见，模式文字右移后仍可点（`MODETEXT_CLICK` → `PasteMode` 0→1→0）② 图钉新形态 + 置顶态下划线：`readme-title.png` 上/下两态实拍，点图钉 `topmost` True→False→True ③ 搜索框 ✕：`readme-search.png`（有字含 ✕）与清空态实拍（占位符「搜索...」、无 ✕）④ 底栏命中守卫：点状态栏空白 `fav_before=2 fav_after=2` 不改收藏；点署名后前台窗口标题变为 `mylxnet/SuperClip-C`（**注意**：进程列表法无效，浏览器新标签复用同一进程）⑤ 气泡位置：`tip=1571,184,1891,238`，在 hovered 行之上、`tip_left - win_left = 51 px ≈ 3 个全角字宽`（`readme-tip.png`）。全程 `GUARD_FAILS=0` |
| v2.1.0 快速模式钉第一行（C14）的实机走查 | **已通过**（靶窗 `SuperClipPasteTarget`；① 取自 16:2x 那轮，②③ 取自 17:04–17:05 这轮） | ① 不点任何条目、直接空格 → 靶窗 EDIT 得 `C14B9893HEADTAIL`（含最新那条、不含上一条）② 连贴第二次 → 靶窗得 `C14A4577HEADTAIL`（粘过的那条已沉底，第一行自然变成下一条）③ 应用侧日志同步给出两行 `[paste] Ctrl+V → SuperClipPasteTarget…按键事件数=4`（17:04:59 / 17:05:05）④ 收藏数全程 2 未变。**未覆盖**：⑤"列表已下滚时钉住行滚进视口"与"绑定完成后直接空格"仍只有 §9.4-16 的逻辑证据，UI 侧未实拍 |
| README 配图 | **已重拍并核对**（v2.1.1） | `doc/images/readme-{main,search,status}.png` 换成 v2.1.1 实拍，新增 `readme-tip.png`（悬浮气泡）与 `readme-title.png`（图钉两态合成）。五张图逐张 `Read` 过：列表内容全部来自 `cpp/qa/mksynthetic.ps1` 的合成条目，无真实剪贴板内容 |
| 150%/200% DPI 无错位复核 | 待用户改缩放 | 步骤 6 遗留；帮助窗 DPI 排版挂同一个包；v2.1.0 新增的图标与 ✕ 也在同一包里复核 |
| N9 连贴按键竞态 | 未闭环 | `WM_WTSSESSION_CHANGE` 注入已实测可取消；`WM_ENDSESSION` 无法注入验证 |
| 靶心红/绿像素复核 + 真实注销场景 | 部分收窄 | 红色（未绑定）已由 2026-10-05 实拍图给出像素证据；**绿色（已绑定）仍待**；真实注销场景仍待。步骤 9 |
| 落盘时机：**变更即落盘**（本节曾误记为"只在退出链落盘"，已订正） | 已核实，无需裁决 | 复查代码：`Store.cpp` 六处 `storage_.Save`（加载规范化 64 / 入列 100 / 收藏 126 / 粘贴标记 147 / 清除 166 / 复位 178），退出链 `AppContext.cpp:267` 只是兜底；写侧是 `.tmp` + `ReplaceFileW` 原子替换。强杀与断电都不丢历史。唯一残留的可选项是 `uninstall.bat` 改"先 `taskkill`（不带 `/f`）再兜底 `/f`"——纯运维洁癖，不是数据安全问题，仍**待裁决** |
| 收藏切换等变更是**同步整文件重写**（n≤500，约几十 KB） | 已知行为，未视为问题 | 若将来出现连击卡顿再评估防抖合并；本轮不改，避免动语义 |
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
