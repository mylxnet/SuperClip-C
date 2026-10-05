#!/usr/bin/env bash
# SuperClip C++ · 交叉构建（WSL 侧，无 Windows SDK 时的最小组合）
# 环境：项目专属发行版 `superclip`（agent.md 二.2：按项目隔离，数据在 E:\public\superclip\wsl，
#       初始化脚本 E:\public\superclip\provision.sh）
# 依赖：cmake ≥3.20、x86_64-w64-mingw32-{g++,windres}、可选 wine64
# 说明：这只验证「可编译 + 可链接 + 纯逻辑正确」；UI/粘贴/托盘行为仍需在真实 Windows 上按 §9.5 走查。
#
# 源清单与链接库清单**只有 CMakeLists.txt 一份**：本脚本以前自己手抄过一份，两边漂移过一次，
# 直接造成 CMake/MSVC 主路径缺 4 个源 + 缺 wtsapi32 而无人察觉（根目录《SuperClip_审计核实与整改清单.md》§1）。
# 中间产物（CMake 缓存与 .obj）一律留在 WSL 内，本地只收最终 exe（agent.md 二.5）。
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CROSS:-x86_64-w64-mingw32-g++}
RC=${RCROSS:-x86_64-w64-mingw32-windres}
command -v cmake >/dev/null || { echo "未找到 cmake（Ubuntu: apt-get install -y cmake）"; exit 127; }
command -v "$CXX" >/dev/null || { echo "未找到交叉编译器 $CXX"; exit 127; }

BUILD=${SC_BUILD:-$HOME/superclip-build}
cmake -S . -B "$BUILD" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_RC_COMPILER="$RC"

cmake --build "$BUILD" -j "$(nproc)"

# QA 驱动脚本（cpp/qa/*.ps1）按 build-mingw/<exe> 取产物，这里只把最终产物落回本地
mkdir -p build-mingw
cp -f "$BUILD/sc_tests.exe" "$BUILD/SuperClip.exe" build-mingw/
echo "中间产物：$BUILD（WSL 内，可整目录删除）"
echo "产物：build-mingw/sc_tests.exe  build-mingw/SuperClip.exe"

if command -v wine64 >/dev/null 2>&1 || command -v wine >/dev/null 2>&1; then
  WINE=${WINE:-wine64}
  command -v "$WINE" >/dev/null 2>&1 || WINE=wine
  echo "运行单测（$WINE）…"
  "$WINE" build-mingw/sc_tests.exe
else
  echo "本机无 wine：请把 build-mingw/sc_tests.exe 拷到 Windows 执行。"
fi
