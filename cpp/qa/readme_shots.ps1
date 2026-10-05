# README screenshot driver: four shots of SuperClip's own windows, written to ../build-mingw (gitignored).
# Requires cpp/qa/mksynthetic.ps1 to have run first -- the list content in these images is typed-in demo
# data, which is the only reason they may be copied into doc/images/ and committed.
#
# Visibility rule learned the hard way (see the audit doc, section 6.1): the help window is created HIDDEN,
# so FindWindow succeeding proves nothing. Every open/close assertion below goes through IsWindowVisible.
#
# ASCII-only source: PS 5.1 decodes BOM-less UTF-8 as GBK and a CJK lead byte eats the next character.
param(
  [string]$Tag = "readme",
  [string]$Query = "Widget"
)
$ErrorActionPreference = "Continue"
$dir = Join-Path (Split-Path -Parent $PSScriptRoot) "build-mingw"
$exe = Join-Path $dir "SuperClip.exe"
if (-not (Test-Path $exe)) { Write-Output "RESULT fail_no_exe__run_build_tests_sh_first"; exit 1 }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class RS {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr after, string c, string n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, uint cb);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U U; }
  public static void Key(ushort vk, uint f) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].U.ki.vk = vk; i[0].U.ki.flags = f;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Tap(ushort vk) { Key(vk, 0); Key(vk, 2); }
  public static void Btn(uint f) {
    INPUT[] i = new INPUT[1]; i[0].type = 0; i[0].U.mi.flags = f;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Click(int x, int y) { SetCursorPos(x, y); Btn(0x0002); Btn(0x0004); }
  public static void RClick(int x, int y) { SetCursorPos(x, y); Btn(0x0008); Btn(0x0010); }
}
"@

function Get-Rect($h) { $r = New-Object RS+R; [void][RS]::GetWindowRect($h, [ref]$r); return $r }

function Save-Shot($h, $name) {
  $r = Get-Rect $h
  $w = $r.Rt - $r.L; $ht = $r.B - $r.T
  if ($w -lt 20 -or $ht -lt 20) { Write-Output ("  skip " + $name + " size " + $w + "x" + $ht); return }
  $path = Join-Path $dir ($Tag + "-" + $name + ".png")
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
  $bmp.Save($path)
  $g.Dispose(); $bmp.Dispose()
  Write-Output ("  shot " + (Split-Path -Leaf $path) + " " + $r.L + "," + $r.T + " " + $w + "x" + $ht +
                " md5=" + (Get-FileHash $path -Algorithm MD5).Hash)
}

# Grab a horizontal strip of a window (offsets in DIP from its top edge) and magnify it 2x, so small
# text -- the status bar signature, the title bar target -- can be checked by eye instead of guessed at.
function Save-Crop($h, [int]$topDip, [int]$hDip, [double]$scale, $name) {
  $r = Get-Rect $h
  $w = $r.Rt - $r.L
  if ($w -lt 20) { Write-Output ("  skip " + $name + " width " + $w); return }
  $ch = [int]($hDip * $scale)
  $y = $r.T + [int]($topDip * $scale)
  $path = Join-Path $dir ($Tag + "-" + $name + ".png")

  $strip = New-Object System.Drawing.Bitmap($w, $ch)
  $g = [System.Drawing.Graphics]::FromImage($strip)
  $g.CopyFromScreen($r.L, $y, 0, 0, (New-Object System.Drawing.Size($w, $ch)))
  $g.Dispose()

  $big = New-Object System.Drawing.Bitmap(($w * 2), ($ch * 2))
  $bg = [System.Drawing.Graphics]::FromImage($big)
  $bg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
  $bg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
  $bg.DrawImage($strip,
    (New-Object System.Drawing.Rectangle(0, 0, ($w * 2), ($ch * 2))),
    (New-Object System.Drawing.Rectangle(0, 0, $w, $ch)),
    [System.Drawing.GraphicsUnit]::Pixel)
  $big.Save($path)
  $bg.Dispose(); $big.Dispose(); $strip.Dispose()
  Write-Output ("  crop " + (Split-Path -Leaf $path) + " y=" + $y + " h=" + $ch + " -> 2x md5=" +
                (Get-FileHash $path -Algorithm MD5).Hash)
}

