@echo off
setlocal enableextensions
rem SuperClip C++ - release packaging (tech doc 10.3 / design doc 11.2).
rem ASCII ONLY in .bat: cmd.exe decodes with the console codepage (cp936 here), so a UTF-8
rem Chinese comment makes its lead byte swallow the next character and breaks parsing.
rem
rem What it does:
rem   1) CleanAndBuild (wipe build\ then full rebuild, so no stale objects survive)
rem   2) read FILEVERSION from src\res\app.rc - the single source of the version, never hardcoded here
rem   3) stage: fixed-name exe + README + CHANGELOG + installer\*.bat
rem   4) emit release\SuperClip_v{Ver}.exe (single file) and release\SuperClip_v{Ver}_portable.zip
rem
rem Red lines: the installed/running binary must stay named SuperClip.exe (process name, single
rem instance path comparison, tray, uninstaller all depend on it); the versioned copy exists only
rem for distribution naming. All package/attachment names are ASCII.

cd /d "%~dp0.."

set "STAGE=release\stage"
set "EXE=build\Release\SuperClip.exe"
set "RC=src\res\app.rc"

echo [1/5] clean rebuild ...
call scripts\CleanAndBuild.bat || exit /b 1

if not exist "%EXE%" (
  echo [fail] %EXE% not found. build.bat must produce it under build\Release\.
  exit /b 1
)

echo [2/5] reading FILEVERSION from %RC% ...
set "RAW="
for /f "usebackq tokens=2" %%A in (`findstr /r /c:"^ *FILEVERSION" "%RC%"`) do set "RAW=%%A"
if "%RAW%"=="" (
  echo [fail] no line matching "^ *FILEVERSION" in %RC%. Refusing to guess a version.
  exit /b 1
)
rem FILEVERSION is written as 2,0,3,0 - take the first three fields for the package name.
set "VER="
for /f "tokens=1,2,3 delims=," %%A in ("%RAW%") do set "VER=%%A.%%B.%%C"
if "%VER%"=="" (
  echo [fail] cannot parse a version out of "%RAW%".
  exit /b 1
)
echo       version = v%VER%  (FILEVERSION %RAW%)

rem agent.md 4.2: app.rc FILEVERSION, Config.h kVersionText and the on-screen version must carry the
rem same number. The UI draws kVersionText, so comparing the two source files catches a missed bump.
findstr /c:"v%VER%" "src\core\Config.h" >nul 2>&1
if errorlevel 1 (
  echo [fail] src\core\Config.h has no v%VER% - it disagrees with app.rc, refusing to package.
  exit /b 1
)
echo       version check passed: Config.h kVersionText carries v%VER%

echo [3/5] staging ...
rmdir /s /q "%STAGE%" >nul 2>&1
mkdir "%STAGE%" >nul 2>&1
mkdir "%STAGE%\installer" >nul 2>&1
copy /y "%EXE%" "%STAGE%\SuperClip.exe" >nul || exit /b 1
copy /y "installer\install.bat"   "%STAGE%\installer\" >nul || exit /b 1
copy /y "installer\uninstall.bat" "%STAGE%\installer\" >nul || exit /b 1

if exist "..\README.md" (
  copy /y "..\README.md" "%STAGE%\" >nul || exit /b 1
) else (
  echo [warn] README.md missing at repo root.
)
if exist "..\CHANGELOG.md" (
  copy /y "..\CHANGELOG.md" "%STAGE%\" >nul || exit /b 1
) else (
  echo [warn] CHANGELOG.md missing at repo root - a real release needs it, continuing anyway.
)

echo [4/5] producing versioned artifacts ...
copy /y "%EXE%" "release\SuperClip_v%VER%.exe" >nul || exit /b 1
del /q "release\SuperClip_v%VER%_portable.zip" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath 'release\SuperClip_v%VER%_portable.zip' -Force" || exit /b 1

echo [5/5] size check (target: exe within 3 MB, tech doc 10.3) ...
set "EXESIZE="
for %%F in ("release\SuperClip_v%VER%.exe") do set "EXESIZE=%%~zF"
set "ZIPSIZE="
for %%F in ("release\SuperClip_v%VER%_portable.zip") do set "ZIPSIZE=%%~zF"
echo       exe  = %EXESIZE% bytes
echo       zip  = %ZIPSIZE% bytes
if %EXESIZE% GTR 3145728 (
  echo [warn] exe exceeds 3 MB. Check /MT static CRT, embedded icon size, linker optimisation.
) else (
  echo       exe within the 3 MB budget.
)

echo.
echo Artifacts in %CD%\release :
echo   SuperClip_v%VER%.exe              single-file portable
echo   SuperClip_v%VER%_portable.zip     exe + README + CHANGELOG + installer\*.bat
echo   stage\                            unpacked payload (fixed name SuperClip.exe)
echo.
echo The release record must also carry these two audit outputs (tech doc appendix A.4):
echo   dumpbin /dependents build\Release\SuperClip.exe
echo   dumpbin /imports    build\Release\SuperClip.exe
endlocal
