$exe = Join-Path $PSScriptRoot "SuperClip.exe"
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W2 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr c, string cl, string wn);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static System.Collections.ArrayList All = new System.Collections.ArrayList();
  public static bool Cb(IntPtr h, IntPtr l) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    var sb = new StringBuilder(256); GetClassNameW(h, sb, 256);
    var tb = new StringBuilder(256); GetWindowTextW(h, tb, 256);
    All.Add(new object[]{ h, pid, sb.ToString(), tb.ToString(), IsWindowVisible(h) });
    return true;
  }
}
"@
$proc = Start-Process -FilePath $exe -PassThru
Write-Output ("pid=" + $proc.Id)
for ($i = 1; $i -le 8; $i++) {
  Start-Sleep -Milliseconds 500
  $alive = [bool](Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)
  $h = [W2]::FindWindowW("SuperClipMain", $null)
  Write-Output ("t=" + ($i * 0.5) + "s alive=" + $alive + " hwnd=" + $h)
  if (-not $alive) { Write-Output ("exitcode=" + $proc.ExitCode); break }
}
[W2]::All.Clear() | Out-Null
$cb = [W2+EnumProc] { param($h, $l) return [W2]::Cb($h, $l) }
[void][W2]::EnumWindows($cb, [IntPtr]::Zero)
[W2]::All | Where-Object { $_[3] -like "*SuperClip*" -or $_[2] -like "*SuperClip*" } | ForEach-Object {
  Write-Output ("win hwnd=" + $_[0] + " pid=" + $_[1] + " class=" + $_[2] + " title=" + $_[3] + " visible=" + $_[4])
}
Stop-Process -Name SuperClip -Force -ErrorAction SilentlyContinue
