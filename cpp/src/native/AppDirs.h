#pragma once
#include "../core/Config.h"
#include <string>

namespace sc {

// %AppData%\SuperClip（不存在则创建）。失败返回空串——调用方据此降级为"仅内存态"。
const std::wstring& DataDir();

std::wstring HistoryPath();      // DataDir()\history.json
std::wstring SettingsPath();     // DataDir()\settings.json（技术方案 §11 步骤 10 起使用）

}  // namespace sc
