param([int]$W = 340, [int]$H = 500, [string]$Out = "E:/qcode/superclip/cpp/build-mingw/resized.png")
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Rz {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool repaint);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
}
"@
# PowerShell 变量名大小写不敏感：绝不能把句柄赋给 $h，否则覆盖参数 $H
$hwnd = [Rz]::FindWindowW("SuperClipMain", "SuperClip")
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "WINDOW_NOT_FOUND"; exit 1 }
$r = New-Object Rz+R
[void][Rz]::GetWindowRect($hwnd, [ref]$r)
[void][Rz]::MoveWindow($hwnd, $r.L, $r.T, $W, $H, $true)
Start-Sleep -Milliseconds 700
$r2 = New-Object Rz+R
[void][Rz]::GetWindowRect($hwnd, [ref]$r2)
Write-Output ("after=" + ($r2.Rt - $r2.L) + "x" + ($r2.B - $r2.T))
$w = $r2.Rt - $r2.L; $ht = $r2.B - $r2.T
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r2.L, $r2.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$bmp.Save($Out)
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved: " + $Out)
