cmake_minimum_required(VERSION 3.20)
project(SuperClip LANGUAGES CXX)

# ============================================================
# SuperClip C++ · M1（三窗口 + 消息循环 + 单实例 + 监听 + 热键 + 托盘 + 持久化）
# 主工具链 = MSVC；MinGW 交叉仅用于编译/跑逻辑单测（见 build-tests.sh）
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
  src/ui/Theme.cpp
  src/ui/ListRenderer.cpp
  src/ui/HoverTip.cpp
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
  target_compile_options(SuperClip PRIVATE -Wall -Wextra -Wno-unused-parameter
                         -Wno-cast-function-type -U__STRICT_ANSI__)
  target_link_options(SuperClip PRIVATE -municode -static)   # -static = 静态 CRT，免运行库
  # windres 对 app.rc 内的相对资源路径解析与 RC.exe 不同：交叉构建只用于验证可编译，
  # 正式发布（内嵌 manifest + 版本信息）走 MSVC/RC.exe
endif()

# uuid：SHGetKnownFolderPath 的 FOLDERID_* 定义（MSVC=uuid.lib，mingw=libuuid.a）
# d2d1/dwrite 自步骤 6（ListRenderer）起链接；dwmapi 走 LoadLibrary，不入链
target_link_libraries(SuperClip PRIVATE
  user32 kernel32 shell32 gdi32 advapi32 ole32 oleaut32 uuid bcrypt d2d1 dwrite
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
  target_compile_options(sc_tests PRIVATE -Wall -Wextra -U__STRICT_ANSI__)
endif()
target_link_libraries(sc_tests PRIVATE kernel32 ole32 oleaut32 bcrypt advapi32 user32 shell32)
if(NOT MSVC)
  target_link_options(sc_tests PRIVATE -static)
endif()

add_test(NAME logic COMMAND sc_tests)
