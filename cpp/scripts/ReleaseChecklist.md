# 发布验收清单（步骤 12）

> 状态：**本机（无 MSVC / 无干净 VM）只能出旁证，正式判据全部未验证。**
> §11 步骤 12 的三条判据 —— `dumpbin /dependents` 白名单比对、AC-7 干净 VM、AC-8 断网 100 次 ——
> 都要等真·MSVC 产物和一台干净环境，本文把"已拿到的证据"和"还欠的证据"分开放，不许混着宣称通过。
>
> 用法：**先跑 §2.1 自检出包环境**，再按 §2.2 的十行判据单逐条执行（每行自带命令与结案判据），
> 换到 VM 后按 §3 的 3.1–3.9 走，**边跑边填 §5 的发布记录表单**；体积门若超标看 §6 的三档降级（按 B→A→C 的顺序试）。
> 2026-10-05 本版新增：§2.1/§2.2/§5/§6 四节、驱动脚本 `cpp/qa/ac8_loop.ps1`（守卫已实测、主循环未验证）。

## 1. 已拿到的旁证（2026-10-05，mingw 交叉产物 `cpp/build-mingw/SuperClip.exe`）

导入表实测（`objdump -p`）：

```
DWrite.dll  GDI32.dll  KERNEL32.dll  SHELL32.dll  USER32.dll
WTSAPI32.dll  bcrypt.dll  d2d1.dll  ole32.dll  msvcrt.dll
```

逐条判读：

| 观察 | 判读 |
|---|---|
| 全是系统 DLL，无 `libgcc*/libstdc++/winpthread` | `-static` 生效，免运行库方向成立（但 mingw 静态 CRT 落到 **`msvcrt.dll`** = 系统自带，MSVC `/MT` 产物里这一项根本不会出现，两者不可互相顶替） |
| 无 `ws2_32 / winhttp / wininet / urlmon / cryptnet / winmm / imm32 / oleacc / psapi / dbghelp` | 符合附录 A.3 禁止清单 |
| 禁项符号扫描（`RegSetValueEx*` / `WSAStartup` / `InternetOpen*` / `HttpOpenRequest*` / `URLDownloadToFile*` / `closesocket` / `send` / `BlockInput`）反汇编 grep **0 命中** | 不写注册表（FR-18）、不联网（AC-8）在符号层无反证 |
| `dwmapi` / `shcore` **不在导入表** | 设计如此：`SystemInfo.cpp` 走运行期 `LoadLibraryW + GetProcAddress`（Win7 缺失即回退），本就不该进导入表；A.4 把 `dwmapi` 列进"固定链接库"与实现的动态加载方式不一致，属文档口径问题 |
| **`WTSAPI32.dll` 不在 §1.2 / A.4 白名单里** | 代码必须链它（§6.1 的 `WTSRegisterSessionNotification`，锁屏取消点选靠这条）。**这是两份 C++ 文档白名单的缺漏**，未经你授权我没动文档 |
| `objdump -p` 里那行 `Entry e 0000000000000000 0000000000000000 CLR Runtime Header` | objdump 对**每个** PE 都会打印这行数据目录，RVA 与 size 均为 0 = 空目录项，**不是** CLR/.NET 引用 |
| exe 体积 3 649 181 B ≈ **3.48 MB**（v2.1.2 全量干净重建产物，MD5 `ab5a5ff23e3312f5fcb282c1f2da317c`；v2.0.3 图标内嵌后为 3 637 683 B，图标接入前 3 557 811 B），超 §10.3 的 ≤3 MB 目标 | 这是未开 `/O2 /GL`、未做 MSVC 优化的交叉产物，且 `.rsrc` 里带了 79 KB 的多尺寸图标，**不能拿它宣称达标**，也不能拿它证伪 |
| `app.rc` 在 windres 下可编且资源进了包 | `.rsrc` 段由 `0xac8` 增至 `0x140f8`（图标接入时 +79,872 B）；`Get-Item .VersionInfo` 读到 `FileVersion/ProductVersion=2.1.2.0`、`Company=SuperClip`；`[System.Drawing.Icon]::ExtractAssociatedIcon` 取出 32×32 且读图核对为本图标。**这只证明"资源能被取出"**，RT_MANIFEST 是否被 OS 实际加载、16 px 档在真机托盘/任务栏的观感仍要 2.2.7 + 实机结案 |

