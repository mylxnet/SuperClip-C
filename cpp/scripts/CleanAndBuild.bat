@echo off
setlocal
rem SuperClip C++ - clean rebuild before a release (tech doc 10.3).
rem ASCII ONLY in .bat: cmd.exe decodes with the console codepage (cp936 here), so a UTF-8
rem Chinese comment makes its lead byte swallow the next character and breaks parsing.
rem
rem This deletes the whole build\ directory (CMake cache + intermediate objects). It touches
rem nothing else: not src\, not cpp\qa\, not cpp\build-mingw\ (cross-build artifacts and QA
rem screenshots), not %APPDATA%\SuperClip (user data).

cd /d "%~dp0.."

if not exist build (
  echo [info] no build\ directory, nothing to clean.
  goto :build
)

echo [warn] deleting build\ directory (CMake cache + all intermediate objects) ...
rmdir /s /q build || exit /b 1

:build
call build.bat || exit /b 1

echo.
echo [ok] clean rebuild finished. artifact: build\Release\SuperClip.exe
endlocal
