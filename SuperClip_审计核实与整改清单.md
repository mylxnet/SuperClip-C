# SuperClip 审计报告核实与整改清单

> 核实对象：`SuperClip_审计报告.md`（2026-10-05，第三方只读审计，非本人撰写）
> 本文作者：本仓实施方（被审计方）
> 用途：把外部审计逐条对码核实后的**事实基线 + 整改清单**固化，供 §11 步骤 12 B 段一并执行
> 状态：**第 1、2 档（部分）与 `Sha256` 注释对齐已于 2026-10-05 落地并实机走查；第 3 档 #6（交叉构建走 CMake）同日落地**。其余档位待授权/待环境。执行记录见 §6，走查记录见 §6.1，CMake 单一清单见 §6.2。

---

## 0. 核实口径

- 每条断言都回到 `file:line` 对码；能本地复现的不做推断（P0 已跑实测，见 §1）。
- 本机无 MSVC（`cl` / `dumpbin` / VS2022 / Windows Kits 均不存在），因此凡"MSVC 才暴露"的结论一律标 **未验证**，只给旁证。
- 实机运行、`dumpbin` 依赖核对、干净 VM 验收仍属步骤 12 B 段，本文不宣称其通过。

---

## 1. P0 已实测复现：MSVC 主构建输入集不完整

### 1.1 结论

审计报告 Q1（"CMake 主目标缺 `wtsapi32` → MSVC 构建 LNK2019"）**成立，且实际情况更大**：
`cpp/CMakeLists.txt` 的源清单与库清单**双双落后于** `cpp/build-tests.sh`，缺 **4 个源文件 + 1 个链接库**。
交叉构建路径把两项都补齐了，所以本地一直是绿的 —— 这正是缺陷被掩盖的原因。

| 缺项 | 位置 | 影响目标 |
|---|---|---|
| `src/core/Settings.cpp` | `CMakeLists.txt:18-28`（`SC_COMMON_SOURCES`） | `SuperClip` **与** `sc_tests` |
| `src/services/PasteService.cpp` | `CMakeLists.txt:30-44`（`SC_APP_SOURCES`） | `SuperClip` |
| `src/services/ProcessPicker.cpp` | 同上 | `SuperClip` |
| `src/ui/HelpWindow.cpp` | 同上 | `SuperClip` |
| `wtsapi32` | `CMakeLists.txt:72-74`（`target_link_libraries`） | `SuperClip`（`MainWindow.cpp:6` 引入、`:206` 调 `WTSRegisterSessionNotification`） |

对照：`build-tests.sh:29-37` 的交叉链接行**含**这 4 个源与 `-lwtsapi32`，两份清单不一致。（这份手抄清单已在同日删除，见 §6.2。）

### 1.2 实测证据（2026-10-05，WSL `lxsyzd` / `x86_64-w64-mingw32-g++`）

因 WSL 内无 `cmake`，改为**按 CMakeLists 列出的输入手工喂给链接器**（源集与库集逐字照抄，只换工具链）：

| 步骤 | 输入 | 结果 |
|---|---|---|
| A | CMake 的 21 个源逐个编译 | 21/21 通过 → 缺项不在编译期 |
| B | A 的 21 个目标文件 + CMake 声明的 11 个库 | **链接失败，22 处 undefined reference，涉及 18 个符号**：`WTSRegisterSessionNotification`、`sc::HelpWindow::{Create,Destroy,Show}`、`sc::ProcessPicker::{Start,Cancel,OnPickMessage,OnSessionLock,OnEndSession,OnTimeout,~}`、`sc::PasteService::{Start,Cancel,OnTimerId}`、`sc::LoadSettings`/`sc::SaveSettings`、`sc::ProcessNameOf`、`sc::FindWindowByProcess`（引用来自 `AppContext.cpp.o` 15 处、`MainWindow.cpp.o` 7 处） |
| C | B + `build-tests.sh` 多出的那 4 个源 | undefined 从 22 降到 **1**，只剩 `WTSRegisterSessionNotification` |
| D | C + `-lwtsapi32` | **链接通过**，产出 exe |
| E | `sc_tests`：CMake 的 9 个 `SC_COMMON_SOURCES` + `tests/test_main.cpp` 按 `CMakeLists.txt:95` 的库链接 | **失败，12 处 undefined**（`LoadSettings`/`SaveSettings`）；补 `Settings.cpp` 后通过 |