> 本节数字随版本更新（2026-10-05 v2.1.2 全量重建重跑）：`objdump -p` 导入表**十项与 v2.0.3 逐字一致**（v2.1.0 新增的标题栏图标走
> `GetIconInfo`+`GetDIBits`+`CreateBitmap`，**没有**引 `windowscodecs`/`gdiplus`），禁项符号扫描仍 0 命中，`.rsrc` 段大小 `0x140f8` 未变。

**结论**：依赖面方向正确、无网络与注册表符号；但这条证据链只覆盖"可编译可链接的 mingw 版"，发布结论必须由下面第 2 节重跑。

## 2. 仍欠的正式判据（必须 MSVC 产物）

### 2.1 出包环境准备（先决条件，一次性配好）

本机（Win11 x64）实测**没有**下列任何东西，所以第 2.2 节一条都执行不了；换机器时先跑这一小节自检：

| # | 要装/要有的东西 | 自检命令 | 通过判据 |
|---|---|---|---|
| 2.1.1 | VS 2022 Community/BuildTools + **「使用 C++ 的桌面开发」工作负载**（含 MSVC v143 x64 工具集、Windows 10/11 SDK） | `"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath` | 打印出安装根目录；非空 |
| 2.1.2 | `vcvars64.bat` 可定位（`build.bat` 就靠它） | `dir /b "%Step1结果%\VC\Auxiliary\Build\vcvars64.bat"` | 文件存在；`build.bat` 不再报 `[fail] vcvars64.bat not found` |
| 2.1.3 | CMake ≥ 3.20（VS 自带的那份即可）+ Ninja 或 MSBuild | `cmake --version` | ≥ 3.20 |
| 2.1.4 | 仓库在 Windows 原生路径下（**别在 `/mnt/e` 上跑 MSVC**，长路径与文件锁会引入无关噪声） | `fsutil behavior query SymlinkEvaluation` 无关；靠肉眼确认盘符 | `cd /d E:\qcode\superclip\cpp` 后 `dir CMakeLists.txt` 命中 |
| 2.1.5 | 全量干净重建 | `cpp\scripts\CleanAndBuild.bat` | 结尾 `[ ok ]`；`build\Release\SuperClip.exe` 存在 |

> 顺序纪律：**2.1.5 必须在 2.2 的每一项之前**，且发布前禁止用增量构建出包（`CleanAndBuild.bat` 存在的唯一理由）。

### 2.2 判据单（每条自带命令与结案判据）

在 VS 开发者命令提示（x64 Native Tools）里执行。`EXE` 代指 `build\Release\SuperClip.exe`。
最后一列写「本机能否预先旁证」——凡标 **只有 MSVC** 的，都不许拿第 1 节的 mingw 证据顶替。

