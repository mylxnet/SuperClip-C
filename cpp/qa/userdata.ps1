param([switch]$Restore)
$data = Join-Path $env:APPDATA "SuperClip"
$bk = Join-Path $env:LOCALAPPDATA "Temp\sc-orig"
if (-not $Restore) {
  New-Item -ItemType Directory -Force -Path $bk | Out-Null
  Copy-Item (Join-Path $data "history.json") (Join-Path $bk "history.json") -Force
  Copy-Item (Join-Path $data "settings.json") (Join-Path $bk "settings.json") -Force
  Write-Output ("backup md5: " + (Get-FileHash (Join-Path $bk "history.json") -Algorithm MD5).Hash)
  Get-ChildItem $bk | ForEach-Object { Write-Output ("backup file: " + $_.Name + " " + $_.Length) }
} else {
  Stop-Process -Name SuperClip -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 800
  Copy-Item (Join-Path $bk "history.json") (Join-Path $data "history.json") -Force
  Copy-Item (Join-Path $bk "settings.json") (Join-Path $data "settings.json") -Force
  $a = (Get-FileHash (Join-Path $bk "history.json") -Algorithm MD5).Hash
  $b = (Get-FileHash (Join-Path $data "history.json") -Algorithm MD5).Hash
  Write-Output ("restored identical: " + ($a -eq $b))
}
