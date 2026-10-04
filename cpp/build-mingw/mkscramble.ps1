Stop-Process -Name SuperClip -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600
$data = Join-Path $env:APPDATA "SuperClip"
$a = Get-Content -Raw -Encoding UTF8 (Join-Path $data "history.json") | ConvertFrom-Json
$rot = New-Object System.Collections.ArrayList
for ($k = 5; $k -lt $a.Count; $k++) { [void]$rot.Add($a[$k]) }
for ($k = 0; $k -lt 5; $k++) { [void]$rot.Add($a[$k]) }
$json = ($rot | ConvertTo-Json -Compress -Depth 5)
[System.IO.File]::WriteAllText((Join-Path $data "history.json"), $json, (New-Object System.Text.UTF8Encoding($false)))
$t = Get-Content -Raw (Join-Path $data "history.json")
"len=" + $t.Length + " head=" + $t.Substring(0, 20)
"scrambled head (should NOT be time-desc):"
for ($k = 0; $k -lt 8; $k++) {
  $x = $rot[$k]
  $c = ($x.Content -replace "`r|`n", " ")
  if ($c.Length -gt 26) { $c = $c.Substring(0, 26) }
  "  " + ($k + 1) + " " + $x.Timestamp.Substring(11, 8) + " fav=" + $x.IsFavorite + " | " + $c
}
