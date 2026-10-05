@echo off
setlocal
rem SuperClip C++ build (tech doc 10.3). Detect VS2022 vcvars64, then CMake Release build.
rem
rem ASCII ONLY on purpose: cmd.exe decodes .bat with the console codepage (cp936 on this box),
rem and a UTF-8 Chinese comment makes the lead byte swallow the following character, so
rem brackets / percent signs on the next line get eaten and the script fails to parse.
rem Comments in this repo are normally Chinese (agent.md 1.1); .bat is the documented exception.

cd /d "%~dp0"

set "VCVARS="
for %%E in (Community Professional Enterprise BuildTools) do (
  if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if "%VCVARS%"=="" (
  echo [fail] vcvars64.bat not found. Install VS2022 with the VC++ Tools workload.
  exit /b 1
)

call "%VCVARS%" >nul || exit /b 1
cmake -S . -B build || exit /b 1
cmake --build build --config Release || exit /b 1

echo.
echo artifact: build\Release\SuperClip.exe
echo tests   : build\Release\sc_tests.exe   (run it on Windows, needs BCrypt and a temp dir)
endlocal
