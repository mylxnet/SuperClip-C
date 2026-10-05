param(
  [string]$Probe = "",              # live state: process, window, data counts, log tail
  [string]$Backup = "",             # copy history/settings/error.log into a fresh Temp dir
  [string]$Restore = "",            # requires -ConfirmCounts; compares counts BEFORE copying
  [string]$Stop = "",               # graceful WM_CLOSE (the app really exits on WM_CLOSE)
  [string]$Start = "",              # launch the exe (default build-mingw\SuperClip.exe, see -Exe)
  [string]$Exe = "",                # override which SuperClip exe to start
  [string]$Host2 = "",              # start PasteTarget.exe with this dump path, reset its EDIT
  [string]$MenuOpen = "",           # raise main window + VK_APPS -> the app's own context menu
  [int]$MenuDown = 0,               # item index inside that menu (0-based, separators count)
  [string]$MenuPick = "",           # Down x (MenuDown+1) then Enter inside the tracked menu
  [string]$AltClick = "",           # "dx,dy" inside the host EDIT: Alt+left click, guarded
  [int]$HoldAltMs = 0,              # keep Alt physically down this long (exercises the Alt-lift path)
  [int]$SecondClick = 0,            # 1 = fire a second Alt+click immediately (300ms dedupe window)
  [string]$HotkeyRelay = "",        # raise host, then Alt+backquote (relay fallback hotkey, v2.3.3+)
  [string]$SearchText = "",         # type into the real search EDIT of SuperClipMain (debounced)
  [string]$ClearSearch = "",        # empty the search EDIT
  [string]$ToggleShow = "",         # Ctrl+` : hide or show the main window, report visibility
  [string]$SessionLock = "",        # inject WM_WTSSESSION_CHANGE(wParam=1) into SuperClipMain
  [string]$SessionUnlock = "",      # inject WM_WTSSESSION_CHANGE(wParam=2) into SuperClipMain
  [string]$Dump = "",               # ask host to write its EDIT text, print it
  [string]$Log = "",                # print tail of error.log
  [int]$LogLines = 25,
  [string]$Shot = "",               # full virtual-screen capture into build-mingw
  [switch]$ConfirmCounts,           # required for -Restore to actually copy files back
  [int]$SleepMs = 500,
  [int]$SleepSec = 0                # generic wait, prints log tail after waking
)

