@echo off
setlocal enableextensions
rem SuperClip C++ uninstaller (tech doc 10.3): taskkill, then remove program dir and shortcuts.
rem ASCII ONLY in .bat: cmd.exe decodes with the console codepage (cp936 here), so a UTF-8
rem Chinese comment makes its lead byte swallow the next character and breaks parsing.
rem
rem Never touches user data: %APPDATA%\SuperClip\ (history.json / error.log)
rem stays intact by design - it holds the real clipboard history and cannot be recovered once
rem deleted. A reinstall picks up the same files (format shared with the .NET build).

set "TARGETDIR=%ProgramFiles%\SuperClip"

net session >nul 2>&1
if errorlevel 1 (
  echo [fail] removing %TARGETDIR% needs elevation.
  echo        Right-click this file and choose "Run as administrator".
  exit /b 1
)

echo About to remove:
echo   %TARGETDIR%            (installed program files)
echo   Start Menu\Programs\SuperClip.lnk
echo   Desktop\SuperClip.lnk
echo Kept untouched:
echo   %APPDATA%\SuperClip    (your clipboard history and settings)
echo.
set "ANSWER="
set /p "ANSWER=Type Y to uninstall, anything else to abort: "
if /i not "%ANSWER%"=="Y" (
  echo [abort] nothing was removed.
  exit /b 0
)

taskkill /im SuperClip.exe /f >nul 2>&1
rem The process may not be running at all, so taskkill's errorlevel is not treated as a failure.

if exist "%TARGETDIR%" (
  rmdir /s /q "%TARGETDIR%"
  if exist "%TARGETDIR%" (
    echo [fail] %TARGETDIR% still exists. A file inside may be locked; close the app and retry.
    exit /b 1
  )
  echo [ok] removed %TARGETDIR%
) else (
  echo [info] %TARGETDIR% not present, skipping.
)

del /q "%ProgramData%\Microsoft\Windows\Start Menu\Programs\SuperClip.lnk" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Desktop')) 'SuperClip.lnk') -Force -ErrorAction SilentlyContinue" >nul 2>&1
echo [ok] shortcuts removed (if they existed).
echo.
echo Uninstall complete. Your data is still at %APPDATA%\SuperClip
endlocal
