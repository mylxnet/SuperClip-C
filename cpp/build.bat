@echo off
setlocal
rem SuperClip C++ · MSVC 构建（技术方案 §10.3）
cd /d "%~dp0"

set "VCVARS="
for %%E in (Community Professional Enterprise BuildTools) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if "%VCVARS%"=="" (
  echo [错误] 未找到 VS2022 的 vcvars64.bat，请安装 VC++ 生成工具后重试。
  exit /b 1
)

call "%VCVARS%" >nul || exit /b 1
cmake -S . -B build || exit /b 1
cmake --build build --config Release || exit /b 1

echo.
echo 产物：build\Release\SuperClip.exe
echo 单测：build\Release\sc_tests.exe   （请在 Windows 上运行，依赖 BCrypt 与临时目录）
endlocal
