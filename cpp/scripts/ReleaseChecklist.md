# 发布验收清单（步骤 12）

> 状态：**本机（无 MSVC / 无干净 VM）只能出旁证，正式判据全部未验证。**
> §11 步骤 12 的三条判据 —— `dumpbin /dependents` 白名单比对、AC-7 干净 VM、AC-8 断网 100 次 ——
> 都要等真·MSVC 产物和一台干净环境，本文把"已拿到的证据"和"还欠的证据"分开放，不许混着宣称通过。

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
| exe 体积 3 507 020 B ≈ **3.34 MB**，超 §10.3 的 ≤3 MB 目标 | 这是未内嵌图标/manifest、未开 `/O2 /GL`、未做 MSVC 优化的交叉产物，**不能拿它宣称达标**，也不能拿它证伪 |

**结论**：依赖面方向正确、无网络与注册表符号；但这条证据链只覆盖"可编译可链接的 mingw 版"，发布结论必须由下面第 2 节重跑。

## 2. 仍欠的正式判据（必须 MSVC 产物）

- [ ] `dumpbin /dependents build\Release\SuperClip.exe` → 与 §1.2 / A.4 白名单逐项核对
- [ ] `dumpbin /imports build\Release\SuperClip.exe` → 同上，输出贴进发布记录（A.4 要求的结案证据）
- [ ] `cl /W4` 零警告（mingw 侧只开了 `-Wall -Wextra`）
- [ ] `__try/__except` 消息兜底路径实测（mingw 下该宏不可用）
- [ ] `app.rc` 的 manifest + 图标内嵌生效。交叉构建（CMake + windres）已把 `.rc` 编进 `.rsrc`，
      `Get-Item SuperClip.exe | % VersionInfo` 能读到 `FileVersion=2.0.2.0`；但图标仍未解（`app.rc:6` 注释态），
      RT_MANIFEST 是否被 OS 实际加载要在 MSVC 产物上 `dumpbin /resources` 结案
- [ ] exe 体积 ≤ 3 MB、启动到可交互 < 300 ms 的实测值
- [ ] AC-7 / AC-8（第 3 节操作单）

## 3. 干净 VM 验收操作单（AC-7 / AC-8）

前提：一台 **未装 .NET Core 3.1 / 未装 VC++ 2015-2022 运行库** 的 Windows 10/11 x64 快照机，有网卡但按第 3.4 步禁用出站。
产物用 `release\SuperClip_vX.Y.Z_portable.zip` 解出来的固定名 `SuperClip.exe`。

| # | 操作 | 判据（必须逐条回贴实际输出/截图） |
|---|---|---|
| 3.1 | 装前基线：`systeminfo` 取 OS 版本；`dir "%ProgramFiles%\Microsoft Visual Studio"`；`reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"`（只为确认**没装**运行库，脚本不写注册表） | 无 VC++ 运行库记录；系统 .NET Core 3.1 不存在 |
| 3.2 | 双击 `SuperClip.exe` | 进程起、托盘图标出现、无 "missing dll" 弹窗、无 Windows Installer 修复提示 |
| 3.3 | 按 `Ctrl + 反引号` | 主窗呼出/收起；随便复制 3 段文本 → 列表出现 3 条；双击粘到记事本 |
| 3.4 | 断网：`netsh advfirewall firewall add rule name="sc_out" dir=out action=block program="C:\path\SuperClip.exe" enable=yes` | 规则添加成功回显 |
| 3.5 | 断网状态下连续复制粘贴 100 次（可用记事本+脚本轮发），全程 30 分钟 | 功能无变化；`%APPDATA%\SuperClip\error.log` 无新增错误；防火墙日志无该 exe 的**出站拦截**记录（拦截=它试图联网=AC-8 失败） |
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

`PackageRelease.bat` 易错点的单独取证（临时探针，跑完即删，未写到用户真实开始菜单/桌面）：

| 探针项 | 实际输出 |
|---|---|
| `findstr /r /c:"^ *FILEVERSION"` + `for /f tokens=1,2,3 delims=,` | `RAW=2,0,2,0` → `VER=2.0.2`（抓不到时硬失败，不猜默认值） |
| `install.bat` 的 exe 定位优先级 | 空目录→`SRC=[]`；只有版本化名→命中 `SuperClip_v9.9.9.exe`；固定名存在→固定名优先 |
| `WScript.Shell` 建 `.lnk` 的那一整行 | `ps-ok`，两个 `.lnk` 生成成功；`ProgramData\...\Programs` 与 Desktop 路径 `Test-Path` 均 True |
| `Compress-Archive` + `%%~zF` 体积门 | zip 2706 B，`if %ZIPSIZE% GTR 3145728` 判定分支正常 |

### 本轮踩到的一个真问题：`.bat` 里不能写中文

第一版脚本按 §一.1「代码注释一律中文」写了中文 `rem`，`cmd.exe` 在 cp936 下把 UTF-8 中文的双字节 lead byte 与**下一个字符**配成一对，
导致后续行的括号/百分号被吞，脚本报出 `'dp0"' 不是内部或外部命令`、`'BuildTools)' …`、`'cmake' …` 这类碎片错误
（`build.bat` 原本就带中文注释，同一缺陷，本轮一并改掉）。
**结论：`.bat`（以及任何被 cmd/PS 解码的脚本）内容一律 ASCII，中文只出现在 `.md` 与 C++ 源文件里。**
这与 §一.1 冲突处已在文件头注释中标明例外理由。

已知缺口（不影响脚本可用性，但影响"能不能真发版"）：仓库根目录**还没有 `CHANGELOG.md`**，`PackageRelease.bat` 对此只给 `[warn]` 并继续打包 —— 真要发版前必须先补上，那属于你往后推的文档整改。
