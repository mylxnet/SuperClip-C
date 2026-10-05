# SuperClip（C++ 版）

Windows 剪贴板历史管理工具。后台监听复制，保留最近条目，用全局热键呼出列表，选中即把内容粘贴到目标应用。

技术形态：**C++20 + 纯 Win32 消息循环 + Direct2D/DirectWrite 全自绘**，不依赖 .NET / Qt / WinUI，
静态链接 CRT（`/MT`），目标机免运行库。**不含任何网络代码**（需求红线 AC-8）。

## 功能

| 能力 | 说明 |
|---|---|
| 自动记录 | 监听剪贴链，文本按 SHA-256 去重置顶，上限 500 条（末位淘汰，收藏项不参与） |
| 表格拆分 | 复制 Excel/WPS 的 TSV 区域时拆成逐格条目，并记录来源行列（标注不上屏，但仍参与搜索） |
| 两种粘贴模式 | 普通模式＝双击粘贴后收起；快速模式＝列表常驻，空格/回车连续粘贴，已粘条目灰显沉底 |
| 目标绑定 | 默认粘贴到"呼出前那个窗口"；点标题栏靶心进入点选模式，可把目标固定到某个进程 |
| 搜索/过滤/收藏 | 300ms 防抖子串搜索；`全部/文本/表格单元格/收藏` 四个视图；收藏只在【收藏】视图出现且不被「清除」删除 |
| 界面 | 无边框自绘窗口（可拖拽、可缩放、可置顶）、悬浮全文气泡、9 步使用帮助窗 |
| 系统集成 | 托盘常驻、`Ctrl+`` 全局热键、单实例、DPI 感知、退出即落盘 |

## 仓库布局

```
README.md               本文
CHANGELOG.md            版本历史（v2.0.3 / v2.0.2 详细，更早指向 doc/技术方案.md）
agent.md                工程规则（复述后动手、文档硬性、发布授权、每轮升版本号等）
doc/
  SuperClip_设计规范.html   需求契约（FR/AC 条目，v1.2）
  技术方案.md               .NET 旧版行为说明（其 §1 技术栈章节已作废）
  DESIGN.md             契约级设计：FR 映射、算法规范、文档矛盾取值 C1–C13、ADR
  PROJECT.md            实现级设计：文件清单、接口签名、消息路由表、测试方案、里程碑判据
  PROJECT_STATE.md      当前状态、环境搭建、命令、踩过的坑、架构决策的理由
  TESTING.md            40 例逐条用例、覆盖缺口、实机走查轮次、数据防护规程
  DEPLOY.md             部署与运维：安装/卸载/数据位置/接管 .NET 数据/故障排查
  审计报告.md            第三方审计原始结论
  审计核实与整改清单.md    逐条核实（成立/不成立）+ 整改档位与授权口径
cpp/
  CMakeLists.txt          MSVC 主构建
  build.bat               一键构建（自动定位 VS2022 vcvars64）
  build-tests.sh          WSL/Linux 侧 mingw 交叉构建（驱动 CMake，清单只有一份）
  src/                    core（纯逻辑）· native（Win32 RAII）· services · ui · app
  tests/test_main.cpp     纯逻辑单测
  tools/PasteTarget.cpp   粘贴闭环用的极简目标程序
  qa/                     实机走查驱动脚本（PowerShell）
```

## 构建

**Windows + VS2022（发布路径）**

```bat
cd cpp
build.bat
```

产物 `cpp\build\Release\SuperClip.exe`，单测 `sc_tests.exe`。

**WSL / Linux 交叉编译（无 MSVC 时的验证路径）**

```bash
# 依赖：cmake、g++-mingw-w64-x86-64
cd cpp
bash build-tests.sh          # 配置 + 构建两个目标 → build-mingw/{sc_tests.exe,SuperClip.exe}
```

一次构建两个目标（源清单与链接库清单只由 `CMakeLists.txt` 提供，脚本不再手抄）。
产物是合法的 Windows x64 exe，直接在 Windows 上运行即可。注意这条路径**不是发布构建**：
无 `/W4 /guard:cf /sdl /LTCG`、CRT 静态方式不同（`-static` vs `/MT`）；`app.rc` 已由 windres 编入
`.rsrc`（manifest 与 VERSIONINFO 实测可被 Windows 读到），但 RT_MANIFEST 是否被 OS 实际加载仍需在
MSVC 产物上用 `dumpbin /resources` 结案。

## 测试

- **纯逻辑单测**：40 例 / 214 断言（表格拆分、哈希与来源标注、Store 不变式、JSON、Settings 逐字段容错）。
  在 Windows 上运行 `sc_tests.exe`（依赖 BCrypt 与临时目录）。
- **UI 与粘贴行为无法单测**，按 `doc/PROJECT.md` §9.5 实机走查。`cpp/qa/` 下的脚本就是这套走查的驱动器：
  `step7/step8/step9.ps1` 驱动采集、粘贴、点选绑定、右键菜单与帮助窗，`ui_shot.ps1`/`crop.ps1`/`sample.ps1` 负责截图与取色自证，
  `userdata.ps1` 备份还原 `%APPDATA%\SuperClip` 下的用户数据。
  脚本默认从同级上级目录 `cpp/build-mingw/` 取 exe，并把截图写回那里（已在 `.gitignore` 内）。

## 数据与兼容性

数据文件在 `%APPDATA%\SuperClip\`：`history.json`（条目）、`settings.json`（位置/尺寸/置顶/模式/筛选/绑定进程名）、
`error.log`。字段名与形态与 .NET v2.0.2 保持一致，两版可直接接管同一份数据；坐标以 DIP 存储，读侧按当前 DPI 换算。

## 当前状态

M1 里程碑按 `doc/PROJECT.md` §11 逐步实施，**步骤 1–11 已落地并实机走查**。仍未闭环的项：

- 150% / 200% DPI 下的排版复核（需切换系统缩放）
- 真实 Excel/WPS 表格复制闭环、真实注销/关机下的 `WM_ENDSESSION` 路径
- 干净 VM 验收（AC-7 免运行库、AC-8 断网无拦截日志）与 `dumpbin /dependents` 依赖清单
- 步骤 12 的打包与版本化发布脚本

UI 层结论凡未实机验证的一律标注"未验证"，具体项见 §11 表格最后两列。