**证据边界（务必如实转述）**：这证明的是"该输入集在任何链接器下都必断"，属于**同一根因、不同表现**——MSVC 会报 `LNK2019` 而非 `undefined reference`，但**无法据 mingw 通过就宣称 MSVC 出包可过**（还有 `RC.exe` 与 `windres` 对 `app.rc` 相对资源路径解析不一致这一处已知差异，交叉构建因此不含 `app.rc`）。MSVC 实测仍属步骤 12 B。
> **同日更新（2026-10-05，见 §6.2）**：WSL 装上 `cmake` 3.22.1 后 `cmake --build` 已真实跑通，交叉构建也不再排除 `app.rc`——windres 编得动，`.rsrc` 里的 VERSIONINFO 能被 Windows 读到。上面这段"边界"现在只剩 **MSVC/`RC.exe` 的差异**与 `dumpbin` 结案两项。

---

## 2. 逐条核实结果

### 2.1 成立（可直接照做）

| 编号 | 断言 | 对码证据 |
|---|---|---|
| Q1/NEW-1 | CMake 主构建断链 | 见 §1（已实测） |
| Q2/D2 | 帮助窗教了一个不存在的操作 | `HelpWindow.cpp:30`「快速模式：先用 ↑ ↓ 选中，再按空格粘贴」↔ ADR T6（设计方案 §14）明确不做方向键导航；快速模式选中**只能靠单击** |
| S5 | 注释与行为不符 | `Sha256.cpp:38` 注释称"调用方据此拒绝入列"，但 `ClipItem.cpp:18` 只算不判、`Store.cpp:85,90` 的 `AddFromClipboard` 无空 hash 检查 |
| R1 | 无应用图标 | `app.rc:6` `// IDI_APP ICON "SuperClip.ico"` 被注释；`TrayService.cpp:25` 回退 `IDI_APPLICATION` |
| R2 | 界面缺署名 | 全仓检索 `Mr lin` 零命中；`app.rc:29` = `Copyright © SuperClip`（非要求文案） |
| Q3 | UI 线程唯一同步 `Sleep` | `PasteService.cpp:50` 写剪贴板重试 `Sleep(kReadRetryMs)` |
| Q4 | `Boundary()` 全表扫描 | `Store.cpp:17-21` `std::count_if` |
| E3 | 无 `CHANGELOG.md` | 根目录不存在（agent.md 硬性要求） |
| E2 | 无 CI | 无 `.github/` |
| D1 | 文档仍写 RapidJSON | 设计方案 `:46,54,394,452,545` + 技术方案 `:661`（§10.1 CMake 要点）↔ `Json.h:10` 已记"改自研极简 JSON" |

### 2.2 部分成立（表述需收窄）

| 编号 | 核实 |
|---|---|
| D6 测试口径 | 技术方案 `:84`、`:575` 已把"40 例 / 无 Catch2 / 自建断言器"写成正文与落地实况；残留矛盾只有 §10.1 `:661` 一句"第三方仅 RapidJSON 与 Catch2"。收口点是删这一句，不是重写 §9.1 |
| E7 双清单漂移 | 判断正确，但**定级偏低**：它是 §1 那个 P0 的直接成因，应与 P0 同批收口（见第 3 档）。审计漏掉的是：`C++_技术方案.md:761`（附录 A.4）"链接库固定"清单本身也错——列了 `dwmapi`（实际由 `SystemInfo.cpp` 走 `LoadLibraryW` 动态加载，不入链），漏了 `advapi32`、`uuid`、`wtsapi32`。**同一件事现在有三份互不一致的清单**（文档 A.4 / CMake / build-tests.sh） |
| N9 连贴竞态 | 现状属实（未闭环），但审计报告未记 `技术方案.md:381` 已实测的边界：`WM_ENDSESSION` 无法用 `PostMessageW` 注入验证、`WM_WTSSESSION_CHANGE` 注入可达且已实测取消 |

