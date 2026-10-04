param([string]$Src, [int]$Y0, [int]$Y1, [int]$X0 = 60, [int]$X1 = 300)
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($Src)
$best = $null
for ($y = $Y0; $y -le $Y1; $y++) {
  for ($x = $X0; $x -le $X1; $x++) {
    $c = $bmp.GetPixel($x, $y)
    $lum = [int]$c.R + [int]$c.G + [int]$c.B
    if ($null -eq $best -or $lum -lt $best.Lum) { $best = [pscustomobject]@{ Lum = $lum; X = $x; Y = $y; R = [int]$c.R; G = [int]$c.G; B = [int]$c.B } }
  }
}
$bmp.Dispose()
Write-Output ("darkest in y=" + $Y0 + ".." + $Y1 + " : rgb(" + $best.R + "," + $best.G + "," + $best.B + ") at " + $best.X + "," + $best.Y)
