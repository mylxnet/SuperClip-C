Stop-Process -Name SuperClip -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600
$data = Join-Path $env:APPDATA "SuperClip"
$a = Get-Content -Raw -Encoding UTF8 (Join-Path $data "history.json") | ConvertFrom-Json
# Re-apply the three favorites the user set by hand during this session (items 1..3).
$a[0].IsFavorite = $true
$a[1].IsFavorite = $true
$a[2].IsFavorite = $true
$json = ($a | ConvertTo-Json -Compress -Depth 5)
[System.IO.File]::WriteAllText((Join-Path $data "history.json"), $json, (New-Object System.Text.UTF8Encoding($false)))
$t = Get-Content -Raw (Join-Path $data "history.json")
"len=" + $t.Length + " head=" + $t.Substring(0, 14)
"favorites=" + (@($a | Where-Object { $_.IsFavorite })).Count + " pasted=" + (@($a | Where-Object { $_.IsPasted })).Count + " items=" + $a.Count