| # | 判据 | 命令 | 通过判据 | 本机能否预先旁证 |
|---|---|---|---|---|
| 2.2.1 | 依赖白名单（顶层） | `dumpbin /dependents %EXE%` | 出现的 DLL **逐条落在** §1.2 / A.4 白名单内；**且**额外只有 `WTSAPI32.dll`（§1 已说明它是白名单缺漏，代码必须链） | 部分（`objdump -p` 已给同形清单，不可顶替） |
| 2.2.2 | 导入表全量留档 | `dumpbin /imports %EXE% > imports.txt` | 与 mingw 侧十项对照无新增系统 DLL；无禁止项；全文贴进 §5 发布记录 | 部分 |
| 2.2.3 | 禁链模块 | `findstr /i "ws2_32 winhttp wininet urlmon cryptnet comdlg32 dbghelp psapi oleacc gdiplus windowscode8 windowscodecs" imports.txt` | **0 命中**（`windowscodecs` 在 mingw 侧已确认未引入，MSVC 产物要重证） | 部分 |
| 2.2.4 | `cl /W4` 零警告 | 看 2.1.5 的完整构建日志（`%EXE%` 重跑前先 `build.bat > build_w4.log 2>&1`） | `warning C` 行数 **0**（第三方源：本项目无第三方源，故不应有任何豁免） | 只有 MSVC |
| 2.2.5 | `/MT` 静态 CRT | `dumpbin /dependents %EXE%` | 清单里**没有** `VCRUNTIME140.dll`/`MSVCP140.dll`/`ucrtbase.dll`；也不该有 mingw 的 `msvcrt.dll` 那行 | 只有 MSVC（mingw 走的是系统 `msvcrt.dll`，方向不可互证） |
| 2.2.6 | `__try/__except` 消息兜底 | 实机：运行中用外部进程向主窗发畸形/越界消息（历史轮里用的 harness 在 `cpp/qa/`），观察进程不崩 | 进程存活 + `error.log` 记到兜底分支 | 只有 MSVC（mingw 下该宏不可用） |
| 2.2.7 | 资源段与 manifest | `dumpbin /resources %EXE%` | 含 `RT_MANIFEST`(1) 与 `RT_ICON`(3)/`RT_GROUP_ICON`(14)/`RT_VERSION`(16)/`RT_RCDATA`；把 `app.rc` 里 manifest 的 `requestedExecutionLevel` 与之一致 | 部分（windres 已把 `.rc` 编进 `.rsrc`，字节级差异只有 MSVC） |
| 2.2.8 | 版本信息三处一致 | `powershell -Command "(Get-Item %EXE%).VersionInfo | Select FileVersion,ProductVersion"` | `2.1.2.0` 与 `Config.h` 的 `kVersionText`、`app.rc` 同值（mingw 侧已过，MSVC 产物需重跑，`PackageRelease.bat` 也有一道硬校验） | 是（同形） |
| 2.2.9 | 体积门 ≤ 3 MB | `dir %EXE%` 取字节数 | ≤ `3 145 728 B`。**当前交叉产物 3 649 181 B（3.48 MB）超标**；若 MSVC `/O2 /GL` 仍超，按 §6 的降级选项裁决（减图标档位 / 剥 `RT_RCDATA` / 把目标改成 3.5 MB 并改 §10.3——后者属改契约，需授权） | 只有 MSVC |
| 2.2.10 | 启动到可交互 < 300 ms | 干净机上双击后立刻 `measure.ps1`（§5 附）或人工掐表 + `error.log` 的「启动完成」时间戳 | 首个可输入时刻 < 300 ms | 只有 MSVC + 真机 |

## 3. 干净 VM 验收操作单（AC-7 / AC-8）

前提：一台 **未装 .NET Core 3.1 / 未装 VC++ 2015-2022 运行库** 的 Windows 10/11 x64 快照机，有网卡但按第 3.4 步禁用出站。
产物用 `release\SuperClip_vX.Y.Z_portable.zip` 解出来的固定名 `SuperClip.exe`。

