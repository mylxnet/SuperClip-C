@echo off
setlocal enableextensions
rem SuperClip C++ installer (tech doc 10.3): copy the exe into %ProgramFiles%\SuperClip\ and make .lnk files.
rem ASCII ONLY in .bat: cmd.exe decodes with the console codepage (cp936 here), so a UTF-8
rem Chinese comment makes its lead byte swallow the next character and breaks parsing.
rem
rem Two hard constraints:
rem   1) No registry writes. FR-18 (no autostart) plus appendix A.3 forbids RegSetValueEx*.
rem      A .lnk in the Start Menu / on the Desktop is just an entry point, not an autostart.
rem   2) Restore the fixed name. The payload exe may arrive as SuperClip.exe or only as the
rem      versioned copy; on disk it must be SuperClip.exe (process name, single-instance path
rem      comparison, tray and uninstaller all depend on that name).

set "TARGETDIR=%ProgramFiles%\SuperClip"

net session >nul 2>&1
if errorlevel 1 (
  echo [fail] writing to %ProgramFiles% needs elevation.
  echo        Right-click this file and choose "Run as administrator".
  exit /b 1
)

set "SRC="
if exist "%~dp0SuperClip.exe" set "SRC=%~dp0SuperClip.exe"
if not defined SRC (
  for %%F in ("%~dp0..\SuperClip.exe") do if exist "%%~fF" set "SRC=%%~fF"
)
if not defined SRC (
  for %%F in ("%~dp0..\SuperClip_v*.exe") do if exist "%%~fF" set "SRC=%%~fF"
)
if not defined SRC (
  echo [fail] no SuperClip.exe found next to this script or in the package root.
  echo        Expected layout: package\SuperClip.exe plus package\installer\install.bat
  exit /b 1
)
echo [info] installing from: %SRC%

if not exist "%TARGETDIR%" mkdir "%TARGETDIR%" || exit /b 1
copy /y "%SRC%" "%TARGETDIR%\SuperClip.exe" >nul || exit /b 1

rem Shortcuts via WScript.Shell COM. One line on purpose: cmd's "^" line continuation is
rem unreliable once the command itself contains embedded double quotes.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $t = '%TARGETDIR%\SuperClip.exe'; $a = $ws.CreateShortcut((Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\SuperClip.lnk')); $a.TargetPath = $t; $a.WorkingDirectory = '%TARGETDIR%'; $a.Description = 'SuperClip clipboard history'; $a.Save(); $d = [Environment]::GetFolderPath('Desktop'); $b = $ws.CreateShortcut((Join-Path $d 'SuperClip.lnk')); $b.TargetPath = $t; $b.WorkingDirectory = '%TARGETDIR%'; $b.Description = 'SuperClip clipboard history'; $b.Save()" || exit /b 1

echo.
echo [ok] installed to %TARGETDIR%\SuperClip.exe
echo      shortcuts: Start Menu\Programs\SuperClip.lnk, Desktop\SuperClip.lnk
echo      hotkey:    Ctrl + backquote  (raise / hide the window)
echo      data:      %APPDATA%\SuperClip  (history.json, error.log)
echo      no autostart is registered by design; launch it from the shortcut.
endlocal
