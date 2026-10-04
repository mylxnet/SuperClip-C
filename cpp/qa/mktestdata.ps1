$bk = Join-Path $env:LOCALAPPDATA "Temp\sc-orig"
$data = Join-Path $env:APPDATA "SuperClip"
$a = Get-Content -Raw -Encoding UTF8 (Join-Path $bk "history.json") | ConvertFrom-Json
# mark three items as pasted (gray) and two as table cells for the filter test
$a[5].IsPasted = $true
$a[6].IsPasted = $true
$a[9].IsPasted = $true
$a[7].Type = 1; $a[7].SourceRow = 3; $a[7].SourceCol = 2
$a[8].Type = 1; $a[8].SourceRow = 1; $a[8].SourceCol = 4
Stop-Process -Name SuperClip -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600
$json = ($a | ConvertTo-Json -Compress -Depth 5)
[System.IO.File]::WriteAllText((Join-Path $data "history.json"), $json, (New-Object System.Text.UTF8Encoding($false)))
"written bytes=" + (Get-Item (Join-Path $data "history.json")).Length
"expected order after Reset (fav partition desc, then non-fav desc):"
$fav = @($a | Where-Object { $_.IsFavorite }) | Sort-Object { [datetime]$_.Timestamp } -Descending
$rest = @($a | Where-Object { -not $_.IsFavorite }) | Sort-Object { [datetime]$_.Timestamp } -Descending
$i = 0
foreach ($x in ($fav + $rest)) { $i++; $c = ($x.Content -replace "`r|`n", " "); if ($c.Length -gt 26) { $c = $c.Substring(0, 26) }
  "  " + $i + " " + $x.Timestamp.Substring(11,8) + " paste=" + $x.IsPasted + " t=" + $x.Type + " | " + $c }