# ASCII-only source. Windows PowerShell 5.1 decodes BOM-less UTF-8 as GBK and a CJK lead byte
# silently eats the following character (doc/PROJECT_STATE.md 4, pitfall list).
# v2.3.0 walkthrough driver for the SWITCHLESS relay: the hook is armed by "quick mode + main
# window visible", so the checks are about arming transitions and what row gets pasted, not about
# an on/off menu item. Every click goes through a hit guard first: if the window under the cursor
# is not the synthetic host, the script aborts instead of clicking.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$dir = Join-Path (Split-Path -Parent $PSScriptRoot) "build-mingw"
$targetExe = Join-Path $dir "PasteTarget.exe"
if ($Exe -ne "") { $scExe = $Exe } else { $scExe = Join-Path $dir "SuperClip.exe" }
$data = Join-Path $env:APPDATA "SuperClip"
$histPath = Join-Path $data "history.json"
$setPath = Join-Path $data "settings.json"
$logPath = Join-Path $data "error.log"
$latestBk = Join-Path $env:LOCALAPPDATA "Temp\sc-relay-latest.txt"

Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class R {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  // Cross-process GetWindowTextW does NOT send WM_GETTEXT - it returns the cached title, which stays
  // empty after a direct WM_SETTEXT. Read EDIT content back with WM_GETTEXT (0x000D) instead.
  [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SendGetText(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMsgPtr(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RC r);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, uint cb);
  [StructLayout(LayoutKind.Sequential)] public struct RC { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int x, y; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U U; }
  public static void Key(ushort vk, uint flags) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].U.ki.vk = vk; i[0].U.ki.flags = flags;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static bool AltDown() { return (GetAsyncKeyState(0x12) & 0x8000) != 0; }
  public static void CtrlTick() { Key(0x11,0); Key(0xC0,0); Key(0xC0,2); Key(0x11,2); }
  public static void AltOem3() {   // relay fallback hotkey since v2.3.3: Alt + backquote (was Ctrl+Alt+Space)
    Key(0x12,0); Key(0xC0,0); Key(0xC0,2); Key(0x12,2);
  }
  public static void Move(int x, int y) { SetCursorPos(x, y); }
  public static void Btn(uint f) {
    INPUT[] i = new INPUT[1]; i[0].type = 0; i[0].U.mi.flags = f;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void AltClick(int x, int y) {
    SetCursorPos(x, y); Key(0x12,0); Btn(0x0002); Btn(0x0004); Key(0x12,2);
  }
  public static void Force(IntPtr h) {          // ALT tap releases the foreground lock for this thread
    Key(0x12,0); Key(0x12,2); SetForegroundWindow(h);
  }
  public static string Cls(IntPtr h) {
    StringBuilder s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString();
  }
  public static string Title(IntPtr h) {
    StringBuilder s = new StringBuilder(256); GetWindowTextW(h, s, 256); return s.ToString();
  }
}
"@

function Get-Rect($hwnd) { $r = New-Object R+RC; [void][R]::GetWindowRect($hwnd, [ref]$r); return $r }
# The process name follows the exe file name, so starting SuperClip-v230.exe yields "SuperClip-v230".
# A plain `Get-Process SuperClip` misses it (v2.3.0 round: -Start printed FAILED while pid 3972 was alive).
function Get-SC { Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'SuperClip*' } }
function Sc-Hwnd {
  $h = [R]::FindWindowW("SuperClipMain", "SuperClip")
  if ($h -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  return $h
}
function Host-Hwnd {
  $h = [R]::FindWindowW("SuperClipPasteTarget", "SuperClipPasteTarget")
  if ($h -eq [IntPtr]::Zero) { Write-Output "HOST_NOT_FOUND"; exit 1 }
  return $h
}
function Item-Count($path) {
  if (-not (Test-Path $path)) { return 0 }
  $raw = Get-Content $path -Raw -Encoding UTF8
  if ([string]::IsNullOrWhiteSpace($raw)) { return 0 }
  return ([regex]::Matches($raw, '"Id"')).Count
}
function Show-Log([int]$lines) {
  if (-not (Test-Path $logPath)) { Write-Output "LOG_MISSING"; return }
  Get-Content $logPath -Tail $lines -Encoding UTF8 | ForEach-Object { Write-Output ("  " + $_) }
}
function Save-WholeScreenShot([string]$name) {
  $vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object System.Drawing.Bitmap($vs.Width, $vs.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($vs.X, $vs.Y, 0, 0, (New-Object System.Drawing.Size($vs.Width, $vs.Height)))
  $p = Join-Path $dir $name
  $bmp.Save($p); $g.Dispose(); $bmp.Dispose()
  Write-Output ("shot " + $p + " " + $vs.Width + "x" + $vs.Height)
}
# The one guard that makes it safe to synthesize a click: resolve the point first, refuse unless the
# root window under it is the synthetic host, and refuse if a SuperClip window owns it.
function Assert-PointHitsHost([int]$x, [int]$y) {
  $p = New-Object R+PT; $p.x = $x; $p.y = $y
  $hit = [R]::WindowFromPoint($p)
  $root = [R]::GetAncestor($hit, 2)          # GA_ROOT
  if ($root -eq [IntPtr]::Zero) { Write-Output ("GUARD_FAIL root_zero x=" + $x + " y=" + $y); exit 1 }
  $cls = [R]::Cls($root)
  # Own-window test must be by process id: the synthetic host is literally classed
  # "SuperClipPasteTarget", so a class-name prefix test would reject the very window we aim at.
  $rootPid = [uint32]0
  [void][R]::GetWindowThreadProcessId($root, [ref]$rootPid)
  $scPid = (Get-SC | Select-Object -First 1 -ExpandProperty Id)
  if ($scPid -and $rootPid -eq $scPid) { Write-Output ("GUARD_FAIL hits_own_window pid=" + $rootPid + " cls=" + $cls); exit 1 }
  $hh = Host-Hwnd
  if ($root -ne $hh) { Write-Output ("GUARD_FAIL root=" + $cls + " pid=" + $rootPid + " expected=SuperClipPasteTarget"); exit 1 }
  return $root
}

if ($Probe -ne "") {
  $proc = Get-SC
  if ($proc) {
    Write-Output ("sc pid=" + ($proc.Id -join ",") + " path=" + ($proc.Path -join ","))
  } else { Write-Output "sc pid=NONE" }
  $sc = [R]::FindWindowW("SuperClipMain", "SuperClip")
  if ($sc -ne [IntPtr]::Zero) {
    $r = Get-Rect $sc
    Write-Output ("sc hwnd visible=" + [R]::IsWindowVisible($sc) + " rect=" + $r.L + "," + $r.T + "," + $r.Rt + "," + $r.B)
  } else { Write-Output "sc hwnd=NONE" }
  $hh = [R]::FindWindowW("SuperClipPasteTarget", "SuperClipPasteTarget")
  if ($hh -ne [IntPtr]::Zero) {
    $hr = Get-Rect $hh
    $ed = [R]::FindWindowExW($hh, [IntPtr]::Zero, "Edit", $null)
    $er = Get-Rect $ed
    Write-Output ("host rect=" + $hr.L + "," + $hr.T + "," + $hr.Rt + "," + $hr.B +
                  " edit=" + $er.L + "," + $er.T + "," + $er.Rt + "," + $er.B)
  } else { Write-Output "host=NONE" }
  Write-Output ("history items=" + (Item-Count $histPath) + " bytes=" + ((Get-Item $histPath -ErrorAction SilentlyContinue).Length))
  if (Test-Path $setPath) { Write-Output ("settings=" + (Get-Content $setPath -Raw -Encoding UTF8)) }
  $fg = [R]::GetForegroundWindow()
  Write-Output ("fg=" + [R]::Cls($fg) + "|" + [R]::Title($fg))
  Write-Output "log tail:"; Show-Log 12
}

if ($Backup -ne "") {
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $bk = Join-Path $env:LOCALAPPDATA ("Temp\sc-relay-" + $stamp)
  New-Item -ItemType Directory -Force -Path $bk | Out-Null
  foreach ($f in @("history.json", "settings.json", "error.log")) {
    $src = Join-Path $data $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $bk $f) -Force }
  }
  Set-Content -Path $latestBk -Value $bk -Encoding ASCII
  Write-Output ("backup dir=" + $bk)
  Write-Output ("backup items=" + (Item-Count (Join-Path $bk "history.json")))
  Write-Output ("backup md5 history=" + (Get-FileHash (Join-Path $bk "history.json") -Algorithm MD5).Hash)
}

