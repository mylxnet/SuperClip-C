param(
  [string]$Host2 = "",              # start PasteTarget.exe with this dump path; append ' min' to start minimized
  [switch]$Activate,                # bring host to foreground and put caret in its EDIT
  [string]$Reset = "",              # reset host EDIT text to HEAD/CRLF/TAIL with caret after HEAD
  [string]$Copy = "",               # write text to clipboard (creates a history item)
  [string]$CopyAfter = "",          # write text to clipboard AFTER the paste/dump verbs (guard window test)
  [string]$Search = "",             # WM_SETTEXT into SuperClip search EDIT
  [string]$Hotkey = "",             # Ctrl+` : raise/lower SuperClip
  [string]$Dbl = "",                # double click at client coords of SuperClip main window
  [string]$ClickAt = "",            # single click at client coords
  [string]$Space = "",              # send VK_SPACE
  [string]$CtrlV = "",              # send Ctrl+V via SendInput (environment isolation test)
  [string]$RaiseHost = "",          # SetForegroundWindow(host) only, no click
  [string]$RaiseSc = "",            # SetForegroundWindow(SuperClip) only
  [string]$MinHost = "",            # SW_MINIMIZE the host (T4: the app must restore it)
  [string]$FocusProbe = "",         # print host thread's focused/active window
  [string]$CloseHost = "",          # close host window (tests dead target fallback)
  [string]$Dump = "",               # ask host to write its text, print result
  [string]$Shot = "",
  [switch]$State,
  [int]$WaitMs = 450,
  [int]$SpaceGapMs = 260,
  [int]$T4Wait = -1
)
# ASCII-only comments: PS 5.1 decodes BOM-less UTF-8 as GBK and a CJK lead byte can eat
# the newline, silently merging param lines.
# artifacts (exe + screenshots) stay in ../build-mingw; only the .ps1 drivers live here
$dir = Join-Path (Split-Path -Parent $PSScriptRoot) "build-mingw"
$targetExe = Join-Path $dir "PasteTarget.exe"
$scExe = Join-Path $dir "SuperClip.exe"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class N {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  // EM_SETSEL needs an integer lParam; reusing the string overload would marshal a pointer (huge nEnd -> select to end).
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMsgPtr(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, uint cb);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, uint[] pid);
  [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint tid, out GUIINFO gi);
  [StructLayout(LayoutKind.Sequential)] public struct GUIINFO {
    public int cbSize; public IntPtr hwndFocus, hwndActive, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret;
    public R rcCaret;
  }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U U; }
  public static void Key(ushort vk, uint flags) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].U.ki.vk = vk; i[0].U.ki.flags = flags;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void CtrlTick() { Key(0x11,0); Key(0xC0,0); Key(0xC0,2); Key(0x11,2); }
  public static void Space(bool up) { Key(0x20, up ? 2u : 0u); }
  public static void CtrlV() { Key(0x11,0); Key(0x56,0); Key(0x56,2); Key(0x11,2); }
  public static void Force(IntPtr h) {          // ALT tap releases the foreground lock for this thread
    Key(0x12,0); Key(0x12,2); SetForegroundWindow(h);
  }
  public static void Move(int x, int y) { SetCursorPos(x, y); }
  public static void Btn(uint f) {
    INPUT[] i = new INPUT[1]; i[0].type = 0; i[0].U.mi.flags = f;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Click(int x, int y) { SetCursorPos(x,y); Btn(0x0002); Btn(0x0004); }
  public static void DoubleClick(int x, int y) {
    SetCursorPos(x,y); Btn(0x0002); Btn(0x0004); Btn(0x0002); Btn(0x0004);
  }
  public static string Cls(IntPtr h) {
    StringBuilder s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString();
  }
  public static string Title(IntPtr h) {
    StringBuilder s = new StringBuilder(256); GetWindowTextW(h, s, 256); return s.ToString();
  }
}
"@

