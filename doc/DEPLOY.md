# SuperClip（C++ 版）部署与运维

> 对应产品 **v2.0.3**。本文只写"已经在代码或实机上证实过"的行为；凡未跑过的判据一律标 **未验证**，
> 并与已验证项分开列。步骤 12 的正式验收证据链在 `cpp/scripts/ReleaseChecklist.md`，本文不重复。

## 1. 两种交付形态

| 形态 | 怎么得到 | 权限 | 说明 |
|---|---|---|---|
| 便携版 | 直接拿 `SuperClip.exe`（或 `release\SuperClip_vX.Y.Z.exe`）双击 | 标准用户即可 | 不写注册表、不留服务；唯一落盘位置是 `%APPDATA%\SuperClip\` |
| 安装版 | 以管理员运行 `cpp\installer\install.bat` | 需要提权（写 `%ProgramFiles%`） | 复制到 `%ProgramFiles%\SuperClip\SuperClip.exe` + 两条 `.lnk` 快捷方式 |

**两种形态都注册表零写入**：FR-18 规定不做开机自启，`install.bat` 与卸载脚本里没有任何 `reg` 调用，
`reg` 符号在交叉产物的反汇编扫描里也是 0 命中（`ReleaseChecklist.md` §1）。`.lnk` 只是入口，不是自启项。

## 2. 发布产物构成

`cpp\scripts\PackageRelease.bat` 从 `cpp\build\Release\SuperClip.exe` 起，产出：

```
release\SuperClip_v{Ver}.exe            单文件便携版（版本化改名，只用于分发）
release\SuperClip_v{Ver}_portable.zip   stage 内容打包：
    SuperClip.exe          ← 固定名，运行与安装都用它
    README.md
    CHANGELOG.md
    installer\install.bat
    installer\uninstall.bat
