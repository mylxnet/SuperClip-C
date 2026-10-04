param([int]$X = 1730, [int]$Y = 352, [string]$Out = "E:/qcode/superclip/cpp/build-mingw/hover.png", [int]$RegionY = -1, [int]$RegionH = 420)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class H {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out P p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(P p);
  [StructLayout(LayoutKind.Sequential)] public struct P { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
}
"@
$h = [H]::FindWindowW("SuperClipMain", "SuperClip")
if ($h -eq [IntPtr]::Zero) { Write-Output "WINDOW_NOT_FOUND"; exit 1 }
$r = New-Object H+R
[void][H]::GetWindowRect($h, [ref]$r)
# 先把光标挪到窗口外，保证目标行一定是一次"进入"（同点重复设置不产生 WM_MOUSEMOVE）
[void][H]::SetCursorPos($r.L - 300, $r.T + 60)
Start-Sleep -Milliseconds 250
# 两次移动：确保系统向窗口投递 WM_MOUSEMOVE（同点重复设置不产生消息）
[void][H]::SetCursorPos($X - 40, $Y - 6)
Start-Sleep -Milliseconds 120
[void][H]::SetCursorPos($X, $Y)
Start-Sleep -Milliseconds 120
[void][H]::SetCursorPos($X + 1, $Y)
$p = New-Object H+P
[void][H]::GetCursorPos([ref]$p)
$under = [H]::WindowFromPoint($p)
Write-Output ("main=" + $h + " cursor=" + $p.X + "," + $p.Y + " windowUnderCursor=" + $under)
# 等过 400ms 悬停延时
Start-Sleep -Milliseconds 900
$x0 = $r.L - 20
if ($RegionY -gt 0) { $y0 = $RegionY } else { $y0 = $r.T + 80 }
$w = ($r.Rt - $r.L) + 20; $ht = $RegionH
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x0, $y0, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$bmp.Save($Out)
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved: " + $Out + " region=" + $x0 + "," + $y0 + " " + $w + "x" + $ht)
