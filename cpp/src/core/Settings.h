#pragma once
#include "Config.h"
#include <string>

// settings.json 的 DTO 与读写（设计方案 §8.3 / §8.4）。
// 字段名、大小写、书写顺序与 .NET v2.0.2 逐字一致，两版可对同一份文件互换读写。
//
// **单位约定**：Left/Top/Width/Height 沿用 .NET（WPF）语义 = **DIP**（96dpi 基准），
// 不是窗口物理像素。换算发生在 UI 层（MainWindow 有 dpi_），本层只管数值合法性。
namespace sc {

struct Settings {
  int left = 0;
  int top = 0;
  int width = kWinW;
  int height = kWinH;
  std::wstring boundProcessName;   // 空 = 未绑定（.NET 侧为 null）
  bool topmost = true;             // C13：默认置顶
  int pasteMode = 0;               // 0 普通 / 1 快速（PasteMode）
  bool splitSingleColumn = false;  // 复制模式，true = 表格复制（CopyMode）
  int filterType = 0;              // FilterType 枚举序号
  bool hasRect = false;            // 文件里确有 Left/Top 才采纳位置，否则回默认停靠
};

// 读侧容错（§8.4）：文件不存在/为空/解析失败/字段类型不符 → 一律回落默认值，
// 只有确实读到的字段才覆盖默认；任何情况都不抛、不阻断启动。
Settings LoadSettings(const std::wstring& path);

// 写侧沿用 history.json 的三级降级（tmp → ReplaceFileW → MoveFileEx → 删除后 move）。
// 失败只记日志并返回 false：设置丢了不影响历史数据。
bool SaveSettings(const std::wstring& path, const Settings& s);

}  // namespace sc
