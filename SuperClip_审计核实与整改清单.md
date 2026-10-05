# SuperClip 审计报告核实与整改清单

> 核实对象：`SuperClip_审计报告.md`（2026-10-05，第三方只读审计，非本人撰写）
> 本文作者：本仓实施方（被审计方）
> 用途：把外部审计逐条对码核实后的**事实基线 + 整改清单**固化，供 §11 步骤 12 B 段一并执行
> 状态：**第 1、2 档（部分）与 `Sha256` 注释对齐已于 2026-10-05 落地并实机走查**，其余档位待授权/待环境。执行记录见 §6，走查记录见 §6.1。

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

对照：`build-tests.sh:29-37` 的交叉链接行**含**这 4 个源与 `-lwtsapi32`，两份清单不一致。

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
| 6 | 交叉构建改走 CMake（`-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++`），消灭手抄清单；`app.rc` 的 windres 差异用 `if(MSVC)` 隔离 | `cpp/build-tests.sh`、`CMakeLists.txt:66-67` | 需 WSL 装 cmake，或改为在 Windows 侧配置 |
| 7 | JSON 选型 ADR 由 RapidJSON 改写为"自研极简 JSON"，删 §10.1 `:661` 的 RapidJSON/Catch2 表述 | 设计方案 `:46,54,394,452,545`、技术方案 `:661` | **改契约原文需单独授权** |

### 第 4 档 · 已推后的 agent.md 整改（用户明示"文档的事往后推"，此处仅登记不启动）

图标 `SuperClip.ico`（16/24/32/48/256）+ 解开 `app.rc:6`；状态栏「by Mr lin」署名（版本号在前）与 `app.rc:29` 版权同步；`doc/` 四件套；README 按 11 段模板重写（现为开发者向）；升版本号三处一致；WSL 按项目隔离到 `E:\public`；一次性 QA 脚本去留。

### 第 5 档 · 明确不做（附理由）

- `favoriteCount_` 增量计数替代 `Boundary()`：n≤500 无实测瓶颈，为未确证的规模风险改动核心分区不变式，收益/风险不划算。
- `Sha256` 失败"真拒绝入列"：BCrypt 属系统组件，失败时数据面已坏，新增分支无法构造测试场景。**改为删掉 `Sha256.cpp:38` 那句不符实的注释尾巴**（1 行，与行为对齐）。
- `HitTest` 抽纯函数做单测（见 §2.4）。
- Windows CI：依赖第 6 项先统一清单，否则 CI 只会重复 CMake 的假绿；且配置远端属发布类动作需触发词。

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
第 3 档第 6 项落地后，这条命令应由 `cmake --build` 取代。

> 本文档为只读核实记录，未修改被审计代码。修动按 agent.md 约定逐项确认后执行。

---

## 6. 执行记录（2026-10-05，本轮）

| 档位项 | 状态 | 改动 | 验证 |
|---|---|---|---|
| 第 1 档 #1 补源 | **已完成** | `CMakeLists.txt` `SC_COMMON_SOURCES` +`src/core/Settings.cpp`；`SC_APP_SOURCES` +`PasteService.cpp`/`ProcessPicker.cpp`/`HelpWindow.cpp` | §5 复现命令复跑：`scraped_files` 21 → **25**，`UNDEFINED` 22 → **1** |
| 第 1 档 #2 补库 | **已完成** | `target_link_libraries(SuperClip)` 末位 +`wtsapi32`（并加一行注释说明不链则 LNK2019） | 复现命令：`scraped_libs` 现含 `-lwtsapi32`，**`LINK_EXIT=0  UNDEFINED=0`**，产出 exe（-O0 目标文件 4,586,994 B） |
| 第 2 档 #3 帮助窗文案 | **已完成（含实机走查 + 读图核对）** | `HelpWindow.cpp:30`「先用 ↑ ↓ 选中」→「单击选中一条」（与 ADR T6 对齐；新文案更短，不会增加换行） | 编译：`build-tests.sh --app` 通过、无新增告警。实机：右键菜单 → 帮助窗打开、翻到第 3 页、截图三张（页 1/2/3 md5 互异，证明确实翻页），并已读图逐字核对渲染文案，见 §6.1 |
| 第 5 档 · `Sha256` 注释 | **已完成** | `Sha256.cpp:38` 改为如实描述："失败返回空串：调用方不额外拒绝，条目照常入列（去重此时按空串比对）" | 仅注释；`build-tests.sh --app` 通过 |
| 回归 | **无回归** | —— | `build-tests.sh` → `sc_tests.exe` 在 Windows 实跑：**用例 40、断言 214、失败 0**；`build-tests.sh --app` → `SuperClip.exe` 编译通过（3,507,020 B） |

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

**本轮未做（有意）**：第 2 档 #4 `CHANGELOG.md` 与第 4 档绑定（版本号/署名/图标同批）；第 3 档 #5 #7 需授权改两份契约原文，#6 需 WSL 装 `cmake`（当前该发行版只有 `make`）。
**MSVC 侧仍属未验证**：本轮证据全部来自"同一组输入喂 mingw 链接器"，只证明缺项已补齐、不再产生未定义符号；`build.bat` 真实出包与 `dumpbin` 结案仍待步骤 12 B。
