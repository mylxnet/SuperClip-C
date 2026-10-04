param(
  [string]$Type = "",
  [string]$EarlyShot = "",
  [string]$Shot = "",
  [string]$Region = "",
  [string]$RegionShot = "",
  [string]$Click = "",
  [string]$AbsClick = "",
  [switch]$Hotkey,
  [switch]$NoRaise,
  [int]$WaitMs = 700,
  [switch]$State,
  [switch]$PostState
)
# Comments stay ASCII-only on purpose: PowerShell 5.1 decodes BOM-less UTF-8 as GBK,
# and a trailing CJK lead byte can swallow the newline, silently merging param lines.
# artifacts (exe + screenshots) stay in ../build-mingw; only the .ps1 drivers live here
$dir = Join-Path (Split-Path -Parent $PSScriptRoot) "build-mingw"
$exe = Join-Path $dir "SuperClip.exe"
if (-not (Get-Process SuperClip -ErrorAction SilentlyContinue)) { Start-Process -FilePath $exe | Out-Null }
Start-Sleep -Seconds 2

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class K {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr after, string cls, string name);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, uint cbSize);
  [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr GetWindowLongPtrW(IntPtr h, int idx);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION U; }
  public static void Key(ushort vk, uint flags) {
    INPUT[] ins = new INPUT[1];
    ins[0].type = 1; ins[0].U.ki.vk = vk; ins[0].U.ki.flags = flags;
    SendInput(1, ins, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void CtrlTick() {           // Ctrl + `  (VK_OEM_3 = 0xC0), KEYEVENTF_KEYUP = 2
    Key(0x11, 0); Key(0xC0, 0); Key(0xC0, 2); Key(0x11, 2);
  }
  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    INPUT[] ins = new INPUT[2];
    ins[0].type = 0; ins[0].U.mi.dwFlags = 0x0002;   // LEFTDOWN
    ins[1].type = 0; ins[1].U.mi.dwFlags = 0x0004;   // LEFTUP
    SendInput(2, ins, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
}
"@

function Save-Shot($rect, $name) {
  $w = $rect.Rt - $rect.L; $ht = $rect.B - $rect.T
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($rect.L, $rect.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
  $path = Join-Path $dir $name
  $bmp.Save($path); $g.Dispose(); $bmp.Dispose()
  Write-Output ("saved " + $path + " " + $w + "x" + $ht)
}

$h = [K]::FindWindowW("SuperClipMain", "SuperClip")
if ($h -eq [IntPtr]::Zero) { Write-Output "WINDOW_NOT_FOUND"; exit 1 }
if (-not $NoRaise) {
  [void][K]::ShowWindow($h, 5)
  [void][K]::SetForegroundWindow($h)
  Start-Sleep -Milliseconds 400
}
$r = New-Object K+R; [void][K]::GetWindowRect($h, [ref]$r)
$edit = [K]::FindWindowExW($h, [IntPtr]::Zero, "EDIT", $null)

function Print-State($hwnd, $editHwnd, $rect) {
  $ex = [int64][K]::GetWindowLongPtrW($hwnd, -20)
  $sb = New-Object System.Text.StringBuilder 400
  if ($editHwnd -ne [IntPtr]::Zero) { [void][K]::GetWindowTextW($editHwnd, $sb, 400) }
  Write-Output ("visible=" + [K]::IsWindowVisible($hwnd) + " topmost=" + (($ex -band 0x8) -ne 0) +
                " rect=" + $rect.L + "," + $rect.T + "," + $rect.Rt + "," + $rect.B +
                " edit='" + $sb.ToString() + "'")
}

if ($State) { Print-State $h $edit $r }

if ($PSBoundParameters.ContainsKey('Type')) {
  if ($edit -eq [IntPtr]::Zero) { Write-Output "EDIT_NOT_FOUND"; exit 1 }
  [void][K]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, $Type)  # WM_SETTEXT
  if ($EarlyShot) { Save-Shot $r $EarlyShot }
  Start-Sleep -Milliseconds $WaitMs
}

if ($Click) {
  $p = $Click.Split(","); [K]::Click(($r.L + [int]$p[0]), ($r.T + [int]$p[1]))
  Start-Sleep -Milliseconds $WaitMs
}
if ($AbsClick) {
  $p = $AbsClick.Split(","); [K]::Click([int]$p[0], [int]$p[1])
  Start-Sleep -Milliseconds $WaitMs
}

if ($Hotkey) {
  [K]::CtrlTick()
  Start-Sleep -Milliseconds $WaitMs
}

if ($Region -and $RegionShot) {
  $p = $Region.Split(",")
  $rc = New-Object K+R
  $rc.L = [int]$p[0]; $rc.T = [int]$p[1]; $rc.Rt = ([int]$p[0] + [int]$p[2]); $rc.B = ([int]$p[1] + [int]$p[3])
  Save-Shot $rc $RegionShot
}
if ($Shot) { Save-Shot $r $Shot }
if ($PostState) { Print-State $h $edit $r }
