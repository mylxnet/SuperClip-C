#!/usr/bin/env bash
# SuperClip C++ · 逻辑层单测的交叉编译与运行（WSL/Linux 侧，无 Windows SDK 时的最小组合）
# 依赖：x86_64-w64-mingw32-g++（Debian/Ubuntu: g++-mingw-w64-x86-64）、可选 wine64
# 说明：这只验证「可编译 + 纯逻辑正确」；UI/粘贴/托盘行为仍需在真实 Windows 上按 §9.5 走查。
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CROSS:-x86_64-w64-mingw32-g++}
command -v "$CXX" >/dev/null || { echo "未找到交叉编译器 $CXX"; exit 127; }

SRC=(
  src/core/Text.cpp src/core/Time.cpp src/core/Sha256.cpp src/core/ClipItem.cpp
  src/core/TableParser.cpp src/core/Json.cpp src/core/Store.cpp src/core/Settings.cpp
  src/services/StorageService.cpp src/util/Log.cpp tests/test_main.cpp
)

FLAGS=(-std=c++20 -O1 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -static
       -DUNICODE -D_UNICODE -DNOMINMAX -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
       -fno-strict-aliasing)
LIBS=(-lbcrypt -lole32 -loleaut32 -ladvapi32 -lkernel32 -luser32 -lshell32)

mkdir -p build-mingw

# --app：额外做「整程序可编译可链接」检查（不含 app.rc，windres 与 RC.exe 对相对资源路径
# 的解析不一致；发布构建走 MSVC/RC.exe）。产物仅供冒烟运行，非发布包。
if [[ "${1:-}" == "--app" ]]; then
  echo "编译 SuperClip.exe（整程序链接检查）…"
  "$CXX" "${FLAGS[@]}" -municode -o build-mingw/SuperClip.exe \
    src/core/Text.cpp src/core/Time.cpp src/core/Sha256.cpp src/core/ClipItem.cpp \
    src/core/TableParser.cpp src/core/Json.cpp src/core/Store.cpp src/core/Settings.cpp \
    src/services/StorageService.cpp src/util/Log.cpp \
    src/native/AppDirs.cpp src/native/Clipboard.cpp src/native/HiddenWindow.cpp \
    src/native/SystemInfo.cpp src/services/ClipboardMonitor.cpp src/services/TrayService.cpp \
    src/services/PasteService.cpp src/services/ProcessPicker.cpp \
    src/ui/Theme.cpp src/ui/ListRenderer.cpp src/ui/HoverTip.cpp src/ui/HelpWindow.cpp src/ui/MainWindow.cpp \
    src/app/AppContext.cpp src/main.cpp \
    "${LIBS[@]}" -luuid -lgdi32 -ld2d1 -ldwrite -lwtsapi32
  echo "编译通过：build-mingw/SuperClip.exe"
  exit 0
fi

echo "编译 sc_tests.exe（${#SRC[@]} 个源文件）…"
"$CXX" "${FLAGS[@]}" -o build-mingw/sc_tests.exe "${SRC[@]}" "${LIBS[@]}"
echo "编译通过：build-mingw/sc_tests.exe"

if command -v wine64 >/dev/null 2>&1 || command -v wine >/dev/null 2>&1; then
  WINE=${WINE:-wine64}
  command -v "$WINE" >/dev/null 2>&1 || WINE=wine
  echo "运行单测（$WINE）…"
  "$WINE" build-mingw/sc_tests.exe
else
  echo "本机无 wine：请把 build-mingw/sc_tests.exe 拷到 Windows 执行。"
fi