if ($Restore -ne "") {
  $bk = $Restore
  if ($bk -eq "latest") { $bk = (Get-Content $latestBk -Raw -Encoding ASCII).Trim() }
  if (-not (Test-Path $bk)) { Write-Output ("RESTORE_ABORT missing_dir=" + $bk); exit 1 }
  $liveCount = Item-Count $histPath
  $bkCount = Item-Count (Join-Path $bk "history.json")
  Write-Output ("compare live_items=" + $liveCount + " backup_items=" + $bkCount + " dir=" + $bk)
  if (-not $ConfirmCounts) { Write-Output "RESTORE_DRYRUN pass -ConfirmCounts to actually copy"; exit 0 }
  Get-SC | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 900
  # error.log is part of the triple, so restoring it destroys the run's log evidence.
  # Snapshot the live log next to the backup before overwriting.
  if (Test-Path $logPath) {
    Copy-Item $logPath (Join-Path $bk "error.log.live-before-restore") -Force
    Write-Output ("log snapshotted to " + (Join-Path $bk "error.log.live-before-restore"))
  }
  foreach ($f in @("history.json", "settings.json", "error.log")) {
    $src = Join-Path $bk $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $data $f) -Force }
  }
  $a = (Get-FileHash (Join-Path $bk "history.json") -Algorithm MD5).Hash
  $b = (Get-FileHash $histPath -Algorithm MD5).Hash
  Write-Output ("restored items=" + (Item-Count $histPath) + " identical=" + ($a -eq $b))
}

