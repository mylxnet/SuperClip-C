param([string]$Out)
if (-not $Out) { $Out = Join-Path $PSScriptRoot "shot6.png" }
$exe = Join-Path $PSScriptRoot "SuperClip.exe"
$existing = Get-Process SuperClip -ErrorAction SilentlyContinue
if ($existing) { $p = $existing } else { $p = Start-Process -FilePath $exe -PassThru }
Start-Sleep -Seconds 2

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
}
"@

$h = [W]::FindWindowW("SuperClipMain", "SuperClip")
if ($h -eq [IntPtr]::Zero) { Write-Output "WINDOW_NOT_FOUND"; exit 1 }
$vis0 = [W]::IsWindowVisible($h)
[void][W]::ShowWindow($h, 5)
[void][W]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 700
$r = New-Object W+R
[void][W]::GetWindowRect($h, [ref]$r)
$buf = New-Object System.Text.StringBuilder 64
[void][W]::GetWindowTextW($h, $buf, 64)
Write-Output ("hwnd=" + $h + " title='" + $buf.ToString() + "' class=SuperClipMain visibleBefore=" + $vis0)
Write-Output ("rect=" + $r.L + "," + $r.T + "," + $r.Rt + "," + $r.B + " size=" + ($r.Rt - $r.L) + "x" + ($r.B - $r.T))

$w = $r.Rt - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$bmp.Save($Out)
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved: " + $Out)