### 2.3 已过时

| 编号 | 核实 |
|---|---|
| E1 发布脚本"一个都不存在" | 报告成文于 `2af89cc` 之前。现已存在：`cpp/scripts/CleanAndBuild.bat`、`PackageRelease.bat`、`ReleaseChecklist.md`、`cpp/installer/install.bat`、`uninstall.bat`。**仍缺**：MSVC 出包实测、`dumpbin` 结案证据、干净 VM 验收（报告 §11 声明未做，与实际一致） |
| E6 "git 仅 2 提交" | 现 3 提交；"整块入库、无法逐步追溯"的判断仍成立，数字过时 |

### 2.4 不成立 / 需驳回

| 项 | 理由 |
|---|---|
| §4 依赖表把 RapidJSON 列为"第三方 MIT，实际未使用" | 同报告 D1 自己已澄清零第三方 —— 表格与结论自相矛盾，应按"零第三方"记 |
| §5 性能表"n≤500 实测无感" | 报告 §11 明确"未做实机运行"，无一份计时/帧率数据支撑 → 属推断，不得作为"已验证"引用 |
| A2「全局静态跨窗口是缺陷」 | 现状可用且已有防护：`ProcessPicker::Cleanup()` 先卸钩子再清单例（技术方案 §5.5）。归为**观察项**，不列整改 |
| T2 抽出 `HitTest` 做单测 | 方向没错但当前无收益：命中逻辑的输入是 D2D 目标尺寸，抽纯函数需一并搬布局常量，属重构而非缺陷修复。挂**可选** |

---

## 3. 整改清单（留待步骤 12 B 一并做）

> 本节的"待做"是登记时的口径；**当前实际状态以 §6 执行记录为准**。

### 第 1 档 · P0，不修则发版即失败

| # | 动作 | 文件 | 验证口径 |
|---|---|---|---|
| 1 | `SC_COMMON_SOURCES` 补 `src/core/Settings.cpp`；`SC_APP_SOURCES` 补 `PasteService.cpp`、`ProcessPicker.cpp`、`HelpWindow.cpp` | `cpp/CMakeLists.txt:18-44` | 已实测：补后 mingw 侧 undefined 由 22→1、`sc_tests` 由 12→0（§1.2 B/C/E）；**MSVC 侧未验证** |
| 2 | `target_link_libraries(SuperClip)` 补 `wtsapi32` | `cpp/CMakeLists.txt:72-74` | 已实测：补后链接通过（§1.2 D）；`dumpbin /dependents` 见 `WTSAPI32.dll` 属正常，需在 A.4 白名单登记 |

### 第 2 档 · P1，用户可见错误

| # | 动作 | 文件 | 验证口径 |
|---|---|---|---|
| 3 | 帮助窗第 3 步文案「先用 ↑ ↓ 选中，再按空格粘贴」→「单击选中一条，再按空格粘贴」 | `cpp/src/ui/HelpWindow.cpp:30` | 需实机走查（帮助窗第 3 页截图 + 快速模式单击选中后空格连贴） |
| 4 | 补 `CHANGELOG.md`（agent.md 硬性） | 根目录 | 与版本项（第 4 档）同批 |

### 第 3 档 · 一致性收口（防 §1 那类回归，需授权改契约原文）