| # | 操作 | 判据（必须逐条回贴实际输出/截图） |
|---|---|---|
| 3.1 | 装前基线：`systeminfo` 取 OS 版本；`dir "%ProgramFiles%\Microsoft Visual Studio"`；`reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"`（只为确认**没装**运行库，脚本不写注册表） | 无 VC++ 运行库记录；系统 .NET Core 3.1 不存在 |
| 3.2 | 双击 `SuperClip.exe` | 进程起、托盘图标出现、无 "missing dll" 弹窗、无 Windows Installer 修复提示 |
| 3.3 | 按 `Ctrl + 反引号` | 主窗呼出/收起；随便复制 3 段文本 → 列表出现 3 条；双击粘到记事本 |
| 3.4 | 断网：先开日志再下规则<br>`netsh advfirewall set allprofiles firewalllog logfilename=%SystemRoot%\System32\logfiles\firewall\pfirewall.log`<br>`netsh advfirewall set allprofiles firewalllog logdroppedpackets enable`<br>`netsh advfirewall set allprofiles firewalllog logallowed disable`<br>`netsh advfirewall firewall add rule name="sc_out" dir=out action=block program="C:\path\SuperClip.exe" enable=yes` | 四条都有 `Ok.` 回显。**不先开日志，3.5 的"无出站拦截记录"就是空话**——默认配置不记被拦包 |
| 3.5 | 断网状态下连续复制粘贴 100 次：`powershell -NoProfile -ExecutionPolicy Bypass -File cpp\qa\ac8_loop.ps1 -ConfirmVm -Iterations 100`（脚本自带 WinForms 靶窗，不依赖记事本；详见 §4 该行的说明与两道守卫）。跑完**再让机器空闲满 30 分钟**（AC-8 的持续时长口径），期间不重启进程、不解除 3.4 的规则 | 逐轮判据看脚本回显：`iterations=100 matched=100 failed=0` + `VERDICT: pass`；`error.log grew by N bytes` 里 **N 对应的内容不含 E/W 级新记录**；`pfirewall.log` 中 grep 不到该 exe 的 `Blocked` 行（有 = 它试图联网 = AC-8 失败）。CSV 贴进 §5 |
| 3.6 | `tasklist /m /fi "IMAGENAME eq SuperClip.exe"` | 模块列表只含 `C:\Windows\System32\` 下的 DLL，与第 2 节 `dumpbin` 结果一致 |
| 3.7 | 重启 VM（不登录 SuperClip）后再双击 | 同一份 `%APPDATA%\SuperClip\history.json` 被接管，条目/收藏/灰显顺序一致 |
| 3.8 | `installer\install.bat`（管理员）→ 开始菜单/桌面出现快捷方式 → 运行 → `uninstall.bat` | 安装到 `%ProgramFiles%\SuperClip\SuperClip.exe`；卸载只删程序目录与 `.lnk`；**`%APPDATA%\SuperClip` 完好** |
| 3.9 | 卸载后 `reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run"` | 无 SuperClip 项（FR-18 不做自启的正面证据） |

判据纪律：3.1–3.9 任何一条没实跑就在发布记录里标"未验证"，不许用第 1 节的 mingw 旁证顶替。

## 4. 脚本清单与实测状态