release\stage\              未打包的同一份载荷
```

版本号单一来源是 `src\res\app.rc` 的 `FILEVERSION`（脚本用 `findstr` 抓，抓不到就硬失败、不猜默认值），
前三段拼成包名；脚本随后校验 `src\core\Config.h` 里的 `kVersionText` 含同一个 `vX.Y.Z`，不一致**拒绝出包**
（agent.md 四.2「三处一致」的机械守卫，两个分支都用探针实测过：`v2.0.3` 放行、`v9.9.9` 拒绝）。

红线：**安装后与运行中的二进制必须叫 `SuperClip.exe`**。进程名、单实例的路径比较、托盘、卸载脚本都依赖这个名字，
版本化那份只是分发命名。

发布记录还必须附两条审计输出（`ReleaseChecklist.md` §2，本机无 MSVC → **未验证**）：

```
dumpbin /dependents build\Release\SuperClip.exe
dumpbin /imports    build\Release\SuperClip.exe
```

## 3. 安装（`installer\install.bat`）

1. 把 zip 解压到任意目录，保持 `package\SuperClip.exe` + `package\installer\install.bat` 的层级。
2. 右键 `install.bat` →「以管理员身份运行」。脚本第一行就是 `net session` 提权检查，非提权直接报
   `[fail] writing to %ProgramFiles% needs elevation` 并 exit 1。
3. 载荷定位优先级：同级 `SuperClip.exe` → 上级 `SuperClip.exe` → 上级 `SuperClip_v*.exe`；
   都没有则报错并打印期望布局。找到版本化名时，落到盘上仍写成固定名 `SuperClip.exe`。
4. 成功回显（可核对）：
   - 程序文件：`%ProgramFiles%\SuperClip\SuperClip.exe`
   - 快捷方式：`%ProgramData%\Microsoft\Windows\Start Menu\Programs\SuperClip.lnk`、桌面 `SuperClip.lnk`
     （桌面路径取 `[Environment]::GetFolderPath('Desktop')`，因此 OneDrive 重定向桌面也正确）
   - 数据目录：`%APPDATA%\SuperClip`

`.lnk` 通过 `WScript.Shell` COM 创建，整行写在一条 PowerShell 命令里——cmd 的 `^` 续行在含内嵌双引号的命令上不可靠，这是脚本注释里明确的坑。

> 脚本本机**未实跑**（当前 shell 非提权）；`.lnk` 创建那一行、载荷定位优先级、`Compress-Archive` 与体积门
> 都用临时探针单独验过，见 `ReleaseChecklist.md` §4 的探针表。整体安装链 **未验证**。

## 4. 卸载（`installer\uninstall.bat`）

提权检查同上 → 打印「将删除 / 将保留」清单 → 必须手动输入 `Y` 才继续（其他输入 = 中止，且中止时不动任何东西）。
删除范围：`taskkill /im SuperClip.exe /f` → `%ProgramFiles%\SuperClip` 整个目录 → 两条 `.lnk`。

**`%APPDATA%\SuperClip` 永远不删**。那里是真实剪贴板历史，删掉不可恢复；重装会自动接管同一份文件。
目录删不掉时（文件被占用）报 `[fail] ... still exists` 并 exit 1，不做二次强删。

## 5. 运行期位置与状态

| 项 | 值 | 来源 |
|---|---|---|
| 单实例 | 命名互斥体 `Local\SuperClip_SingleInstance_9F3A2B1C`；已存在则唤起老实例并退出（返回 0） | `Config.h:80`、`main.cpp:87` |
| 为什么是 `Local\` | `Global\` 需要 `SeCreateGlobalPrivilege`，标准用户创建会失败 → 语义定为**会话级**单实例 | `Config.h:79` 注释（T1） |
| 全局热键 | `Ctrl` + `` ` ``（`MOD_CONTROL` + `VK_OEM_3`），失败只记 warn：「Ctrl+` 热键被占用，呼出不可用（可点击托盘）」 | `MainWindow.cpp:587` |
| 托盘 | 双击＝呼出/收起；右键＝「打开 SuperClip / 退出」；收到 `TaskbarCreated` 广播自动重挂 | `TrayService.cpp` |
| 隐藏宿主窗口 | `WS_POPUP` + `WS_EX_TOOLWINDOW`，0×0 @(-32000,-32000)；**刻意不用 message-only**，否则收不到 `TaskbarCreated` | `HiddenWindow.cpp:38` |
| 数据目录 | `%APPDATA%\SuperClip\`（`FOLDERID_RoamingAppData`），不存在则创建 | `AppDirs.cpp` |
| 条目 | `history.json`（上限 500 条，收藏不参与末位淘汰） | `Config.h` |
| 设置 | `settings.json`（位置/尺寸 DIP、置顶、粘贴模式、复制模式、筛选、绑定进程名） | 步骤 10 实机结论见 `doc/PROJECT.md` §11 |
| 日志 | `error.log`：追加写；超过 1 MB 保留尾部 512 KB 重写；轮转任一步失败就放弃轮转（宁可超长也不写坏） | `Log.cpp:60`、`Config.h:20` |
| 崩溃兜底 | `SetUnhandledExceptionFilter` + `set_terminate` → 记 `error.log`；会话关闭时**不弹窗**（弹窗会卡注销） | `main.cpp:34` |
| **历史落盘时机** | **变更即落盘**：入列、去重迁移、收藏切换、粘贴标记（沉底）、清除、复位、加载后的规范化顺序——每一处都同步整文件重写；退出链的 `SaveToDisk()` 只是最后一次兜底写。写盘走 `history.json.tmp` + `ReplaceFileW`（失败降级 `MoveFileExW`）的原子替换，强杀最坏只丢"那一次没写完的替换"，不会留半截文件 | `Store.cpp:64/100/126/147/166/178`、`StorageService.cpp:140-166`、兜底 `AppContext.cpp:267` |

运维含义（订正：此前本节误写为"只在退出链落盘"，据代码与实机复查已改）：

1. **动 `history.json` 前必须先让进程退出**——不是怕丢数据，而是因为**活着的实例会在下一次变更时用它的内存态把你刚还原的文件盖回去**。先退出，再改文件，最后重启。
2. `uninstall.bat` 里的 `taskkill /im SuperClip.exe /f` 在卸载场景下不会丢历史：磁盘上已是最新态。是否改成"先礼后兵"（先 `taskkill`（不带 `/f`）等退出链走完、再兜底 `/f`）**待裁决**，属运维洁癖而非数据安全问题，本轮未动代码。
3. 复制到 `%APPDATA%` 之外的备份/演练文件，取的就是当下最新态；无需先"触发一次落盘"。
4. 复制事件本身不写日志（`error.log` 只记粘贴、异常与淘汰），所以事后无法从日志判断某个时间窗内是否发生过复制。

## 6. 从 .NET v2.0.2 接管数据

字段名与形态与 .NET 版一致，直接换二进制即可，不需要迁移工具。顺序：

1. 退出运行中的旧版（托盘「退出」）。
2. 备份 `%APPDATA%\SuperClip\history.json` 与 `settings.json`（复制到其他目录，别原地改名）。
3. 放回/保持同名文件，启动 C++ 版 `SuperClip.exe`。
4. 核对列表条目数与顺序一致、收藏标记仍在。
5. 通过后删除备份。

互读验证做过一次（`doc/PROJECT.md` §11 步骤 3 判据：C++ 写出的 `history.json` 能被 .NET 版读回）。
2026-10-05 另有一条旁证：**用户真实生产数据（135 条）被本版完整读回**（重启日志 `启动完成，条目 135 条`，
且 `按进程名恢复绑定` 在真实 `settings.json` 上生效）——但那 135 条是本 C++ 版自己写的，属**跨版本连续性**证据，
**不等于** ".NET 写的文件被 C++ 读"。后者仍只有合成数据覆盖，真实 .NET 生产文件的整机接管演练 **未验证**。

## 7. 故障排查

| 现象 | 先查 | 处置 |
|---|---|---|
| `Ctrl+`` 无反应 | `error.log` 是否有「热键被占用」 | 用托盘双击呼出；关闭占用该组合键的程序后重启 SuperClip。热键不可用不影响其他功能 |
| 托盘图标消失 | Explorer 是否重启过 | 正常会自动重挂（`TaskbarCreated`）；仍缺失时看日志有无「NIM_ADD 失败」——那时托盘整体不可用，热键与窗口不受影响 |
| 列表永远空 / 退出后无历史 | `%APPDATA%\SuperClip` 是否可写、是否有同名**普通文件**占位 | 数据目录拿不到时按设计降级为「仅内存态」且日志不落盘；删掉占位文件重启即可恢复 |
| 窗口跑到屏幕外（多显示器拔掉后） | `settings.json` 的 `Left/Top` | 越界值（实测 `Left9000/Top7000`）会回默认停靠；手改文件后重启可强制归位 |
| 高 DPI 下位置/尺寸不对 | 坐标以 DIP 存储，读侧按当前 DPI 换算 | 150%/200% 排版复核仍是遗留项（`doc/PROJECT.md` §11 步骤 6 未闭环），先固定缩放验证 |
| 装不上 | 是否提权 | `install.bat`/`uninstall.bat` 都要求管理员；便携版不需要 |
| 怀疑被防火墙拦 | 出站规则 | 按 AC-8 的验收做法：给该 exe 加 `dir=out action=block` 规则再连续复制粘贴 100 次，日志与防火墙记录都应无异常（`ReleaseChecklist.md` §3.4–3.5） |

