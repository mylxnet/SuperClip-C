param([int]$Wheel = 0, [string]$Out = "shot_b.png")
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $dir "SuperClip.exe"
$existing = Get-Process SuperClip -ErrorAction SilentlyContinue
if (-not $existing) { Start-Process -FilePath $exe | Out-Null }
Start-Sleep -Seconds 2

Add-Type -AssemblyName System.Drawing,System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public class S {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, uint cbSize);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L,T,Rt,B; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION U; }
  public static void Wheel(int clicks) {
    INPUT[] ins = new INPUT[1];
    ins[0].type = 0;
    ins[0].U.mi.dx = 0; ins[0].U.mi.dy = 0;
    ins[0].U.mi.mouseData = unchecked((uint)(-120 * clicks));
    ins[0].U.mi.dwFlags = 0x0800;   // MOUSEEVENTF_WHEEL
    for (int i = 0; i < 12; i++) { SendInput(1, ins, (uint)System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT))); }
  }
}
"@
$h = [S]::FindWindowW("SuperClipMain", "SuperClip")
if ($h -eq [IntPtr]::Zero) { Write-Output "WINDOW_NOT_FOUND"; exit 1 }
[void][S]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400

if ($Wheel -gt 0) {
  [void][S]::SetCursorPos(1700, 500)
  Start-Sleep -Milliseconds 200
  [S]::Wheel(1)
}
Start-Sleep -Milliseconds 400

$r = New-Object S+R; [void][S]::GetWindowRect($h, [ref]$r)
$w = $r.Rt - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$path = Join-Path $dir $Out
$bmp.Save($path); $g.Dispose(); $bmp.Dispose()
Write-Output ("wheel=" + $Wheel + " saved " + $path + " " + $w + "x" + $ht)
