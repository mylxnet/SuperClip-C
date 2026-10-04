# 截取 SuperClip 主窗客户区，存 PNG（供模型自查绘制结果）
param([string]$Out = "")
if (-not $Out) { $Out = Join-Path $PSScriptRoot "shot.png" }
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c,string t);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public struct RECT { public int Left,Top,Right,Bottom; }
}
'@
$hwnd = [W]::FindWindowW("SuperClipMain","SuperClip")
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "NO_WINDOW"; exit 1 }
$rc = New-Object W+RECT
[void][W]::GetWindowRect($hwnd, [ref]$rc)
$w = $rc.Right - $rc.Left
$hh = $rc.Bottom - $rc.Top
Write-Output "RECT=$($rc.Left),$($rc.Top),$w,$hh VISIBLE=$([W]::IsWindowVisible($hwnd))"
$bmp = New-Object System.Drawing.Bitmap($w, $hh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($rc.Left, $rc.Top, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$bmp.Save($Out)
$g.Dispose(); $bmp.Dispose()
Write-Output "SAVED=$Out"
