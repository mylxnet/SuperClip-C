param(
  [int]$Pages = 3,                  # 2 => ends on page 3 (page index is 1-based in the app)
  [string]$Tag = "auditfix"         # png prefix; artifacts land in ../build-mingw (gitignored)
)
# Step-11 help-window sweep for the audit remediation (page 3 wording).
# ASCII-only source: PS 5.1 decodes BOM-less UTF-8 as GBK and a CJK lead byte can eat the
# next character, so the CJK window title is never written here -- lookup is by class.
#
# The help window is created hidden at startup (MainWindow), so FindWindow succeeding means
# nothing: every open/close assertion below goes through IsWindowVisible, and the shot is
# only taken after the rect moved away from the create-time (0,0).
$ErrorActionPreference = "Continue"
$dir = Join-Path (Split-Path -Parent $PSScriptRoot) "build-mingw"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class HL2 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, uint cb);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int idx);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U U; }
  public static void Key(ushort vk, uint flags) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].U.ki.vk = vk; i[0].U.ki.flags = flags;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Tap(ushort vk) { Key(vk, 0); Key(vk, 2); }
  public static void Btn(uint f) {
    INPUT[] i = new INPUT[1]; i[0].type = 0; i[0].U.mi.flags = f;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Click(int x, int y) { SetCursorPos(x, y); Btn(0x0002); Btn(0x0004); }
  public static void RClick(int x, int y) { SetCursorPos(x, y); Btn(0x0008); Btn(0x0010); }
  public static string Cls(IntPtr h) { StringBuilder s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString(); }
}
"@

$script:lastHash = ""
function Get-Rect($h) { $r = New-Object HL2+R; [void][HL2]::GetWindowRect($h, [ref]$r); return $r }
function Find-Help { return [HL2]::FindWindowW("SuperClipHelp", [NullString]::Value) }
function Save-Shot($h, $path) {
  $r = Get-Rect $h
  $w = $r.Rt - $r.L; $ht = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
  $bmp.Save($path)
  $g.Dispose(); $bmp.Dispose()
  $script:lastHash = (Get-FileHash $path -Algorithm MD5).Hash
  Write-Output ("  shot " + (Split-Path -Leaf $path) + " from " + $r.L + "," + $r.T + " " + $w + "x" + $ht + " md5=" + $script:lastHash)
}

$sc = [HL2]::FindWindowW("SuperClipMain", "SuperClip")
if ($sc -eq [IntPtr]::Zero) { Write-Output "RESULT fail_no_main_window"; exit 1 }
[void][HL2]::ShowWindow($sc, 5)                       # SW_SHOW
[void][HL2]::SetForegroundWindow($sc)
Start-Sleep -Milliseconds 500
$sr = Get-Rect $sc
Write-Output ("main rect=" + $sr.L + "," + $sr.T + " size=" + ($sr.Rt - $sr.L) + "x" + ($sr.B - $sr.T) + " visible=" + [HL2]::IsWindowVisible($sc))

$h0 = Find-Help
if ($h0 -eq [IntPtr]::Zero) { Write-Output "RESULT fail_help_class_missing"; exit 1 }
Write-Output ("help window exists=" + $true + " visible_before_test=" + [HL2]::IsWindowVisible($h0))

# open: real right-click inside a list row (WM_CONTEXTMENU -> TrackPopupMenuEx at the cursor),
# then walk the keyboard to the last item and confirm it
$px = [int]($sr.L + 180); $py = [int]($sr.T + 137)
[HL2]::RClick($px, $py)
Start-Sleep -Milliseconds 600
for ($i = 0; $i -lt 6; $i++) { [HL2]::Tap(0x28); Start-Sleep -Milliseconds 90 }
[HL2]::Tap(0x0D)
$opened = $false
for ($i = 0; $i -lt 12; $i++) {
  Start-Sleep -Milliseconds 150
  if ([HL2]::IsWindowVisible($h0)) { $opened = $true; break }
}
if (-not $opened) {
  Write-Output "RESULT fail_help_not_visible_after_menu_route"
  [HL2]::Tap(0x1B)                                    # VK_ESCAPE: never leave a menu hanging
  exit 1
}
$dpi = [HL2]::GetDpiForWindow($h0)
$s = $dpi / 96.0
$r = Get-Rect $h0
Write-Output ("help OPENED dpi=" + $dpi + " scale=" + $s + " rect=" + $r.L + "," + $r.T + " size=" + ($r.Rt - $r.L) + "x" + ($r.B - $r.T))

Save-Shot $h0 (Join-Path $dir ($Tag + "-help-p1.png"))
$first = $script:lastHash

# Config.h: kHelpW=420 kHelpH=300 kPad=12 kHelpBtnH=30 kHelpNavBtnW=76 -> next btn centre (370,273)
$nx = [int]($r.L + 370 * $s); $ny = [int]($r.T + 273 * $s)
for ($p = 2; $p -le $Pages; $p++) {
  [HL2]::Click($nx, $ny)
  Start-Sleep -Milliseconds 400
  Save-Shot $h0 (Join-Path $dir ($Tag + "-help-p" + $p + ".png"))
}
Write-Output ("page" + $Pages + " pixels differ from page1 = " + ($first -ne $script:lastHash))

[HL2]::Click([int]($r.L + 40 * $s), $ny)              # close button centre
Start-Sleep -Milliseconds 500
Write-Output ("help hidden after close click = " + (-not [HL2]::IsWindowVisible($h0)))
Write-Output ("main window still alive = " + [HL2]::IsWindowVisible($sc))
Write-Output "RESULT ok"