| # | 动作 | 文件 | 备注 |
|---|---|---|---|
| 5 | 附录 A.4"链接库固定"改为与实际一致：删 `dwmapi`（动态加载不入链），补 `advapi32`、`uuid`、`wtsapi32` | `C++_技术方案.md:761` | **改契约原文需单独授权** |
| 6 | 交叉构建改走 CMake（`-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++`），消灭手抄清单；`app.rc` 的 windres 差异用 `if(MSVC)` 隔离 | `cpp/build-tests.sh`、`CMakeLists.txt:66-67` | **已落地（2026-10-05）**：WSL 装 `cmake` 3.22.1，`build-tests.sh` 改为驱动 `cmake --build`，脚本内源/库清单全删；windres 实测能编 `app.rc`，无需 `if(MSVC)` 隔离。见 §6.2 |
| 7 | JSON 选型 ADR 由 RapidJSON 改写为"自研极简 JSON"，删 §10.1 `:661` 的 RapidJSON/Catch2 表述 | 设计方案 `:46,54,394,452,545`、技术方案 `:661` | **改契约原文需单独授权** |

### 第 4 档 · 已推后的 agent.md 整改（用户明示"文档的事往后推"，此处仅登记不启动）

图标 `SuperClip.ico`（16/24/32/48/256）+ 解开 `app.rc:6`；状态栏「by Mr lin」署名（版本号在前）与 `app.rc:29` 版权同步；`doc/` 四件套；README 按 11 段模板重写（现为开发者向）；升版本号三处一致；WSL 按项目隔离到 `E:\public`；一次性 QA 脚本去留。

### 第 5 档 · 明确不做（附理由）

- `favoriteCount_` 增量计数替代 `Boundary()`：n≤500 无实测瓶颈，为未确证的规模风险改动核心分区不变式，收益/风险不划算。
- `Sha256` 失败"真拒绝入列"：BCrypt 属系统组件，失败时数据面已坏，新增分支无法构造测试场景。**改为删掉 `Sha256.cpp:38` 那句不符实的注释尾巴**（1 行，与行为对齐）。
- `HitTest` 抽纯函数做单测（见 §2.4）。
- Windows CI：原阻塞理由（"两份清单漂移，CI 只会重复 CMake 的假绿"）随 §6.2 消失——现在 CI 跑 `cmake --build` 就是本地跑的同一条路径。仍不做的原因改为：配置远端工作流属发布类动作，需触发词授权；且 CI 上仍是 mingw 或 VS 镜像二选一，选哪个镜像要先定。

---

## 4. 与步骤 12 B 段的合并点

12 B 段仍欠（全部因本机无 MSVC / 无第二台干净 VM 而阻塞）：MSVC `build.bat` 出包、`dumpbin /dependents`+`/imports` 贴入发布记录（AC-8 结案证据）、≤3 MB 体积实测、AC-7/AC-8 干净 VM 走查（操作单已在 `cpp/scripts/ReleaseChecklist.md`）。

建议把 **第 1、2 档与 12 B 合并成一轮**：第 1 档修的就是"MSVC 能不能链接"，只有在同一次真实 MSVC 构建里才能同时结案，分两轮会重复出包成本。

## 5. 复现命令（已实测跑通，2026-10-05）

把下面存成 `%TEMP%\sc-audit-repro.sh`，在 WSL 里执行 `tr -d '\r' < … > /tmp/repro.sh && bash /tmp/repro.sh`。
源清单与库清单**直接从 `CMakeLists.txt` 抓取**，不靠手抄，因此修好后重跑同一条命令即可看到 `UNDEFINED` 归零。

