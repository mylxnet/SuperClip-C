# ASCII only. Usage: powershell -File enumwin.ps1 <pid>
$target = [uint32]$args[0]
$script:rows = @()
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class EW {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int idx);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int left, top, right, bottom; }
}
"@
$cb = [EW+EnumProc] { param($h,$l)
  $p = [uint32]0
  [void][EW]::GetWindowThreadProcessId($h, [ref]$p)
  if ($p -eq $target) {
    $t = New-Object System.Text.StringBuilder 256
    [void][EW]::GetWindowTextW($h,$t,256)
    $c = New-Object System.Text.StringBuilder 256
    [void][EW]::GetClassNameW($h,$c,256)
    $r = New-Object EW+RECT
    [void][EW]::GetWindowRect($h,[ref]$r)
    $script:rows += ("hwnd=" + $h + " vis=" + [EW]::IsWindowVisible($h) +
      " title='" + $t.ToString() + "' class='" + $c.ToString() + "'" +
      " owner=" + [EW]::GetWindow($h,4) +
      " style=" + [EW]::GetWindowLong($h,-16) +
      " exstyle=" + [EW]::GetWindowLong($h,-20) +
      " rect=" + $r.left + "," + $r.top + "," + $r.right + "," + $r.bottom)
  }
  return $true
}
$null = [EW]::EnumWindows($cb, [IntPtr]::Zero)
$script:rows | ForEach-Object { Write-Output $_ }
