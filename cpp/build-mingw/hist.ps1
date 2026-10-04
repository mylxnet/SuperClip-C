$f = Join-Path $env:APPDATA "SuperClip\history.json"
$t = Get-Content -Raw -Encoding UTF8 $f
$a = ConvertFrom-Json $t
"items=" + $a.Count
"winget=" + (@($a | Where-Object { $_.Content -match "winget" })).Count
"type1(表格)=" + (@($a | Where-Object { $_.Type -eq 1 })).Count
"type0(文本)=" + (@($a | Where-Object { $_.Type -eq 0 })).Count
"favorite=" + (@($a | Where-Object { $_.IsFavorite })).Count
"pasted=" + (@($a | Where-Object { $_.IsPasted })).Count
"--- 前 6 条（显示顺序）---"
$i = 0
foreach ($x in $a) { $i++; $c = ($x.Content -replace "`r|`n", " ") ; if ($c.Length -gt 34) { $c = $c.Substring(0, 34) }; "  " + $i + " fav=" + $x.IsFavorite + " t=" + $x.Type + " | " + $c }