```bash
#!/usr/bin/env bash
# Feed the linker EXACTLY what cpp/CMakeLists.txt declares (sources + libraries scraped
# straight out of the file, so it survives line-number drift). mingw stands in for MSVC:
# same input set, different error wording (undefined reference vs LNK2019).
REPO=/mnt/e/qcode/superclip/cpp
CXX=x86_64-w64-mingw32-g++
rm -rf /tmp/simaudit && mkdir -p /tmp/simaudit && cd /tmp/simaudit || exit 1
FILES=$(grep -o 'src/[A-Za-z0-9/]*\.cpp' "$REPO/CMakeLists.txt" | sort -u)
echo "scraped_files=$(echo "$FILES" | wc -l)"
for f in $FILES; do
  $CXX -std=c++20 -O0 -c -DUNICODE -D_UNICODE -DNOMINMAX -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 \
       -fno-strict-aliasing "$REPO/$f" -o "$(basename "$f").o" 2>/dev/null || echo "COMPILE_FAIL $f"
done
LIBS=$(sed -n '/^target_link_libraries(SuperClip/,/^)/p' "$REPO/CMakeLists.txt" | grep -o '[a-z0-9_]*' \
       | grep -E '^(user32|kernel32|shell32|gdi32|advapi32|ole32|oleaut32|uuid|bcrypt|d2d1|dwrite|wtsapi32)$' \
       | sed 's/^/-l/' | tr '\n' ' ')
echo "scraped_libs=$LIBS"
$CXX -municode -static -o app.exe *.o $LIBS 2> link.log
echo "LINK_EXIT=$?  UNDEFINED=$(grep -c 'undefined reference' link.log)"
grep -o "undefined reference to \`[^']*'" link.log | sed "s/undefined reference to //" | sort -u
cd / && rm -rf /tmp/simaudit
```

两次实测输出（原样）：

```
# 修复前（§1.2 B 的证据）
scraped_files=21
scraped_libs=-luser32 -lkernel32 -lshell32 -lgdi32 -ladvapi32 -lole32 -loleaut32 -luuid -lbcrypt -ld2d1 -ldwrite
LINK_EXIT=1  UNDEFINED=22

# 修复后（§6 第 1 档的证据）
scraped_files=25
scraped_libs=-luser32 -lkernel32 -lshell32 -lgdi32 -ladvapi32 -lole32 -loleaut32 -luuid -lbcrypt -ld2d1 -ldwrite -lwtsapi32
LINK_EXIT=0  UNDEFINED=0
```

> 踩过的坑，记录以免重犯：抓取源文件名的正则若写成 `[A-Za-z/]`（漏数字），会把 `Sha256.cpp` 静默丢掉，
> 于是多报 2 处 undefined（`sc::HexSha256Utf8`）且 `scraped_files=20` —— 数字对不上先查正则，别改结论。

同类旁证脚本本轮还跑了：补 4 个源后 `UNDEFINED=1`、再加 `-lwtsapi32` 后链接通过；`sc_tests` 侧 12→0。
第 3 档第 6 项已落地，这条旁证命令的现役替代就是 `cd cpp && bash build-tests.sh`（内部即 `cmake --build`，见 §6.2）；命令本身保留在此，用于随时复现"缺项长什么样"。

> 本文档为只读核实记录，未修改被审计代码。修动按 agent.md 约定逐项确认后执行。

---

## 6. 执行记录（2026-10-05，本轮）

| 档位项 | 状态 | 改动 | 验证 |
|---|---|---|---|
| 第 1 档 #1 补源 | **已完成** | `CMakeLists.txt` `SC_COMMON_SOURCES` +`src/core/Settings.cpp`；`SC_APP_SOURCES` +`PasteService.cpp`/`ProcessPicker.cpp`/`HelpWindow.cpp` | §5 复现命令复跑：`scraped_files` 21 → **25**，`UNDEFINED` 22 → **1** |
| 第 1 档 #2 补库 | **已完成** | `target_link_libraries(SuperClip)` 末位 +`wtsapi32`（并加一行注释说明不链则 LNK2019） | 复现命令：`scraped_libs` 现含 `-lwtsapi32`，**`LINK_EXIT=0  UNDEFINED=0`**，产出 exe（-O0 目标文件 4,586,994 B） |
| 第 2 档 #3 帮助窗文案 | **已完成（含实机走查 + 读图核对）** | `HelpWindow.cpp:30`「先用 ↑ ↓ 选中」→「单击选中一条」（与 ADR T6 对齐；新文案更短，不会增加换行） | 编译：`build-tests.sh --app` 通过、无新增告警。实机：右键菜单 → 帮助窗打开、翻到第 3 页、截图三张（页 1/2/3 md5 互异，证明确实翻页），并已读图逐字核对渲染文案，见 §6.1 |
| 第 5 档 · `Sha256` 注释 | **已完成** | `Sha256.cpp:38` 改为如实描述："失败返回空串：调用方不额外拒绝，条目照常入列（去重此时按空串比对）" | 仅注释；`build-tests.sh --app` 通过 |
| 第 3 档 #6 交叉构建走 CMake | **已完成**（详见 §6.2） | WSL 装 `cmake` 3.22.1；`build-tests.sh` 改为驱动 `cmake --build`，删除脚本内手抄的源/库清单与 `--app` 手工链接行；`CMakeLists.txt` 两处过时注释同步 | `CONFIGURE_RC=0`、`BUILD_RC=0`、error/undefined **0**、自有源 warning **0**；`sc_tests.exe` Windows 实跑 40/214/0；`objdump -p` 见 `WTSAPI32.dll` 入导入表；windres 编 `app.rc` 成功且 Windows 读到 `FileVersion 2.0.2.0` |
| 回归 | **无回归** | —— | `build-tests.sh` → `sc_tests.exe` 在 Windows 实跑：**用例 40、断言 214、失败 0**；`build-tests.sh --app` → `SuperClip.exe` 编译通过（3,507,020 B）。〔该 `--app` 参数随后被 §6.2 的 CMake 单一清单构建取代〕 |