if ($Stop -ne "") {
  $proc = Get-SC
  if (-not $proc) { Write-Output "stop: not running"; }
  else {
    $sc = [R]::FindWindowW("SuperClipMain", "SuperClip")
    if ($sc -eq [IntPtr]::Zero) { Write-Output "stop: no window, refusing to kill - abort"; exit 1 }
    [void][R]::SendMsgPtr($sc, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE -> AppContext::Exit
    $proc.WaitForExit(8000) | Out-Null
    $still = Get-SC
    Write-Output ("stop wm_close exited=" + ($null -eq $still) + " items_now=" + (Item-Count $histPath))
  }
}

if ($Start -ne "") {
  if (Get-SC) { Write-Output "start: already running"; exit 1 }
  if (-not (Test-Path $scExe)) { Write-Output ("start: exe missing " + $scExe); exit 1 }
  Start-Process -FilePath $scExe | Out-Null
  Start-Sleep -Milliseconds 1500
  $p = Get-SC | Select-Object -First 1
  if (-not $p) { Write-Output "start: FAILED"; exit 1 }
  $sc = [R]::FindWindowW("SuperClipMain", "SuperClip")
  $vi = (Get-Item $scExe).VersionInfo
  Write-Output ("start exe=" + (Split-Path -Leaf $scExe) + " fileversion=" + $vi.FileVersion +
                " pid=" + $p.Id + " hwnd_visible=" + [R]::IsWindowVisible($sc) +
                " items=" + (Item-Count $histPath))
  Write-Output "log tail:"; Show-Log 10
}

if ($Host2 -ne "") {
  if (-not (Get-Process PasteTarget -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath $targetExe -ArgumentList @($Host2) | Out-Null
    Start-Sleep -Seconds 1
  }
  $hh = Host-Hwnd
  $ed = [R]::FindWindowExW($hh, [IntPtr]::Zero, "Edit", $null)
  [void][R]::SendMessageW($ed, 0x000C, [IntPtr]::Zero, "HEAD`r`nTAIL")   # WM_SETTEXT
  [void][R]::SendMsgPtr($ed, 0x00B1, [IntPtr]4, [IntPtr]4)                # EM_SETSEL caret after HEAD
  $er = Get-Rect $ed
  Write-Output ("host pid=" + (Get-Process PasteTarget | Select-Object -First 1 -ExpandProperty Id) +
                " visible=" + [R]::IsWindowVisible($hh) + " edit=" + $er.L + "," + $er.T + "," + $er.Rt + "," + $er.B +
                " dump=" + $Host2)
}

if ($MenuOpen -ne "") {
  $sc = Sc-Hwnd
  if (-not [R]::IsWindowVisible($sc)) { [R]::CtrlTick(); Start-Sleep -Milliseconds 600 }  # raise
  if (-not [R]::IsWindowVisible($sc)) { Write-Output "MENUOPEN_STILL_HIDDEN"; exit 1 }
  [void][R]::Force($sc)
  Start-Sleep -Milliseconds 300
  $r = Get-Rect $sc
  $itemH = [System.Windows.Forms.SystemInformation]::MenuHeight
  Write-Output ("menuopen rect=" + $r.L + "," + $r.T + "," + $r.Rt + "," + $r.B + " menu_item_h=" + $itemH)
  [R]::Key(0x5D, 0); Start-Sleep -Milliseconds 80; [R]::Key(0x5D, 2)     # VK_APPS
  Start-Sleep -Milliseconds 400
  $fg = [R]::GetForegroundWindow()
  Write-Output ("vk_apps sent fg=" + [R]::Cls($fg) + " (a tracked menu keeps the owner class here)")
}

if ($MenuPick -ne "") {
  $sc = Sc-Hwnd
  for ($i = 0; $i -le $MenuDown; $i++) { [R]::Key(0x28, 0); Start-Sleep -Milliseconds 60; [R]::Key(0x28, 2); Start-Sleep -Milliseconds 60 }
  [R]::Key(0x0D, 0); Start-Sleep -Milliseconds 80; [R]::Key(0x0D, 2)
  Start-Sleep -Milliseconds 700
  Write-Output ("menupick down=" + $MenuDown + " sc_visible=" + [R]::IsWindowVisible($sc))
  Write-Output "log tail:"; Show-Log 10
}

if ($AltClick -ne "") {
  $hh = Host-Hwnd
  $ed = [R]::FindWindowExW($hh, [IntPtr]::Zero, "Edit", $null)
  $er = Get-Rect $ed
  $p = $AltClick.Split(",")
  $x = $er.L + [int]$p[0]; $y = $er.T + [int]$p[1]
  [R]::Move($x, $y)
  Start-Sleep -Milliseconds 120
  [void](Assert-PointHitsHost $x $y)                       # guard BEFORE the button goes down
  [R]::Key(0x12, 0)                                        # Alt down
  [R]::Btn(0x0002); Start-Sleep -Milliseconds 40; [R]::Btn(0x0004)
  if ($SecondClick -eq 1) {
    Start-Sleep -Milliseconds 60
    [void](Assert-PointHitsHost $x $y)
    [R]::Btn(0x0002); Start-Sleep -Milliseconds 40; [R]::Btn(0x0004)   # inside the 300ms dedupe window
  }
  if ($HoldAltMs -gt 0) { Start-Sleep -Milliseconds $HoldAltMs }
  [R]::Key(0x12, 2)                                        # Alt up
  Start-Sleep -Milliseconds $SleepMs
  Write-Output ("altclick x=" + $x + " y=" + $y + " second=" + $SecondClick +
                " hold_alt_ms=" + $HoldAltMs + " guard=OK")
  Write-Output "log tail:"; Show-Log 10
}

if ($HotkeyRelay -ne "") {
  $hh = Host-Hwnd
  [void][R]::Force($hh)
  Start-Sleep -Milliseconds 400
  $fg = [R]::GetForegroundWindow()
  [R]::AltOem3()
  Start-Sleep -Milliseconds $SleepMs
  Write-Output ("hotkey_relay fg_before=" + [R]::Cls($fg) + " fg_now=" + [R]::Cls([R]::GetForegroundWindow()))
  Write-Output "log tail:"; Show-Log 10
}

if ($SearchText -ne "" -or $ClearSearch -ne "") {
  $sc = Sc-Hwnd
  $ed = [R]::FindWindowExW($sc, [IntPtr]::Zero, "Edit", $null)
  if ($ed -eq [IntPtr]::Zero) { Write-Output "SEARCH_EDIT_NOT_FOUND"; exit 1 }
  $txt = if ($ClearSearch -ne "") { "" } else { $SearchText }
  [void][R]::SendMessageW($ed, 0x000C, [IntPtr]::Zero, $txt)   # WM_SETTEXT -> EN_CHANGE -> 300ms debounce
  Start-Sleep -Milliseconds 900
  $buf = New-Object Text.StringBuilder 512
  [void][R]::SendGetText($ed, 0x000D, [IntPtr]511, $buf)   # WM_GETTEXT, not GetWindowTextW (cached title only cross-process)
  Write-Output ("search set=[" + $buf.ToString() + "] sc_visible=" + [R]::IsWindowVisible($sc))
  Write-Output "log tail:"; Show-Log 8
}

if ($ToggleShow -ne "") {
  $sc = Sc-Hwnd
  $before = [R]::IsWindowVisible($sc)
  [R]::CtrlTick()                                               # Ctrl+` = the summon/withdraw hotkey
  Start-Sleep -Milliseconds 800
  $after = [R]::IsWindowVisible($sc)
  Write-Output ("toggle visible_before=" + $before + " visible_after=" + $after)
  Write-Output "log tail:"; Show-Log 8
}

if ($SessionLock -ne "") {
  $sc = Sc-Hwnd
  [void][R]::SendMsgPtr($sc, 0x02B1, [IntPtr]1, [IntPtr]::Zero)   # WM_WTSSESSION_CHANGE / WTS_SESSION_LOCK
  Start-Sleep -Milliseconds $SleepMs
  Write-Output "sessionlock injected"
  Write-Output "log tail:"; Show-Log 8
}

if ($SessionUnlock -ne "") {
  $sc = Sc-Hwnd
  [void][R]::SendMsgPtr($sc, 0x02B1, [IntPtr]2, [IntPtr]::Zero)   # WTS_SESSION_UNLOCK -> re-arm by state
  Start-Sleep -Milliseconds $SleepMs
  Write-Output ("sessionunlock injected sc_visible=" + [R]::IsWindowVisible($sc))
  Write-Output "log tail:"; Show-Log 8
}

if ($Dump -ne "") {
  $hh = Host-Hwnd
  [void][R]::SendMsgPtr($hh, 0x8007, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_APP+7
  Start-Sleep -Milliseconds 200
  if (Test-Path $Dump) {
    $txt = [IO.File]::ReadAllText($Dump, [Text.Encoding]::UTF8)
    Write-Output ("dumplen=" + $txt.Length + " dump=[" + $txt.Replace("`r", "\r").Replace("`n", "\n") + "]")
  } else { Write-Output ("DUMP_MISSING " + $Dump) }
}

if ($Log -ne "") { Write-Output "log tail:"; Show-Log $LogLines }

if ($Shot -ne "") { Save-WholeScreenShot $Shot }

if ($SleepSec -gt 0) {
  $t0 = Get-Date
  Write-Output ("sleeping " + $SleepSec + "s (relay idle window) from " + $t0.ToString("s"))
  Start-Sleep -Seconds $SleepSec
  Write-Output ("woke at " + (Get-Date).ToString("s"))
  Write-Output "log tail:"; Show-Log 12
}
