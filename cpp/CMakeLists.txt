cmake_minimum_required(VERSION 3.20)
project(SuperClip LANGUAGES CXX)

# ============================================================
# SuperClip C++ · M1（三窗口 + 消息循环 + 单实例 + 监听 + 热键 + 托盘 + 持久化）
# 主工具链 = MSVC；MinGW 交叉构建走同一份清单（`bash build-tests.sh`），用于跑逻辑单测与整程序链接检查
# 技术方案 §10.1
# ============================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # /MT 静态 CRT，目标机免运行库
endif()

set(SC_COMMON_SOURCES
  src/core/Text.cpp
  src/core/Time.cpp
  src/core/Sha256.cpp
  src/core/ClipItem.cpp
  src/core/TableParser.cpp
  src/core/Json.cpp
  src/core/Store.cpp
  src/core/Settings.cpp
  src/services/StorageService.cpp
  src/util/Log.cpp
)

set(SC_APP_SOURCES
  ${SC_COMMON_SOURCES}
  src/native/AppDirs.cpp
  src/native/Clipboard.cpp
  src/native/HiddenWindow.cpp
  src/native/SystemInfo.cpp
  src/services/ClipboardMonitor.cpp
  src/services/TrayService.cpp
  src/services/PasteService.cpp
  src/services/ProcessPicker.cpp
  src/services/RelayService.cpp
  src/ui/Theme.cpp
  src/ui/ListRenderer.cpp
  src/ui/HoverTip.cpp
  src/ui/HelpWindow.cpp
  src/ui/MainWindow.cpp
  src/app/AppContext.cpp
  src/main.cpp
)

if(WIN32)
  list(APPEND SC_APP_SOURCES src/res/app.rc)
endif()

add_executable(SuperClip WIN32 ${SC_APP_SOURCES})

target_compile_definitions(SuperClip PRIVATE
  UNICODE _UNICODE NOMINMAX
  WINVER=0x0601 _WIN32_WINNT=0x0601      # Win7 SP1 下限；新符号一律动态加载，禁止提升此宏
)

if(MSVC)
  target_compile_options(SuperClip PRIVATE /W4 /permissive- /utf-8 /EHsc /sdl /guard:cf
                           $<$<CONFIG:Release>:/O2 /GL>)
  target_link_options(SuperClip PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
                      $<$<CONFIG:Release>:/LTCG>)
else()
  # -fno-strict-aliasing：沿用迁移前 build-tests.sh 的 codegen 假设；交叉构建改走 CMake 时一并保住，未单独验证其必要性
  target_compile_options(SuperClip PRIVATE -Wall -Wextra -Wno-unused-parameter
                         -Wno-cast-function-type -U__STRICT_ANSI__ -fno-strict-aliasing)
  target_link_options(SuperClip PRIVATE -municode -static)   # -static = 静态 CRT，免运行库
  # 实测：windres 能编 src/res/app.rc（manifest + VERSIONINFO 进 .rsrc，Windows 侧能读到 FileVersion）。
  # 但 RC.exe 与 windres 对 RT_MANIFEST 的处理差异只能在 MSVC 产物上用 dumpbin /resources 结案，见步骤 12 B。
endif()

# uuid：SHGetKnownFolderPath 的 FOLDERID_* 定义（MSVC=uuid.lib，mingw=libuuid.a）
# d2d1/dwrite 自步骤 6（ListRenderer）起链接；dwmapi 走 LoadLibrary，不入链
# wtsapi32：WTSRegisterSessionNotification（MainWindow 锁屏取消点选）——不链则 LNK2019
target_link_libraries(SuperClip PRIVATE
  user32 kernel32 shell32 gdi32 advapi32 ole32 oleaut32 uuid bcrypt d2d1 dwrite wtsapi32
)

set_target_properties(SuperClip PROPERTIES
  OUTPUT_NAME "SuperClip"                       # 固定名：进程名/installer/单实例都依赖它
)
# 不再设 VS_DPI_AWARE：DPI 感知由 src/res/app.manifest 内嵌（§10.2），二者同时存在会冲突

# ---------------- 逻辑层单测（不链 UI/图形库）----------------
enable_testing()
add_executable(sc_tests
  ${SC_COMMON_SOURCES}
  tests/test_main.cpp
)
target_compile_definitions(sc_tests PRIVATE
  UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601
)
if(MSVC)
  target_compile_options(sc_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
  target_compile_options(sc_tests PRIVATE -Wall -Wextra -U__STRICT_ANSI__ -fno-strict-aliasing)
endif()
target_link_libraries(sc_tests PRIVATE kernel32 ole32 oleaut32 bcrypt advapi32 user32 shell32)
if(NOT MSVC)
  target_link_options(sc_tests PRIVATE -static)
endif()

add_test(NAME logic COMMAND sc_tests)