### 6.1 实机走查记录（2026-10-05，用户让出桌面约 60 秒）

驱动脚本：`cpp/qa/help11.ps1`（新建，ASCII-only；一次性开窗/翻页/截图/关窗探针，产物写 `cpp/build-mingw/`，已被 `.gitignore` 覆盖）。
数据防护：走查前 `%APPDATA%\SuperClip\{history.json,settings.json,error.log}` 备份到 `%LOCALAPPDATA%\Temp\sc-audit-bak`，全程只投喂一次性合成条目（token `AUDITQ-73419`），走查后还原并核对 md5，随后删除备份目录与全部 Temp 探针。

快速模式语义（`PasteMode:1`，靶窗 `SuperClipPasteTarget` 的 EDIT 初始为 `HEAD\r\nTAIL`）：

| 动作 | 靶窗 EDIT 内容 | 判据 |
|---|---|---|
| 单击选中一条（不按键） | `dumplen=10 dump=[HEAD\r\nTAIL]` | 点选**不粘贴**，与 ADR「点选才算绑定、空格才粘贴」一致 |
| 随后按空格 | `dumplen=22 dump=[HEADAUDITQ-73419\r\nTAIL]` | 空格粘贴，插入位置正确（光标在 HEAD 之后） |

帮助窗走查输出（原样）：

```
main rect=980,120 size=380x600 visible=True
help window exists=True visible_before_test=False
help OPENED dpi=96 scale=1 rect=548,270 size=420x300
  shot auditfix-help-p1.png … md5=4DD94E3B…
  shot auditfix-help-p2.png … md5=CF02B109…
  shot auditfix-help-p3.png … md5=CD878D66…
page3 pixels differ from page1 = True
main window still alive = True
RESULT ok
```

走查收尾状态（已实测）：

```
main=True mainVisible=True
help=True helpVisible=False                      # 帮助窗已关闭，不残留
superclip_visible_windows=SuperClipMain|132506;SuperClipPasteTarget|1312052   # 无残留菜单/浮窗
pre_backup_md5=AF92FB8B11D466012A0F11516E4E598E
pre_live_md5=3B6E7762546AFA34C8D33E1134A7F2EB    # 走查污染后的实测值，与备份不同 → 证备份确为测前快照
pre_live_has_marker=True backup_has_marker=False
restored_history_md5_match=True  live=AF92FB8B11D466012A0F11516E4E598E
restored_settings_md5_match=True live=149E1406517B948DB4C238FE8AB56904
marker_present_after_restore=False
superclip_still_running=0 pastetarget_still_running=0
```

