# Build a synthetic history.json for the UI walkthrough (real data is backed up in s10bak).
# ASCII-only source strings: PS 5.1 decodes BOM-less UTF-8 scripts as GBK, CJK literals would garble.
# 2 favorites + 2 table cells (one pasted) + 1 plain text.
param([string]$Out = "")
if ($Out -eq "") { $Out = Join-Path $env:APPDATA "SuperClip\history.json" }

function Hash($s) {
  $sha = [Security.Cryptography.SHA256]::Create()
  ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)) | ForEach-Object { $_.ToString("x2") }) -join ""
}
function Item($content, $type, $row, $col, $fav, $pasted, $ts) {
  [pscustomobject]@{
    Id = [Guid]::NewGuid().ToString("D")
    Content = $content
    Type = $type
    SourceRow = $row
    SourceCol = $col
    Timestamp = $ts
    Hash = (Hash $content)
    IsFavorite = $fav
    IsPasted = $pasted
  }
}

$items = @(
  (Item "Q1-400.00"                 1 1 2 $false $false "2026-10-04T10:05:01.000"),
  (Item "Q2-12.50"                  1 2 3 $false $true  "2026-10-04T10:05:02.000"),
  (Item "FAV-A-hidden-in-All"       0 $null $null $true $false "2026-10-04T10:05:03.000"),
  (Item "PLAIN-no-blue-label"       0 $null $null $false $false "2026-10-04T10:05:04.000"),
  (Item "FAV-B-fav-view-only"       0 $null $null $true $false "2026-10-04T10:05:05.000")
)

$json = ($items | ConvertTo-Json -Compress)
if (-not $json.StartsWith("[")) { Write-Output "NOT_AN_ARRAY_ABORT"; exit 1 }
$enc = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($Out, $json, $enc)
Write-Output ("wrote " + $Out + " bytes=" + (Get-Item $Out).Length + " items=" + $items.Count)
