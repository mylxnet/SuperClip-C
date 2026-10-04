# M1 冒烟：单实例 / 剪贴板采集 / 持久化（临时脚本，运行后可删）
$ErrorActionPreference = "Continue"
$exe = Join-Path $PSScriptRoot "SuperClip.exe"

Get-Process SuperClip -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$old = Get-Clipboard -Raw
$tag = "M1SMOKE-" + (Get-Random -Maximum 999999)

Start-Process -FilePath $exe
Start-Sleep -Seconds 2
$first = @(Get-Process SuperClip -ErrorAction SilentlyContinue).Count
Write-Output "PROCS_AFTER_FIRST=$first"

Set-Clipboard -Value $tag
Start-Sleep -Seconds 2
$hist = Get-Content "$env:APPDATA\SuperClip\history.json" -Raw -Encoding UTF8
$pattern = '"Id":"'
$items = ([regex]::Matches($hist, [regex]::Escape($pattern))).Count
Write-Output "HISTORY_ITEMS=$items"
Write-Output "MARKER_FOUND=$($hist.Contains($tag))"
Write-Output "FAVORITE_FIELD_OK=$($hist.Contains('"IsFavorite":'))"

$second = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 3
$after = @(Get-Process SuperClip -ErrorAction SilentlyContinue).Count
Write-Output "PROCS_AFTER_SECOND=$after"
Write-Output "SECOND_HAS_EXITED=$($second.HasExited)"
Write-Output "MAIN_WINDOW_TITLE=$((Get-Process SuperClip -ErrorAction SilentlyContinue | Select-Object -First 1).MainWindowTitle)"

# 表格采集：多制表符内容应按单元格拆条
Set-Clipboard -Value "TABC1`tTABC2`nTABC3`tTABC4"
Start-Sleep -Seconds 2
$hist2 = Get-Content "$env:APPDATA\SuperClip\history.json" -Raw -Encoding UTF8
Write-Output "TABLE_ITEMS=$((([regex]::Matches($hist2, [regex]::Escape($pattern))).Count) - $items)"
Write-Output "TABLE_CELL_FOUND=$($hist2.Contains('TABC3'))"
Write-Output "SOURCEROW_OK=$($hist2.Contains('"SourceRow":1'))"

$log = Get-Content "$env:APPDATA\SuperClip\error.log" -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
if ($log) { Write-Output "LOG_TAIL=" + (($log -split "`r?`n" | Select-Object -Last 6) -join " || ") } else { Write-Output "LOG_TAIL=<no error.log>" }

Write-Output "TAG=$tag"
if ($null -ne $old) { Set-Clipboard -Value $old }