$sc = [RS]::FindWindowW("SuperClipMain", [NullString]::Value)
if ($sc -eq [IntPtr]::Zero) { $sc = [RS]::FindWindowW("SuperClipMain", "SuperClip") }
if ($sc -eq [IntPtr]::Zero) {
  Start-Process -FilePath $exe
  for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 200
    $sc = [RS]::FindWindowW("SuperClipMain", "SuperClip")
    if ($sc -ne [IntPtr]::Zero) { break }
  }
}
if ($sc -eq [IntPtr]::Zero) { Write-Output "RESULT fail_main_window_never_appeared"; exit 1 }

[void][RS]::ShowWindow($sc, 5)
[void][RS]::SetForegroundWindow($sc)
Start-Sleep -Milliseconds 800
if (-not [RS]::IsWindowVisible($sc)) { Write-Output "RESULT fail_main_not_visible"; exit 1 }
$dpi = [RS]::GetDpiForWindow($sc)
$s = $dpi / 96.0
$r = Get-Rect $sc
Write-Output ("main rect=" + $r.L + "," + $r.T + " size=" + ($r.Rt - $r.L) + "x" + ($r.B - $r.T) +
              " dpi=" + $dpi + " scale=" + $s)

$clientHDip = [int] (($r.B - $r.T) / $s)
Save-Shot $sc "main"
Save-Crop $sc ($clientHDip - 22) 22 $s "status"     # bottom bar: left hint text + "v2.0.3  by Mr lin"
Save-Crop $sc 0 36 $s "titlebar"                    # top strip: target bullseye + paste-mode wording

# 3) filtered view: the search box is a real EDIT child, so WM_SETTEXT drives it and the app's own
#    300ms debounce does the rest. Cleared again afterwards so the app is left in a neutral state.
$edit = [RS]::FindWindowExW($sc, [IntPtr]::Zero, "EDIT", [NullString]::Value)
if ($edit -eq [IntPtr]::Zero) {
  Write-Output "  skip search shot: no EDIT child"
} else {
  [void][RS]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, $Query)   # WM_SETTEXT
  Start-Sleep -Milliseconds 900
  Save-Shot $sc "search"
  [void][RS]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, "")
  Start-Sleep -Milliseconds 700
  Write-Output ("search cleared, main still visible=" + [RS]::IsWindowVisible($sc))
}

# 4) help window: right-click inside the list, walk the keyboard to the last menu item, confirm.
$px = [int]($r.L + 180 * $s); $py = [int]($r.T + 137 * $s)
[RS]::RClick($px, $py)
Start-Sleep -Milliseconds 600
for ($i = 0; $i -lt 6; $i++) { [RS]::Tap(0x28); Start-Sleep -Milliseconds 90 }   # VK_DOWN x6
[RS]::Tap(0x0D)                                                                  # VK_RETURN
$help = [RS]::FindWindowW("SuperClipHelp", [NullString]::Value)
$opened = $false
if ($help -ne [IntPtr]::Zero) {
  for ($i = 0; $i -lt 12; $i++) {
    Start-Sleep -Milliseconds 150
    if ([RS]::IsWindowVisible($help)) { $opened = $true; break }
  }
}
if ($opened) {
  [void][RS]::SetForegroundWindow($help)
  Start-Sleep -Milliseconds 300
  Save-Shot $help "help"
  [RS]::Tap(0x1B)                                                                # VK_ESCAPE closes help, restores main
  Start-Sleep -Milliseconds 500
  Write-Output ("help hidden after Esc=" + (-not [RS]::IsWindowVisible($help)) +
                " main visible=" + [RS]::IsWindowVisible($sc))
} else {
  Write-Output "  skip help shot: help window did not become visible"
  [RS]::Tap(0x1B)
}
Write-Output "RESULT ok"
