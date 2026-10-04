param([string]$Src, [int]$X, [int]$Y, [int]$W, [int]$H, [int]$Scale = 3, [string]$Out = "crop.png")
Add-Type -AssemblyName System.Drawing
$src0 = New-Object System.Drawing.Bitmap($Src)
$big = New-Object System.Drawing.Bitmap(($W * $Scale), ($H * $Scale))
$g = [System.Drawing.Graphics]::FromImage($big)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.DrawImage($src0, (New-Object System.Drawing.Rectangle(0, 0, ($W * $Scale), ($H * $Scale))), (New-Object System.Drawing.Rectangle($X, $Y, $W, $H)), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose(); $src0.Dispose()
$big.Save((Join-Path (Split-Path -Parent $Src) $Out)); $big.Dispose()
Write-Output ("saved " + $Out)
