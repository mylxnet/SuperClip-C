param(
  [string]$Start = "",              # launch SuperClip.exe if not running (data must be backed up first)
  [string]$Host2 = "",              # start PasteTarget.exe with this dump path
  [string]$Copy = "",               # write text to clipboard (creates a history item)
  [string]$Reset = "",              # reset host EDIT text to HEAD/CRLF/TAIL with caret after HEAD
  [string]$Pick = "",               # real click on the toolbar bullseye -> start/stop picking
  [string]$ClickAt = "",            # single real click at SuperClip client coords (never double: that pastes)
  [string]$RaiseHost = "",          # bring the host to the foreground so a click at its centre
                                    # really lands on it (other windows may cover that point)
  [string]$ClickHost = "",          # real click at the host window centre (completes the pick)
  [string]$Move = "",               # move the mouse to screen coords (forces WM_SETCURSOR, so the
                                    # live cursor handle is re-evaluated) without clicking
  [string]$Dbl = "",                # double click at SuperClip client coords (normal-mode paste)
  [string]$Hotkey = "",             # Ctrl+` : raise/lower SuperClip (also cancels picking)
  [string]$Wts = "",                # post WM_WTSSESSION_CHANGE with this wParam (1 = session lock)
  [string]$EndSession = "",         # post WM_ENDSESSION with this wParam (1 = session ending)
  [string]$WaitPick = "",           # sleep this many ms so the 8s pick timeout can fire
  [string]$CloseHost = "",          # close the host window (bound window goes dead -> R5)
  [string]$Dump = "",               # ask host to write its text to this path, print it
  [string]$Clicks = "",             # print the host mouse-key counter (T2 evidence)
  [string]$Shot = "",               # screenshot the SuperClip window
  [string]$Search = "",             # set the search EDIT text (U:hhhh,hhhh form keeps this script ASCII)
  [string]$Topmost = "",            # print WS_EX_TOPMOST bit + whether any window sits above us
  [string]$CursorShot = "",         # screenshot the live system cursor (32x32)
  [string]$Close = "",              # post WM_CLOSE to SuperClip -> graceful exit (Shutdown chain
                                    # persists settings geometry + history; SendInput click would need
                                    # screen coords and could hit whatever window covers us)
  [string]$RightClick = "",         # real right click at SuperClip client coords -> opens the step-11
                                    # main menu; the app thread then BLOCKS inside TrackPopupMenuEx,
                                    # so this verb must be followed by MenuClick / MenuKey / MenuEsc
  [string]$MenuDump = "",           # find the live "#32768" popup, print its rect + every item text
  [string]$MenuClick = "",          # real click on the nth menu item BY POSITION (separator counts)
  [string]$MenuKey = "",            # space separated VK codes sent to the menu, e.g. "40 40 13"
  [string]$WinState = "",           # visible / enabled / rect / exstyle of a window class (any app)
  [string]$ClickScreen = "",        # real click at raw screen coords "x,y" (help window buttons)
  [string]$WinShot = "",            # "ClassName,file.png" -> screenshot that top-level window
  [string]$WinKey = "",             # "ClassName|vk vk vk" -> focus that window then send the keys
                                    # NOTE: verb blocks fire in SOURCE order, not command-line
                                    # order, so WinKey is defined before WinShot on purpose
  [switch]$State,
  [int]$WaitMs = 450
)
# ASCII-only comments: PS 5.1 decodes BOM-less UTF-8 as GBK, a CJK lead byte can eat the newline
# and silently merge param lines (parameters then vanish without any error).
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$scExe = Join-Path $dir "SuperClip.exe"
$targetExe = Join-Path $dir "PasteTarget.exe"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class N9 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMsgPtr(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", EntryPoint="PostMessageW", CharSet=CharSet.Unicode)] public static extern bool PostMsgStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool PostMsgPtr(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int idx);   // GWL_EXSTYLE = -20
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);   // GW_HWNDPREV = 3
  [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuStringW(IntPtr h, uint id, StringBuilder s, int n, uint flag);
  [DllImport("user32.dll")] public static extern bool GetMenuItemRect(IntPtr owner, IntPtr hMenu, uint item, out R rc);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorInfo(out CI ci);
  [DllImport("user32.dll")] public static extern bool GetIconInfo(IntPtr h, out II ii);
  [DllImport("user32.dll")] public static extern bool DrawIconEx(IntPtr dc, int x, int y, IntPtr h, int w, int ht, uint step, IntPtr br, uint flags);
  [DllImport("user32.dll", EntryPoint="LoadCursorW")] public static extern IntPtr LoadCursorRes(IntPtr i, IntPtr n);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, uint cb);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, uint[] pid);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateDIBSection(IntPtr dc, ref BI bi, uint u, out IntPtr bits, IntPtr sec, uint off);
  [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
  [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct CI { public int cbSize; public uint flags; public IntPtr hcursor; public int x, y; }
  [StructLayout(LayoutKind.Sequential)] public struct II { public uint f; public int xHot, yHot; public IntPtr bm, color; }
  [StructLayout(LayoutKind.Sequential)] public struct BI {
    public int size; public int w, ht; public short planes, bits; public int comp, usable; public int sizeImg;
    public int xppm, yppm; public uint colorsUsed, colorsImportant;
  }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U U; }
  public static void Key(ushort vk, uint flags) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].U.ki.vk = vk; i[0].U.ki.flags = flags;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void CtrlTick() { Key(0x11,0); Key(0xC0,0); Key(0xC0,2); Key(0x11,2); }
  public static void Btn(uint f) {
    INPUT[] i = new INPUT[1]; i[0].type = 0; i[0].U.mi.flags = f;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Click(int x, int y) { SetCursorPos(x, y); Btn(0x0002); Btn(0x0004); }
  public static void RClick(int x, int y) { SetCursorPos(x, y); Btn(0x0008); Btn(0x0010); }
  public static void MoveBy(int dx, int dy) {
    INPUT[] i = new INPUT[1]; i[0].type = 0; i[0].U.mi.dx = dx; i[0].U.mi.dy = dy; i[0].U.mi.flags = 0x0001;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void DoubleClick(int x, int y) { SetCursorPos(x, y); Btn(0x0002); Btn(0x0004); Btn(0x0002); Btn(0x0004); }
  public static string Cls(IntPtr h) { StringBuilder s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString(); }
  public static string Title(IntPtr h) { StringBuilder s = new StringBuilder(256); GetWindowTextW(h, s, 256); return s.ToString(); }
}
"@

function Rect($hwnd) { $r = New-Object N9+R; [void][N9]::GetWindowRect($hwnd, [ref]$r); return $r }
function Sc-Hwnd {
  $h = [N9]::FindWindowW("SuperClipMain", "SuperClip")
  if ($h -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  return $h
}
function Host-Hwnd {
  $h = [N9]::FindWindowW("SuperClipPasteTarget", "SuperClipPasteTarget")
  if ($h -eq [IntPtr]::Zero) { Write-Output "HOST_NOT_FOUND"; exit 1 }
  return $h
}
# Toolbar geometry from src/core/Config.h (logical px): pad 12, search 26, buttons y 78, pick 72 wide
function Pick-Centre($hwnd) {
  $dpi = [N9]::GetDpiForWindow($hwnd)
  if ($dpi -eq 0) { $dpi = 96 }
  $r = Rect $hwnd
  $xDip = 12 + (60 + 8) + (56 + 8) + (56 + 8) + 36      # centres of filter/clear/reset then half of pick
  $yDip = 36 + 8 + 26 + 8 + 14
  $x = $r.L + [int][Math]::Round($xDip * $dpi / 96)
  $y = $r.T + [int][Math]::Round($yDip * $dpi / 96)
  return ,@($x, $y, $dpi, $xDip, $yDip)
}
function Menu-Hwnd {
  # "#32768" is the registered class of every tracked popup menu. While TrackPopupMenuEx runs the
  # app thread is blocked in its modal loop, so this is the only external handle we can act on.
  $h = [IntPtr]::Zero
  while ($true) {
    $h = [N9]::FindWindowExW([IntPtr]::Zero, $h, "#32768", [NullString]::Value)
    if ($h -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
    if ([N9]::IsWindowVisible($h)) { return $h }
  }
}
function Menu-HMENU($hwnd) { return [N9]::SendMsgPtr($hwnd, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero) }  # MN_GETHMENU
function Save-Cursor($path) {
  $ci = New-Object N9+CI
  $ci.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($ci)
  if (-not [N9]::GetCursorInfo([ref]$ci)) { Write-Output "CURSOR_QUERY_FAILED"; return }
  $dc = [N9]::CreateCompatibleDC([N9]::GetDC([IntPtr]::Zero))
  $bi = New-Object N9+BI
  $bi.size = [Runtime.InteropServices.Marshal]::SizeOf($bi)
  $bi.w = 32; $bi.ht = -32; $bi.planes = 1; $bi.bits = 32; $bi.comp = 0; $bi.usable = 0
  $bits = [IntPtr]::Zero
  $bmp = [N9]::CreateDIBSection($dc, [ref]$bi, 0, [ref]$bits, [IntPtr]::Zero, 0)
  [void][N9]::SelectObject($dc, $bmp)
  [void][N9]::DrawIconEx($dc, 0, 0, $ci.hcursor, 32, 32, 0, [IntPtr]::Zero, 3)
  $bmp2 = [System.Drawing.Image]::FromHbitmap($bmp)
  $bmp2.Save($path); $bmp2.Dispose()
  Write-Output ("cursor hcursor=" + $ci.hcursor + " pos=" + $ci.x + "," + $ci.y + " shot=" + $path)
}

if ($Start -ne "") {
  if (-not (Get-Process SuperClip -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath $scExe | Out-Null
    Start-Sleep -Seconds 2
  }
  $p = Get-Process SuperClip -ErrorAction SilentlyContinue
  $h = Sc-Hwnd
  $r = Rect $h
  Write-Output ("sc started pid=" + $p.Id + " visible=" + [N9]::IsWindowVisible($h) +
                " rect=" + $r.L + "," + $r.T + "," + $r.Rt + "," + $r.B + " dpi=" + [N9]::GetDpiForWindow($h))
}

if ($Host2 -ne "") {
  if (-not (Get-Process PasteTarget -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath $targetExe -ArgumentList @($Host2) | Out-Null
    Start-Sleep -Seconds 1
  }
  $hh = Host-Hwnd
  $r = Rect $hh
  Write-Output ("host started pid=" + (Get-Process PasteTarget | Select-Object -First 1 -ExpandProperty Id) +
                " rect=" + $r.L + "," + $r.T + "," + $r.Rt + "," + $r.B)
}

if ($Copy -ne "") { Set-Clipboard -Value $Copy; Start-Sleep -Milliseconds $WaitMs }

if ($Reset -ne "") {
  $hh = Host-Hwnd
  $ed = [N9]::FindWindowExW($hh, [IntPtr]::Zero, "Edit", $null)
  [void][N9]::SendMessageW($ed, 0x000C, [IntPtr]::Zero, "HEAD`r`nTAIL")   # WM_SETTEXT
  [void][N9]::SendMsgPtr($ed, 0x00B1, [IntPtr]4, [IntPtr]4)               # EM_SETSEL (integer lParam!)
  Start-Sleep -Milliseconds 200
  Write-Output "host text reset"
}

if ($Pick -ne "") {
  $h = Sc-Hwnd
  $pc = Pick-Centre $h
  $visBefore = [N9]::IsWindowVisible($h)
  [N9]::Click($pc[0], $pc[1])
  Start-Sleep -Milliseconds 350
  $ci = New-Object N9+CI; $ci.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($ci)
  [void][N9]::GetCursorInfo([ref]$ci)
  $arrow = [N9]::LoadCursorRes([IntPtr]::Zero, [IntPtr]32512)   # MAKEINTRESOURCE(IDC_ARROW)
  Write-Output ("pick click at " + $pc[0] + "," + $pc[1] + " (dip " + $pc[3] + "x" + $pc[4] +
                " dpi " + $pc[2] + ") visible=" + $visBefore + "->" + [N9]::IsWindowVisible($h) +
                " cursor=" + $ci.hcursor + " arrowH=" + $arrow)
}

if ($ClickAt -ne "") {
  $h = Sc-Hwnd
  $r = Rect $h
  $p = $ClickAt.Split(",")
  [N9]::Click(($r.L + [int]$p[0]), ($r.T + [int]$p[1]))
  Start-Sleep -Milliseconds $WaitMs
  Write-Output ("clicked client " + $p[0] + "," + $p[1] + " scVisible=" + [N9]::IsWindowVisible($h))
}

if ($Move -ne "") {
  $p = $Move.Split(",")
  [void][N9]::SetCursorPos([int]$p[0], [int]$p[1])
  [N9]::MoveBy(1, 0); Start-Sleep -Milliseconds 40
  [N9]::MoveBy(-1, 0); Start-Sleep -Milliseconds 60      # a real move makes the system re-apply the cursor
  $ci = New-Object N9+CI; $ci.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($ci)
  [void][N9]::GetCursorInfo([ref]$ci)
  Write-Output ("moved to " + $p[0] + "," + $p[1] + " cursor=" + $ci.hcursor +
                " pos=" + $ci.x + "," + $ci.y + " scVisible=" + [N9]::IsWindowVisible((Sc-Hwnd)))
}

if ($Move -ne "") {
  $p = $Move.Split(",")
  [void][N9]::SetCursorPos([int]$p[0], [int]$p[1])
  [N9]::MoveBy(2, 0); Start-Sleep -Milliseconds 60
  [N9]::MoveBy(-2, 0); Start-Sleep -Milliseconds 60      # only a real move re-applies the system cursor
  $ci = New-Object N9+CI; $ci.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($ci)
  [void][N9]::GetCursorInfo([ref]$ci)
  Write-Output ("moved to " + $p[0] + "," + $p[1] + " cursor=" + $ci.hcursor +
                " pos=" + $ci.x + "," + $ci.y + " scVisible=" + [N9]::IsWindowVisible((Sc-Hwnd)))
}

if ($ClickHost -ne "") {
  $hh = Host-Hwnd
  $r = Rect $hh
  [N9]::Click(($r.L + [int](($r.Rt - $r.L) / 2)), ($r.T + [int](($r.B - $r.T) / 2)))
  Start-Sleep -Milliseconds $WaitMs
  Write-Output ("clicked host centre, sc visible=" + [N9]::IsWindowVisible((Sc-Hwnd)))
}

if ($Dbl -ne "") {
  $h = Sc-Hwnd
  $r = Rect $h
  $p = $Dbl.Split(",")
  [N9]::DoubleClick(($r.L + [int]$p[0]), ($r.T + [int]$p[1]))
  Start-Sleep -Milliseconds $WaitMs
}

if ($Hotkey -ne "") { [N9]::CtrlTick(); Start-Sleep -Milliseconds $WaitMs }

if ($Wts -ne "") {
  $h = Sc-Hwnd
  [void][N9]::PostMsgPtr($h, 0x02B1, [IntPtr]([int]$Wts), [IntPtr]::Zero)   # WM_WTSSESSION_CHANGE
  Start-Sleep -Milliseconds 350
  Write-Output ("posted WTS wParam=" + $Wts + " sc visible=" + [N9]::IsWindowVisible($h))
}

if ($EndSession -ne "") {
  $h = Sc-Hwnd
  [void][N9]::PostMsgPtr($h, 0x0162, [IntPtr]([int]$EndSession), [IntPtr]::Zero)   # WM_ENDSESSION
  Start-Sleep -Milliseconds 350
  Write-Output ("posted ENDSESSION wParam=" + $EndSession + " sc visible=" + [N9]::IsWindowVisible($h))
}

if ($WaitPick -ne "") {
  Start-Sleep -Milliseconds ([int]$WaitPick)
  Write-Output ("waited " + $WaitPick + "ms, sc visible=" + [N9]::IsWindowVisible((Sc-Hwnd)))
}

if ($CloseHost -ne "") {
  $hh = Host-Hwnd
  [void][N9]::PostMsgPtr($hh, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)       # WM_CLOSE
  Start-Sleep -Milliseconds 500
  $still = [N9]::FindWindowW("SuperClipPasteTarget", "SuperClipPasteTarget")
  Write-Output ("host closed, find=" + ($still -eq [IntPtr]::Zero))
}

if ($Clicks -ne "") {
  $hh = Host-Hwnd
  $n = [N9]::SendMessageW($hh, 0x8009, [IntPtr]::Zero, "")                  # WM_APP+9, string marshal ignored
  Write-Output ("host mouse-key count=" + $n)
}

if ($Dump -ne "") {
  $hh = Host-Hwnd
  [void][N9]::SendMessageW($hh, 0x8007, [IntPtr]::Zero, $Dump)              # WM_APP+7
  Start-Sleep -Milliseconds 150
  if (Test-Path $Dump) {
    $txt = [IO.File]::ReadAllText($Dump, [Text.Encoding]::UTF8)
    $flat = $txt.Replace("`r", "\r").Replace("`n", "\n")
    Write-Output ("host text=[" + $flat + "] len=" + $txt.Length)
  } else { Write-Output "dump file missing" }
}

if ($Search -ne "") {
  $h = Sc-Hwnd
  $e = [N9]::FindWindowExW($h, [IntPtr]::Zero, "EDIT", $null)
  if ($e -eq [IntPtr]::Zero) { Write-Output "SEARCH_EDIT_NOT_FOUND"; exit 1 }
  $txt = $Search
  if ($Search.StartsWith("U:")) {
    # build CJK from code points so this script stays pure ASCII (PS 5.1 + BOM-less UTF-8 = GBK)
    $txt = -join ($Search.Substring(2).Split(",") | ForEach-Object { [char][Convert]::ToInt32($_, 16) })
  }
  [void][N9]::SendMessageW($e, 0x000C, [IntPtr]::Zero, $txt)   # WM_SETTEXT -> EN_CHANGE -> debounce
  Start-Sleep -Milliseconds ($WaitMs + 400)
  $cur = New-Object Text.StringBuilder 256
  [void][N9]::GetWindowTextW($e, $cur, 256)
  Write-Output ("search set to [" + $cur.ToString() + "] edit=" + $e + " scVisible=" + [N9]::IsWindowVisible($h))
}

if ($Topmost -ne "") {
  $h = Sc-Hwnd
  $ex = [N9]::GetWindowLong($h, -20)                       # GWL_EXSTYLE
  $isTop = ($ex -band 0x0008) -ne 0                        # WS_EX_TOPMOST
  $prev = [N9]::GetWindow($h, 3)                           # GW_HWNDPREV: Zero => nothing above
  Write-Output ("topmost=" + $isTop + " exstyle=" + $ex + " windowAbove=" + $prev +
                " fg=" + [N9]::Cls([N9]::GetForegroundWindow()))
}

if ($CursorShot -ne "") { Save-Cursor (Join-Path $dir $CursorShot) }

if ($Close -ne "") {
  $h = Sc-Hwnd
  if ($h -eq [IntPtr]::Zero) { Write-Output "close skipped: no SuperClip window"; exit 0 }
  [void][N9]::PostMsgPtr($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE -> ctx.Exit()
  Start-Sleep -Milliseconds 900
  $still = [N9]::FindWindowW("SuperClipMain", "SuperClip")
  Write-Output ("closed, window gone=" + ($still -eq [IntPtr]::Zero) + " still=" + $still)
}

if ($RightClick -ne "") {
  $h = Sc-Hwnd
  $r = Rect $h
  $p = $RightClick.Split(",")
  [N9]::RClick(($r.L + [int]$p[0]), ($r.T + [int]$p[1]))
  Start-Sleep -Milliseconds 350
  $m = Menu-Hwnd
  if ($m -eq [IntPtr]::Zero) { Write-Output ("rightclicked at " + $p[0] + "," + $p[1] + " but NO MENU"); exit 0 }
  $mr = Rect $m
  Write-Output ("rightclicked client " + $p[0] + "," + $p[1] + " menu=" + $m +
                " rect=" + $mr.L + "," + $mr.T + "," + $mr.Rt + "," + $mr.B)
}

if ($MenuDump -ne "") {
  $m = Menu-Hwnd
  if ($m -eq [IntPtr]::Zero) { Write-Output "MENU_NOT_FOUND"; exit 0 }
  $hm = Menu-HMENU $m
  $n = [N9]::GetMenuItemCount($hm)
  $mr = Rect $m
  Write-Output ("menu hwnd=" + $m + " hmenu=" + $hm + " items=" + $n +
                " rect=" + $mr.L + "," + $mr.T + "," + $mr.Rt + "," + $mr.B)
  for ($i = 0; $i -lt $n; $i++) {
    $sb = New-Object Text.StringBuilder 256
    [void][N9]::GetMenuStringW($hm, [uint32]$i, $sb, 256, 0x0400)   # MF_BYPOSITION; separator -> ""
    $ir = New-Object N9+R
    $ok = [N9]::GetMenuItemRect((Sc-Hwnd), $hm, [uint32]$i, [ref]$ir)
    $rt = if ($ok) { "" + $ir.L + "," + $ir.T + "," + $ir.Rt + "," + $ir.B } else { "NO_RECT" }
    Write-Output ("  [" + $i + "] [" + $sb.ToString() + "] " + $rt)
  }
}

if ($MenuClick -ne "") {
  $m = Menu-Hwnd
  if ($m -eq [IntPtr]::Zero) { Write-Output "MENU_NOT_FOUND"; exit 1 }
  $hm = Menu-HMENU $m
  $i = [uint32][int]$MenuClick
  $ir = New-Object N9+R
  if (-not [N9]::GetMenuItemRect((Sc-Hwnd), $hm, $i, [ref]$ir)) { Write-Output "MENU_RECT_FAIL"; exit 1 }
  $x = [int](($ir.L + $ir.Rt) / 2); $y = [int](($ir.T + $ir.B) / 2)
  [N9]::Click($x, $y)
  Start-Sleep -Milliseconds 500
  $still = Menu-Hwnd
  Write-Output ("menu item " + $MenuClick + " clicked at " + $x + "," + $y +
                " menuStillOpen=" + ($still -ne [IntPtr]::Zero))
}

if ($MenuKey -ne "") {
  $m = Menu-Hwnd
  if ($m -eq [IntPtr]::Zero) { Write-Output "MENU_NOT_FOUND"; exit 1 }
  foreach ($t in $MenuKey.Split(" ")) {
    if ($t -eq "") { continue }
    $vk = [uint16][int]$t
    [N9]::Key($vk, 0); Start-Sleep -Milliseconds 60; [N9]::Key($vk, 2)   # KEYEVENTF_KEYUP = 2
    Start-Sleep -Milliseconds 240
  }
  $still = Menu-Hwnd
  Write-Output ("sent keys [" + $MenuKey + "] menuStillOpen=" + ($still -ne [IntPtr]::Zero))
}

if ($ClickScreen -ne "") {
  # real click at raw screen coords -- needed for the help window, whose client origin is its
  # window origin (WS_POPUP) so the SuperClip-relative ClickAt maths does not apply
  $p = $ClickScreen.Split(",")
  [N9]::Click([int]$p[0], [int]$p[1])
  Start-Sleep -Milliseconds 300
  Write-Output ("clicked screen " + $p[0] + "," + $p[1])
}

if ($WinState -ne "") {
  $h = [N9]::FindWindowW($WinState, [NullString]::Value)
  if ($h -eq [IntPtr]::Zero) { Write-Output ("winstate not found: " + $WinState); exit 0 }
  $r = Rect $h
  Write-Output ("class=" + $WinState + " hwnd=" + $h + " visible=" + [N9]::IsWindowVisible($h) +
                " enabled=" + [N9]::IsWindowEnabled($h) + " rect=" + $r.L + "," + $r.T + "," +
                $r.Rt + "," + $r.B + " exstyle=" + [N9]::GetWindowLong($h, -20) +
                " fg=" + [N9]::Cls([N9]::GetForegroundWindow()) + " scEnabled=" +
                [N9]::IsWindowEnabled((Sc-Hwnd)))
}

if ($WinKey -ne "") {
  $p = $WinKey.Split("|")
  $h = [N9]::FindWindowW($p[0], [NullString]::Value)
  if ($h -eq [IntPtr]::Zero) { Write-Output ("winkey: class not found " + $p[0]); exit 1 }
  [void][N9]::SetForegroundWindow($h)
  Start-Sleep -Milliseconds 150
  foreach ($t in $p[1].Split(" ")) {
    if ($t -eq "") { continue }
    $vk = [uint16][int]$t
    [N9]::Key($vk, 0); Start-Sleep -Milliseconds 60; [N9]::Key($vk, 2)
    Start-Sleep -Milliseconds 220
  }
  Write-Output ("sent [" + $p[1] + "] to " + $p[0] + " visible=" + [N9]::IsWindowVisible($h) +
                " scEnabled=" + [N9]::IsWindowEnabled((Sc-Hwnd)))
}

if ($WinShot -ne "") {
  $p = $WinShot.Split(",")
  $h = [N9]::FindWindowW($p[0], [NullString]::Value)
  if ($h -eq [IntPtr]::Zero) { Write-Output ("winshot: class not found " + $p[0]); exit 0 }
  $r = Rect $h
  $w = $r.Rt - $r.L; $ht = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
  $out = Join-Path $dir $p[1]
  $bmp.Save($out); $g.Dispose(); $bmp.Dispose()
  Write-Output ("winshot " + $p[0] + " -> " + $out + " " + $w + "x" + $ht)
}

if ($Shot -ne "") {
  $h = Sc-Hwnd
  $r = Rect $h
  $w = $r.Rt - $r.L; $ht = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
  $p = Join-Path $dir $Shot
  $bmp.Save($p); $g.Dispose(); $bmp.Dispose()
  Write-Output ("shot " + $p + " " + $w + "x" + $ht)
}

if ($State) {
  $h = Sc-Hwnd
  $r = Rect $h
  $ci = New-Object N9+CI; $ci.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($ci)
  [void][N9]::GetCursorInfo([ref]$ci)
  Write-Output ("sc visible=" + [N9]::IsWindowVisible($h) + " rect=" + $r.L + "," + $r.T + "," +
                $r.Rt + "," + $r.B + " fg=" + [N9]::Cls([N9]::GetForegroundWindow()) +
                " cursor=" + $ci.hcursor + " cursorPos=" + $ci.x + "," + $ci.y)
}