| 脚本 | 作用 | 实测状态（2026-10-05） |
|---|---|---|
| `cpp/build.bat` | 探测 `vcvars64.bat` → `cmake -S . -B build` → `--config Release` | 已执行：正确报 `[fail] vcvars64.bat not found`、exit 1（本机确实无 VS2022）。**MSVC 全链未验证** |
| `cpp/scripts/CleanAndBuild.bat` | `rmdir /s /q build\` 后全量重建（发布前必用，防缓存） | 已执行：`build\` 不存在时走 `[info] no build\ directory` → `goto :build` → 调 `build.bat` → exit 1。**删除动作只针对 `build\`，本机没有该目录，未实跑过真实清理** |
| `cpp/scripts/PackageRelease.bat` | CleanAndBuild → 从 `src\res\app.rc` 抓 `FILEVERSION` → 汇集 stage（固定名 exe + README + CHANGELOG + `installer\*.bat`）→ 出 `release\SuperClip_v{Ver}.exe` 与 `release\SuperClip_v{Ver}_portable.zip` → 体积判定 | 已执行：正确停在步骤 1/5（缺工具链），exit 1。步骤 2–5 的四个易错点用临时探针单独验过，见下表；**整链跑通要等 MSVC** |
| `cpp/installer/install.bat` | 管理员校验 → 复制并**还原固定名** `SuperClip.exe` 到 `%ProgramFiles%\SuperClip\` → `WScript.Shell` 建开始菜单与桌面 `.lnk` | 未执行（本 shell 非提权）。不写注册表（FR-18 / A.3），`.lnk` 只是入口不是自启 |
| `cpp\installer\uninstall.bat` | `Y` 确认 → `taskkill` → 删 `%ProgramFiles%\SuperClip` 与两条 `.lnk` | 已执行：正确走 `[fail] ... needs elevation`。**绝不删 `%APPDATA%\SuperClip`**（真实剪贴板历史，删了不可恢复） |
| `cpp/qa/ac8_loop.ps1`（2026-10-05 新增，配合 3.5） | 断网连贴 100 次的驱动：脚本自备 WinForms 靶窗（不依赖记事本/Excel），每轮「写剪贴板 → 按 Ctrl+反引号呼出 → 按空格粘最新」，回读靶窗比对 token，落 CSV，末行给 `VERDICT` | **部分实测**：`Parser` 语法 0 错、文件 0 个非 ASCII 字节；两道守卫在你本机实跑并正确拦停（不带 `-ConfirmVm` → `exit 2`；带 `-ConfirmVm` 但检测到真实历史 135 条 → `exit 3`，拦停后核对 `history.json` 仍是 135 条 / 9 收藏 / md5 `8F9FD08A…` **未变**）。主循环（真发按键、真粘贴）**未验证**——它会把 100 条写进当前实例的历史，只能在快照 VM 上首跑。**它是 QA 资产，不进发布包**，跑 3.5 前单独拷到 VM |

`PackageRelease.bat` 易错点的单独取证（临时探针，跑完即删，未写到用户真实开始菜单/桌面）：

| 探针项 | 实际输出 |
|---|---|
| `findstr /r /c:"^ *FILEVERSION"` + `for /f tokens=1,2,3 delims=,` | `RAW=2,0,2,0` → `VER=2.0.2`（抓不到时硬失败，不猜默认值） |
| `install.bat` 的 exe 定位优先级 | 空目录→`SRC=[]`；只有版本化名→命中 `SuperClip_v9.9.9.exe`；固定名存在→固定名优先 |
| `WScript.Shell` 建 `.lnk` 的那一整行 | `ps-ok`，两个 `.lnk` 生成成功；`ProgramData\...\Programs` 与 Desktop 路径 `Test-Path` 均 True |
| `Compress-Archive` + `%%~zF` 体积门 | zip 2706 B，`if %ZIPSIZE% GTR 3145728` 判定分支正常 |
| 版本号一致性校验（2.0.3 那轮新增，防 agent.md 四.2 的三处不同号） | 同一段 `findstr`+`for /f` 从 `app.rc` 得 `RAW=2,0,3,0` → `VER=2.0.3`；`findstr /c:"v%VER%" src\core\Config.h` → `errorlevel 0`（放行），对照 `findstr /c:"v9.9.9"` → `errorlevel 1`（拒绝出包）。探针为 Temp 脚本，跑完即删 |
| **同一探针在 v2.1.2 那轮重跑，且这次直接抽 `PackageRelease.bat` 第 29–53 行原样执行**（不再手抄逻辑） | 正向：`RAW=2,1,2,0` → `version = v2.1.2` → 走完一致性检查、落到我加的尾标记，`exit 0`（放行）。负向：把 `/c:"v%VER%"` 换成 `/c:"v9.9.9"`，脚本停在 `[fail] src\core\Config.h has no v2.1.2 - it disagrees with app.rc, refusing to package.`，`exit 1`，**到尾标记的那一行没执行**。**踩到的坑**：抽段执行必须先补 `set "RC=src\res\app.rc"`，否则 `%RC%` 为空，`findstr` 会报"找不到路径"并把 errorlevel 搅乱，看起来像闸门失效 |

### 本轮踩到的一个真问题：`.bat` 里不能写中文

第一版脚本按 §一.1「代码注释一律中文」写了中文 `rem`，`cmd.exe` 在 cp936 下把 UTF-8 中文的双字节 lead byte 与**下一个字符**配成一对，
导致后续行的括号/百分号被吞，脚本报出 `'dp0"' 不是内部或外部命令`、`'BuildTools)' …`、`'cmake' …` 这类碎片错误
（`build.bat` 原本就带中文注释，同一缺陷，本轮一并改掉）。
**结论：`.bat`（以及任何被 cmd/PS 解码的脚本）内容一律 ASCII，中文只出现在 `.md` 与 C++ 源文件里。**
这与 §一.1 冲突处已在文件头注释中标明例外理由。

原「仓库根目录还没有 `CHANGELOG.md`」的缺口已于 2026-10-05 补齐（根目录 `CHANGELOG.md`，v2.0.3/v2.0.2 详细、
更早版本指向 `doc/技术方案.md`），`PackageRelease.bat` 的 `[warn]` 分支保留作兜底。真发版仍卡在下面两件事：
MSVC 工具链与干净 VM，也就是第 2、3 节那两张单，**都不是文档问题**。

## 5. 发布记录表单（跑完 2/3 节就地填，别事后回忆）

一次发布一份，命名 `doc/发布记录_vX.Y.Z.md`。**没实跑的格子写"未验证"，空着等于作假**。

```markdown
# SuperClip vX.Y.Z 发布记录