**走查中踩到的坑（务必记录，否则会误判"通过"）**：帮助窗在启动时是**创建即隐藏**（`MainWindow.cpp:211`），所以 `FindWindowW` 能拿到句柄并不等于窗口开着。头两次运行据此判定成功，实际截到的是桌面（背景色恒定 36,38,36、墨迹铺满 415×257、`rect=0,0` 正是创建位），页间 md5 差异只是我移动了鼠标。改为**轮询 `IsWindowVisible` 且只在可见后截图**后，才得到上面 `rect=548,270` 的真实截图。判据教训：可见性只能用 `IsWindowVisible`，不能用 `FindWindow` 的成功与否。

**已验（读图核对）**：用 Read 工具直接查看 `cpp/build-mingw/auditfix-help-p3.png`，屏幕上第 3 页原文如下（逐字）：

```
使用帮助                                                     3 / 9

两种粘贴模式

普通模式：双击一条即粘贴到目标的光标处。
快速模式：单击选中一条，再按空格粘贴，粘过的条目沉底并变灰。
点击标题栏上的模式文字，或在窗口里右键选【粘贴模式】都能切换。

关闭                        上一步   下一步
```

新文案"单击选中一条"已按预期渲染；正文三行未溢出、未换行错位，"关闭"在左下、"上一步/下一步"在右下，页码 `3 / 9` 正常。第 2 档 #3 至此**完全结案**。

**走查事故（如实记录，与代码无关但影响用户数据）**：清理 Temp 时按 `sc-*` 通配批量删除，误删了上一轮按"覆盖真实用户数据前一律留底"规则**有意保留**的
`%LOCALAPPDATA%\Temp\sc-pre-restore\history.polluted.json`（154 条那份，含 10-04 21:47 之后用户真实复制的条目），**不可恢复**。
现行 `%APPDATA%\SuperClip\history.json` 已核对为测前快照（md5 `AF92FB8B…`、36 350 B），未受影响；
另一风险是本轮还原前只比对了 md5、**未比对条目数**，无法证明那 60 秒里用户没有真实复制。
两条已写入项目记忆，后续走查的固定动作改为：还原前打印"live 与备份各自条目数 + 各自是否含本轮 token"，数不对就停下来问；
清理时只删本轮自己新建的那个目录。

**本轮未做（有意）**：第 2 档 #4 `CHANGELOG.md` 与第 4 档绑定（版本号/署名/图标同批）；第 3 档 #5 #7 需授权改两份契约原文。
**MSVC 侧仍属未验证**：§1.2 的旁证与 §6.2 的真实 `cmake --build` 都是 mingw 工具链，只证明"清单已补齐、CMake 路径本身能配置能构建"；`build.bat`（MSVC）真实出包与 `dumpbin` 结案仍待步骤 12 B。

### 6.2 第 3 档 #6 落地：交叉构建改走 CMake 单一清单（2026-10-05，同日追加）

| 动作 | 结果 |
|---|---|
| WSL `lxsyzd` 装 `cmake` | `apt-get install -y cmake` → `APT_RC=0`，`cmake version 3.22.1`（此前只有 `make`，所以 §1.2 只能用旁证） |
| `cpp/build-tests.sh` 重写 | 删掉脚本内手抄的 `SRC=(…)` 与 `LIBS=(…)` 和整条 `--app` 手工链接行；改为 `cmake -S . -B build-mingw/cmake` + `cmake --build`，再把两个 exe 拷到 `build-mingw/` 供 `cpp/qa/*.ps1` 取用。**源清单与库清单从此只有 `CMakeLists.txt` 一份** |
| 真实 CMake 构建 | `CONFIGURE_RC=0`（`The CXX compiler identification is GNU 10.0.0`）、`BUILD_RC=0`；`error:`/`undefined reference`/`No rule to make` 合计 **0 行**；编译器 `warning:` **0 行**（日志里 6 行 `warning:` 全是 `gmake: Clock skew detected`）；`CMake Warning` **0** 条 |
| 产物（冷构建，`CMAKE_BUILD_TYPE=Release` = `-O3 -DNDEBUG -fno-strict-aliasing`） | `SuperClip.exe` **3,556,680 B**、`sc_tests.exe` **3,276,987 B**；Windows 侧 `VersionInfo.FileVersion` 读到 **2.0.2.0** |
| 单测（CMake 产物） | Windows 实跑 `build-mingw/sc_tests.exe`：**用例 40、断言 214、失败 0** |
| 增量与幂等 | 第二次跑 `build-tests.sh` 用时 **5.06 s**，`[100%] Built target …` 两行，无重编风暴 |
| 顺带保住的一处差异 | 旧脚本的 `-fno-strict-aliasing` 在 CMake 侧原本没有 → 已补进 `CMakeLists.txt` 两个非 MSVC 分支（沿用旧 codegen 假设，未单独验证其必要性；补它使体积 +1.5 KB） |
| 顺带去掉的一处噪声 | 首跑有 `CMake Warning: Manually-specified variables were not used: CMAKE_C_COMPILER`（`project()` 只声明 CXX）→ 脚本不再传该变量，冷构建 `CMake Warning` 归零 |

