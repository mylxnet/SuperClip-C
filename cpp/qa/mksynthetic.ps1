# Write a FULLY SYNTHETIC history.json + settings.json for the README screenshots.
# Nothing is copied from the real history: every string below is typed-in demo content, which is what
# makes the resulting PNGs safe to commit under doc/images/ (cpp/build-mingw screenshots show real
# clipboard content and are gitignored for exactly that reason).
#
# ORDER OF OPERATIONS (do not skip): cpp/qa/userdata.ps1 (backup) -> this -> app run -> screenshots
#                                     -> userdata.ps1 -Restore (compare item counts BEFORE restoring).
#
# ASCII-only source: PS 5.1 decodes BOM-less UTF-8 as GBK and a CJK lead byte eats the next character.
param(
  [string]$Tag = "synth"          # kept for log lines only
)
$ErrorActionPreference = "Stop"
$data = Join-Path $env:APPDATA "SuperClip"
$bk   = Join-Path $env:LOCALAPPDATA "Temp\sc-orig"

if (-not (Test-Path (Join-Path $bk "history.json"))) {
  Write-Output "RESULT fail_no_backup__run_userdata_ps1_first"
  exit 1
}
if (-not (Test-Path $data)) { New-Item -ItemType Directory -Force -Path $data | Out-Null }

Stop-Process -Name SuperClip -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 700

$now = Get-Date
# [Hash] is ignored on load (FromJson -> MakeItem recomputes SHA-256), so a placeholder keeps the
# file shape .NET-compatible without pretending to be a real digest.
$hex64 = ("0" * 64)

function New-Row([int]$minutesAgo, [string]$content, [int]$type, [int]$row, [int]$col,
                 [bool]$fav, [bool]$pasted) {
  [pscustomobject]@{
    Id         = [guid]::NewGuid().ToString("N")
    Content    = $content
    Type       = $type
    SourceRow  = $row
    SourceCol  = $col
    Timestamp  = $now.AddMinutes(-1 * $minutesAgo).ToString("yyyy-MM-dd'T'HH:mm:ss.fff")
    Hash       = $hex64
    IsFavorite = $fav
    IsPasted   = $pasted
  }
}

$tab = [char]9
$rows = @(
  New-Row  2 "https://example.com/docs/clipboard-api#settext"                    0 0 0 $true  $false
  New-Row  6 "Release v2.0.3 checklist: dumpbin, clean VM, tray icon, signature" 0 0 0 $true  $false
  New-Row 11 "Meeting notes: freeze non-critical merges before the Thursday cut"  0 0 0 $false $false
  New-Row 17 ("Name" + $tab + "Qty" + $tab + "Price" + "`r`n" + "Widget-A" + $tab + "12" + $tab + "3.50" + "`r`n" + "Widget-B" + $tab + "7" + $tab + "18.00") 0 0 0 $false $false
  New-Row 23 "North"        1 3 2 $false $false
  New-Row 24 "1,240"        1 3 3 $false $false
  New-Row 25 "South"        1 4 2 $false $false
  New-Row 26 "980"          1 4 3 $false $false
  New-Row 34 "git commit -m 'fix: hotkey registration on the Win7 baseline'"     0 0 0 $false $false
  New-Row 41 "=SUM(C2:C40)/COUNTA(C2:C40)"                                       0 0 0 $false $false
  New-Row 52 "10.0.5.24:5432  dbname=demo  user=readonly"                        0 0 0 $false $false
  New-Row 63 "TODO: re-check DWrite line spacing -- SetLineSpacing needs 0x0603" 0 0 0 $false $false
  New-Row 78 "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris." 0 0 0 $false $false
  New-Row 95 "Ctrl + backquote toggles the window; tray double-click is the fallback" 0 0 0 $false $true
  New-Row 121 "0755 /usr/local/bin/deploy.sh"                                    0 0 0 $false $true
)

$json = ConvertTo-Json -InputObject $rows -Compress -Depth 4
[System.IO.File]::WriteAllText((Join-Path $data "history.json"), $json, (New-Object System.Text.UTF8Encoding($false)))

# Settings: default-looking dock, normal paste mode, all-types filter, no process binding, topmost on.
Add-Type -AssemblyName System.Windows.Forms
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
$settings = [pscustomobject]@{
  Left              = [int]($vs.Right - 400)
  Top               = [int]($vs.Top + 120)
  Width             = 380
  Height            = 600
  BoundProcessName  = ""
  Topmost           = $true
  PasteMode         = 0
  SplitSingleColumn = $false
  FilterType        = 0
}
[System.IO.File]::WriteAllText((Join-Path $data "settings.json"),
  (ConvertTo-Json -InputObject $settings -Compress), (New-Object System.Text.UTF8Encoding($false)))

# Refuse to hand back control until the app can actually read what we wrote.
$roundTrip = Get-Content -Raw -Encoding UTF8 (Join-Path $data "history.json") | ConvertFrom-Json
Write-Output ("written history bytes=" + (Get-Item (Join-Path $data "history.json")).Length)
Write-Output ("written settings bytes=" + (Get-Item (Join-Path $data "settings.json")).Length)
Write-Output ("item_count=" + @($roundTrip).Count + " fav=" + @($roundTrip | Where-Object { $_.IsFavorite }).Count +
              " pasted=" + @($roundTrip | Where-Object { $_.IsPasted }).Count +
              " cells=" + @($roundTrip | Where-Object { $_.Type -eq 1 }).Count)
Write-Output ("backup untouched items=" + (@(Get-Content -Raw -Encoding UTF8 (Join-Path $bk "history.json") | ConvertFrom-Json)).Count)
Write-Output "first lines:"
foreach ($x in @($roundTrip) | Select-Object -First 4) {
  $c = ($x.Content -replace "`r|`n", " ")
  if ($c.Length -gt 46) { $c = $c.Substring(0, 46) }
  Write-Output ("  " + $x.Timestamp.Substring(11, 8) + " fav=" + $x.IsFavorite + " t=" + $x.Type + " | " + $c)
}
Write-Output "RESULT ok"