- 构建机：OS / VS 版本 / 工具集版本（vswhere 输出原样贴）
- 源：git commit `____`（`git rev-parse HEAD`），工作区 `git status` 干净
- 构建：`CleanAndBuild.bat` 结尾回显 `____`；完整日志 `build_w4.log`（`warning C` 行数 = ____）

## 2.2 判据单结果
| # | 判据 | 实际输出（原样贴，不许摘要） | 结论 |
|---|---|---|---|
| 2.2.1 | dumpbin /dependents | | |
| 2.2.2 | dumpbin /imports | | |
| 2.2.3 | 禁链模块 grep | | |
| 2.2.4 | /W4 零警告 | | |
| 2.2.5 | /MT 无 VCRUNTIME | | |
| 2.2.6 | 消息兜底不崩 | | |
| 2.2.7 | /resources 四类资源 | | |
| 2.2.8 | VersionInfo | | |
| 2.2.9 | 体积 ____ B ≤ 3 145 728 | | |
| 2.2.10 | 启动可交互 ____ ms | | |

## 3 干净 VM 结果
- 快照机：OS 版本 / 已确认无 .NET Core 3.1 与 VC++ 运行库（3.1 输出）
- 3.2–3.9 逐条：操作 + 实际回显 + 结论
- 3.5 附 `sc-ac8-result.csv` 全文，及 `pfirewall.log` 里 grep 该 exe 的结果（应为空）
- 3.6 `tasklist /m` 原样输出

## 结案
- 通过项：____
- 未验证项：____（必须逐条列，禁止以"其余通过"收口）
- 裁决记录：体积门若超标，按 §6 选项 ____ 执行，由 ____ 于 ____ 批准
```

## 6. 体积门（≤3 MB）若 MSVC 产物仍超标的降级选项

当前交叉产物 3 649 181 B（3.48 MB），超标 492 KB。**先把 MSVC `/O2 /GL` 的真实数字测出来再谈裁剪**，
不要拿 mingw 数字去论证"必须减功能"。若仍超，按代价从小到大三档，选一档需你裁决：

| 档 | 做法 | 预计收益 | 代价 |
|---|---|---|---|
| A | 图标只保留 16/32/48 三档（现多一档 256）；`RT_RCDATA` 里非必要资源剥离 | 约几十 KB | 高 DPI 托盘观感变差（本来就记着"16 px 档偏糊"这条已知限制） |
| B | 链接期 `/OPT:REF /OPT:ICF`、`/GL` + LTCG 关断调试信息；确认 CMake Release 用的是 `/MT` 而非 `/MDd` | 可达数百 KB，**很可能直接达标** | 无功能代价；纯构建配置，先做这档。`CMakeLists.txt:15` 已设 `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`（`/MT`），mingw 侧的 `-static` 也已验（导入表里的 `msvcrt.dll` 是系统自带，MSVC `/MT` 产物根本不该出现 CRT 独立 DLL）；这一档真正能省的是优化串与调试信息，**别把调试档带进发布** |
| C | 改 §10.3 目标值为 3.5 MB | 0 | **属改契约原文**，需单独授权，且要在 CHANGELOG 记为规格变更而非达成 |

推荐顺序：**B → 复测 → 不够再 A → 仍不够才提 C**。B 档完全不动代码与语义，风险最低。