**`app.rc` 的 windres 差异实测为"不存在"**：构建日志有 `[ 97%] Building RC object CMakeFiles/SuperClip.dir/src/res/app.rc.res`，
产物有 `.rsrc` 段（0xac8 字节），Windows 侧 `Get-Item SuperClip.exe | % VersionInfo` 读到
`FileVersion=2.0.2.0  ProductVersion=2.0.2.0  Company=SuperClip`，`.rsrc` 内可见 manifest 的
`<requestedExecutionLevel level="asInvoker"/>`、`dpiAware`、`compatibility` 段。
→ 整改清单原写的"`if(MSVC)` 隔离 `app.rc`"**没有必要**，未做（这是对第 3 档 #6 登记动作的一处偏离，理由是上面的实测）。
仍属未验证：RT_MANIFEST 是否被 OS 实际加载、与 `RC.exe` 产物的字节级差异 —— 归 `dumpbin /resources`（12 B）。

**导入表实测**（`objdump -p SuperClip.exe | grep 'DLL Name'`，mingw 交叉构建、非发布产物）：

```
DWrite.dll  GDI32.dll  KERNEL32.dll  SHELL32.dll  USER32.dll  WTSAPI32.dll  bcrypt.dll  d2d1.dll  msvcrt.dll  ole32.dll
```

- `WTSAPI32.dll` 在表内 → 第 1 档 #2 的补链在**最终二进制**层面成立，不只是链接器不报错。
- 表里没有 `oleaut32`/`advapi32`/`uuid`：`uuid` 只是 GUID 数据（不产生导入），另两个当前无被调符号 → 链接库清单里它们是冗余项，但删它们属"清理"不是"修错"，未动。
- `msvcrt.dll` 是 mingw 静态 CRT 仍留的导入；MSVC `/MT` 版是否出现同名导入**未验证**，所以这张表**不能**直接当 A.4 白名单结案证据。
- 顺带一条体积事实（同一份代码，三种 mingw 优化档实测）：无 `CMAKE_BUILD_TYPE`（＝`-O0`）4,590,084 B、旧脚本 `-O1` 3,507,020 B、CMake `Release`（`-O3 -DNDEBUG -fno-strict-aliasing`）3,556,680 B。
  §10.3 的"≤3 MB"判据在交叉构建上任何一档都没接近过，只能在 MSVC `/O2 /GL /MT` 上结。

**一处环境噪声**：冷构建日志有 6 行 `gmake: warning: Clock skew detected`（源文件在 `/mnt/e`，其 Windows mtime 比 WSL 时钟超前约 2 s，刚写出的 `.o` 反而"更旧"）。
实测 `date` 与 `Get-Date` 差 2 s；冷构建没有"跳过重编"的可能，增量构建那一轮则无此告警，判定为无害。
若日后出现"改了码没重编"的怪象，先校时（`wsl --shutdown` 会同步一次），别怀疑构建系统。

