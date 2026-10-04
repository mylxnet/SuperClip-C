# Writes %APPDATA%\SuperClip\settings.json for the step-10 QA sweep (ASCII only: PS 5.1
# decodes BOM-less UTF-8 as GBK, a CJK lead byte can eat the newline and drop parameters).
# History data is a separate concern -- back it up with userdata.ps1 before any of this.
param(
  [string]$Case = "show",           # del | rect | off | bind | show
  [string]$BoundName = ""           # empty => BoundProcessName null, else the quoted name
)
$ErrorActionPreference = "Stop"
$path = Join-Path (Join-Path $env:APPDATA "SuperClip") "settings.json"

$bv = if ($BoundName -eq "") { "null" } else { '"' + $BoundName + '"' }

function Put([string]$json) {
  [IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))
}

switch ($Case) {
  # no settings.json at all => first-run path (defaults: dock right, topmost on, normal mode)
  "del" { if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force } }
  # a stored rect + Quick mode + Text filter + Topmost OFF, all must come back on restart
  "rect" {
    Put ('{"Left":100,"Top":120,"Width":400,"Height":520,"BoundProcessName":' + $bv +
         ',"Topmost":false,"PasteMode":1,"SplitSingleColumn":false,"FilterType":1}')
  }
  # rect far outside every monitor (the "monitor was unplugged" case) => must fall back to dock
  "off" {
    Put ('{"Left":9000,"Top":7000,"Width":380,"Height":600,"BoundProcessName":' + $bv +
         ',"Topmost":true,"PasteMode":0,"SplitSingleColumn":false,"FilterType":0}')
  }
  # binding restore probe: exact rect we can assert against, topmost on, normal mode
  "bind" {
    Put ('{"Left":200,"Top":200,"Width":380,"Height":600,"BoundProcessName":' + $bv +
         ',"Topmost":true,"PasteMode":0,"SplitSingleColumn":false,"FilterType":0}')
  }
  default { }
}

if (Test-Path -LiteralPath $path) {
  Write-Output ("settings.json = " + (Get-Content -Raw -LiteralPath $path))
} else {
  Write-Output "settings.json = <absent>"
}
