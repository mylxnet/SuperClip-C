#include "AppDirs.h"
#include <shlobj.h>

namespace sc {
namespace {

std::wstring ComputeDataDir() {
  PWSTR roaming = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) || roaming == nullptr) {
    CoTaskMemFree(roaming);
    return {};
  }
  std::wstring dir = roaming;
  CoTaskMemFree(roaming);
  dir += L"\\";
  dir += kDataDirName;
  const DWORD attr = GetFileAttributesW(dir.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
  } else if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return {};                                       // 同名普通文件占位：不覆盖用户数据
  }
  return dir;
}

}  // namespace

const std::wstring& DataDir() {
  static const std::wstring dir = ComputeDataDir();
  return dir;
}

std::wstring HistoryPath() {
  const std::wstring& dir = DataDir();
  return dir.empty() ? std::wstring() : dir + L"\\" + kHistoryFile;
}

std::wstring SettingsPath() {
  const std::wstring& dir = DataDir();
  return dir.empty() ? std::wstring() : dir + L"\\" + kSettingsFile;
}

}  // namespace sc
