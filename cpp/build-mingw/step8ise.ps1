param(
  [string]$Start = "",              # launch powershell_ise.exe and report pid/hwnd/rect
  [string]$Click = "",              # click at window-relative coords (default: script pane)
  [string]$Keys = "",               # 'selallcopy' = Ctrl+A then Ctrl+C ; 'ctrlv' = Ctrl+V
  [string]$Raise = "",              # SetForegroundWindow(ISE) only
  [switch]$State,
  [switch]$Stop
)
$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class I {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, uint cb);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U U; }
  public static void Key(ushort vk, uint flags) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].U.ki.vk = vk; i[0].U.ki.flags = flags;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Combo(ushort vk) { Key(0x11,0); Key(vk,0); Key(vk,2); Key(0x11,2); }
  public static void Click(int x, int y) {
    SetCursorPos(x,y);
    INPUT[] i = new INPUT[2];
    i[0].type = 0; i[0].U.mi.flags = 0x0002;
    i[1].type = 0; i[1].U.mi.flags = 0x0004;
    SendInput(2, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
}
"@

function Ise-Hwnd {
  $p = Get-Process powershell_ise -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { return [IntPtr]::Zero }
  return $p.MainWindowHandle
}

if ($Start -ne "") {
  if (-not (Get-Process powershell_ise -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath "$env:windir\System32\WindowsPowerShell\v1.0\powershell_ise.exe" | Out-Null
    Start-Sleep -Seconds 4
  }
  $p = Get-Process powershell_ise | Select-Object -First 1
  Write-Output ("ise pid=" + $p.Id + " elevated_probe=" + ($p.StartTime.ToString("HH:mm:ss")) + " hwnd=" + $p.MainWindowHandle)
}

$h = Ise-Hwnd
if ($h -eq [IntPtr]::Zero) { Write-Output "ISE_NOT_FOUND"; exit 1 }
$r = New-Object I+R; [void][I]::GetWindowRect($h, [ref]$r)

if ($Raise -ne "") {
  if ([I]::IsIconic($h)) { [void][I]::ShowWindow($h, 9) }
  [I]::Combo(0x12)      # ALT tap releases the foreground lock
  [void][I]::SetForegroundWindow($h)
  Start-Sleep -Milliseconds 300
}
if ($Click -ne "") {
  $p2 = $Click.Split(",")
  [I]::Click(($r.L + [int]$p2[0]), ($r.T + [int]$p2[1]))
  Start-Sleep -Milliseconds 300
}
if ($Keys -eq "selallcopy") {
  [I]::Combo(0x41); Start-Sleep -Milliseconds 250   # Ctrl+A
  [I]::Combo(0x43); Start-Sleep -Milliseconds 350   # Ctrl+C
}
if ($Keys -eq "ctrlv") { [I]::Combo(0x56); Start-Sleep -Milliseconds 350 }

if ($State) {
  $sb = New-Object System.Text.StringBuilder 256
  [void][I]::GetWindowTextW($h, $sb, 256)
  Write-Output ("ise hwnd=" + $h + " rect=" + $r.L + "," + $r.T + "," + $r.Rt + "," + $r.B +
                " iconic=" + [I]::IsIconic($h) + " title='" + $sb.ToString() + "'")
}

if ($Stop) {
  $p = Get-Process powershell_ise -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($p) { Stop-Process -Id $p.Id -Force; Write-Output ("ise stopped pid=" + $p.Id) }
  else { Write-Output "ise already gone" }
}