## 8. 回滚

便携版：结束进程、删 exe、数据目录留着不管即可。
安装版：`uninstall.bat`（输入 `Y`）→ 重新双击便携版。
两条路径都保留 `%APPDATA%\SuperClip`，所以回滚不丢历史；需要彻底清空时才手动删该目录，且删前另存一份。

## 9. 环境要求与已确认的兼容边界

- Windows 7 SP1 x64 起（`_WIN32_WINNT = 0x0601` 基线；因此代码里禁用了 `IDWriteTextFormat::Clone`、`SetLineSpacing`、`GetDpiForWindow` 等 newer API，见 `doc/PROJECT.md` §12）。
- 系统自带 DLL 即可运行：D2D/DWrite/UMS 都在箱底；静态 CRT。mingw 交叉产物实测导入表为
  `d2d1 / dwrite / ole32 / bcrypt / kernel32 / user32 / gdi32 / shell32 / wtsapi32 / msvcrt`，无 `libgcc/libstdc++/winpthread`。
- 目标机**无需** .NET、无需 VC++ 运行库；这条结论目前只有导入表旁证，AC-7 的干净 VM 实跑 **未验证**。
- 依赖白名单缺 `WTSAPI32.dll`（锁屏取消点选必须链它），属文档口径问题，已在 `ReleaseChecklist.md` §1 登记。

## 10. 未验证清单（发布前必须结案）

| # | 项 | 阻塞原因 |
|---|---|---|
| 1 | MSVC `build.bat` / `CleanAndBuild.bat` 全链 | 本机无 VS2022，只能证明「正确报错并 exit 1」 |
| 2 | `dumpbin /dependents`、`/imports`、`/resources` | 无 dumpbin |
| 3 | exe ≤ 3 MB、启动 < 300 ms | 当前交叉产物 3.64 MB（未优化、含图标），不能顶替 |
| 4 | `cl /W4` 零警告、`__try/__except` 兜底路径 | mingw 下该宏不可用 |
| 5 | AC-7 无运行库 / AC-8 断网 100 次（`ReleaseChecklist.md` §3 的 3.1–3.9） | 无干净 VM |
| 6 | `install.bat` 提权整链、`uninstall.bat` 真实删除 | 当前 shell 非提权 |
| 7 | RT_MANIFEST 是否被 OS 实际加载；图标在真机托盘/任务栏的观感 | 需实机走查 + `dumpbin /resources`（二进制层已证 `.rsrc` 内嵌、图标可被 `ExtractAssociatedIcon` 取出；主窗整窗图已给出**红色靶心**与**底栏 `v2.0.3  by Mr lin`** 的像素级证据，见整改清单 §6.6） |