function Rect($hwnd) { $r = New-Object N+R; [void][N]::GetWindowRect($hwnd, [ref]$r); return $r }
function Save-Shot($rect, $name) {
  $w = $rect.Rt - $rect.L; $ht = $rect.B - $rect.T
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($rect.L, $rect.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
  $p = Join-Path $dir $name
  $bmp.Save($p); $g.Dispose(); $bmp.Dispose()
  Write-Output ("shot " + $p + " " + $w + "x" + $ht)
}
function Host-Hwnd {
  $h = [N]::FindWindowW("SuperClipPasteTarget", "SuperClipPasteTarget")
  if ($h -eq [IntPtr]::Zero) { Write-Output "HOST_NOT_FOUND"; exit 1 }
  return $h
}

if ($Host2 -ne "") {
  $parts = $Host2.Split(" ")
  $dumpPath = $parts[0]
  $args2 = @($dumpPath)
  if ($parts.Count -gt 1) { $args2 += $parts[1] }
  if (-not (Get-Process PasteTarget -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath $targetExe -ArgumentList $args2 | Out-Null
    Start-Sleep -Seconds 1
  }
  $hh = Host-Hwnd
  Write-Output ("host started pid=" + (Get-Process PasteTarget | Select-Object -First 1 -ExpandProperty Id) +
                " visible=" + [N]::IsWindowVisible($hh) + " iconic=" + [N]::IsIconic($hh))
}

if ($Copy -ne "") { Set-Clipboard -Value $Copy; Start-Sleep -Milliseconds $WaitMs }

if ($Activate) {
  $hh = Host-Hwnd
  if ([N]::IsIconic($hh)) { [void][N]::ShowWindow($hh, 9) }   # SW_RESTORE
  [void][N]::Force($hh)
  Start-Sleep -Milliseconds 300
  $ed = [N]::FindWindowExW($hh, [IntPtr]::Zero, "Edit", $null)
  $er = Rect $ed
  [N]::Click(($er.L + 50), ($er.T + 8))                         # real click = keyboard focus on the EDIT
  [void][N]::SendMsgPtr($ed, 0x00B1, [IntPtr]4, [IntPtr]4)   # EM_SETSEL: caret after "HEAD"
  Start-Sleep -Milliseconds 200
  $fg = [N]::GetForegroundWindow()
  Write-Output ("activated fg=" + [N]::Cls($fg) + " host=" + [N]::Cls($hh))
}

if ($Reset -ne "") {
  $hh = Host-Hwnd
  $ed = [N]::FindWindowExW($hh, [IntPtr]::Zero, "Edit", $null)
  [void][N]::SendMessageW($ed, 0x000C, [IntPtr]::Zero, "HEAD`r`nTAIL")   # WM_SETTEXT
  [void][N]::SendMsgPtr($ed, 0x00B1, [IntPtr]4, [IntPtr]4)               # EM_SETSEL
  Start-Sleep -Milliseconds 200
  Write-Output "host text reset"
}

$sc = [N]::FindWindowW("SuperClipMain", "SuperClip")
if ($Hotkey -ne "") {
  if ($sc -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  [N]::CtrlTick(); Start-Sleep -Milliseconds $WaitMs
}

if ($Search -ne "") {
  if ($sc -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  $edit = [N]::FindWindowExW($sc, [IntPtr]::Zero, "EDIT", $null)
  if ($edit -eq [IntPtr]::Zero) { Write-Output "SC_EDIT_NOT_FOUND"; exit 1 }
  [void][N]::Force($sc)
  [void][N]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, $Search)   # WM_SETTEXT
  Start-Sleep -Milliseconds 600                                      # debounce 300ms
}

if ($ClickAt -ne "") {
  if ($sc -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  $r = Rect $sc
  $p = $ClickAt.Split(",")
  [N]::Click(($r.L + [int]$p[0]), ($r.T + [int]$p[1]))
  Start-Sleep -Milliseconds $WaitMs
}
if ($Dbl -ne "") {
  if ($sc -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  $r = Rect $sc
  $p = $Dbl.Split(",")
  [N]::DoubleClick(($r.L + [int]$p[0]), ($r.T + [int]$p[1]))
  Start-Sleep -Milliseconds $WaitMs
}
if ($Space -ne "") {
  # The app raises the target inside the keydown handler, so a fast release lands on the target and
  # the injected Ctrl+V gets swallowed. Hold the key past the app focus wait.
  [N]::Space($false); Start-Sleep -Milliseconds $SpaceGapMs; [N]::Space($true); Start-Sleep -Milliseconds $WaitMs
}
if ($CtrlV -ne "") { [N]::CtrlV(); Start-Sleep -Milliseconds $WaitMs }
# Same as -Copy but runs after the paste verbs, inside the app's 1000ms paste guard.
if ($CopyAfter -ne "") { Set-Clipboard -Value $CopyAfter; Start-Sleep -Milliseconds $WaitMs }

if ($MinHost -ne "") {
  $hh = Host-Hwnd
  [void][N]::ShowWindow($hh, 6)                       # SW_MINIMIZE: T4, the app must restore it itself
  Start-Sleep -Milliseconds 400
  Write-Output ("host minimized iconic=" + [N]::IsIconic($hh) + " visible=" + [N]::IsWindowVisible($hh))
}
if ($RaiseHost -ne "") {
  $hh = Host-Hwnd
  [void][N]::Force($hh)
  Start-Sleep -Milliseconds 300
  $fg = [N]::GetForegroundWindow()
  Write-Output ("raise-host fg=" + [N]::Cls($fg))
}
if ($RaiseSc -ne "") {
  if ($sc -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  [void][N]::Force($sc)
  Start-Sleep -Milliseconds 300
  Write-Output ("raise-sc fg=" + [N]::Cls([N]::GetForegroundWindow()))
}
if ($FocusProbe -ne "") {
  $hh = Host-Hwnd
  $tid = [N]::GetWindowThreadProcessId($hh, $null)
  $gi = New-Object N+GUIINFO
  $gi.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($gi)
  [void][N]::GetGUIThreadInfo($tid, [ref]$gi)
  Write-Output ("host focused child=" + [N]::Cls($gi.hwndFocus) + " active=" + [N]::Cls($gi.hwndActive))
}

# Isolate the T4 restore timing without involving SuperClip: minimize, restore with the same
# three-call sequence the app uses, wait N ms, then send Ctrl+V from here.
if ($T4Wait -ge 0) {
  $hh = Host-Hwnd
  [void][N]::ShowWindow($hh, 6)                              # SW_MINIMIZE
  Start-Sleep -Milliseconds 500
  [void][N]::ShowWindow($hh, 9)                              # SW_RESTORE
  [void][N]::SetForegroundWindow($hh); [void][N]::SetActiveWindow($hh); [N]::SwitchToThisWindow($hh, $true)
  Start-Sleep -Milliseconds $T4Wait
  $fgb = [N]::Cls([N]::GetForegroundWindow())
  [N]::CtrlV()
  Start-Sleep -Milliseconds 400
  Write-Output ("t4 wait=" + $T4Wait + " fgBeforeKey=" + $fgb + " iconic=" + [N]::IsIconic($hh))
}

if ($Dump -ne "") {
  $hh = Host-Hwnd
  [void][N]::SendMessageW($hh, 0x8007, [IntPtr]::Zero, $null)        # WM_APP+7
  Start-Sleep -Milliseconds 150
  $f = $Dump
  if (Test-Path $f) {
    $txt = [IO.File]::ReadAllText($f, [Text.Encoding]::UTF8)
    Write-Output ("dumplen=" + $txt.Length + " dump=[" + $txt.Replace("`r","\r").Replace("`n","\n") + "]")
  } else { Write-Output "DUMP_MISSING $f" }
}

if ($CloseHost -ne "") {
  $hh = Host-Hwnd
  [void][N]::SendMessageW($hh, 0x0010, [IntPtr]::Zero, $null)         # WM_CLOSE
  Start-Sleep -Milliseconds 400
  Write-Output ("host closed, still found=" + ([N]::FindWindowW("SuperClipPasteTarget","SuperClipPasteTarget") -ne [IntPtr]::Zero))
}

if ($State) {
  if ($sc -eq [IntPtr]::Zero) { Write-Output "sc=NOT_RUNNING"; }
  else {
    $r = Rect $sc
    Write-Output ("sc visible=" + [N]::IsWindowVisible($sc) + " rect=" + $r.L + "," + $r.T + "," + $r.Rt + "," + $r.B)
  }
  $fg = [N]::GetForegroundWindow()
  Write-Output ("fg=" + [N]::Cls($fg) + "|" + [N]::Title($fg))
}

if ($Shot -ne "") {
  if ($sc -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }
  Save-Shot (Rect $sc) $Shot
}
