param(
  [string]$Token = "SCTOK-N1",
  [string]$RowY = "140",
  [string]$Shot = ""
)
# Full Notepad scenario in one process: caret focus, raise, filter, double-click paste,
# then read the document back through Ctrl+A / Ctrl+C (real app round trip).
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class M {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, uint cb);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U U; }
  public static void Key(ushort vk, uint f) {
    INPUT[] i = new INPUT[1]; i[0].type = 1; i[0].U.ki.vk = vk; i[0].U.ki.flags = f;
    SendInput(1, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Combo(ushort vk) { Key(0x11,0); Key(vk,0); Key(vk,2); Key(0x11,2); }
  public static void CtrlTick() { Key(0x11,0); Key(0xC0,0); Key(0xC0,2); Key(0x11,2); }
  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    INPUT[] i = new INPUT[2];
    i[0].type = 0; i[0].U.mi.flags = 0x0002;
    i[1].type = 0; i[1].U.mi.flags = 0x0004;
    SendInput(2, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static void DblClick(int x, int y) {
    SetCursorPos(x, y);
    INPUT[] i = new INPUT[4];
    for (int k = 0; k < 4; ++k) { i[k].type = 0; i[k].U.mi.flags = (k % 2 == 0) ? 0x0002u : 0x0004u; }
    SendInput(4, i, (uint)Marshal.SizeOf(typeof(INPUT)));
  }
  public static R Rect(IntPtr h) { R r; GetWindowRect(h, out r); return r; }
  public static string Cls(IntPtr h) { StringBuilder s = new StringBuilder(128); GetClassNameW(h, s, 128); return s.ToString(); }
}
"@

function Fg() { return [M]::GetForegroundWindow() }
function ClsOf($h) { return [M]::Cls($h) }

$np = Get-Process notepad -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $np) { Start-Process notepad | Out-Null; Start-Sleep -Seconds 4; $np = Get-Process notepad | Select-Object -First 1 }
if (-not $np) { Write-Output "NOTEPAD_MISSING"; exit 1 }
$npH = [IntPtr]$np.MainWindowHandle
Write-Output ("notepad hwnd=" + $npH + " title=[" + $np.MainWindowTitle + "]")

$sc = [M]::FindWindowW("SuperClipMain", "SuperClip")
if ($sc -eq [IntPtr]::Zero) { Write-Output "SC_WINDOW_NOT_FOUND"; exit 1 }

# 1) clear the document so the round trip is unambiguous
[M]::SetForegroundWindow($npH); Start-Sleep -Milliseconds 400
$r = [M]::Rect($npH)
[M]::Click(($r.L + 200), ($r.T + 200)); Start-Sleep -Milliseconds 300
[M]::Combo(0x41); Start-Sleep -Milliseconds 200        # Ctrl+A
[M]::Key(0xDE, 0); [M]::Key(0xDE, 2); Start-Sleep -Milliseconds 300   # VK_DELETE
Write-Output ("step1 fg=" + (ClsOf (Fg)))

# 2) copy the token (creates the history item) while Notepad is the active app
Set-Clipboard -Value $Token -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600
Write-Output ("step2 clip=[" + (Get-Clipboard) + "] fg=" + (ClsOf (Fg)))

# 3) raise SuperClip with the hotkey -> target capture happens here
[M]::CtrlTick(); Start-Sleep -Milliseconds 700
Write-Output ("step3 sc visible=" + [M]::IsWindowVisible($sc) + " fg=" + (ClsOf (Fg)))

# 4) filter to the token row and double-click it (normal mode = paste, position kept)
$edit = [M]::FindWindowExW($sc, [IntPtr]::Zero, "EDIT", $null)
[M]::SetForegroundWindow($sc)
[M]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, $Token) | Out-Null
Start-Sleep -Milliseconds 700
$sr = [M]::Rect($sc)
[M]::DblClick(($sr.L + 190), ($sr.T + [int]$RowY))
Start-Sleep -Milliseconds 900
Write-Output ("step4 fg=" + (ClsOf (Fg)) + " clip=[" + (Get-Clipboard) + "]")

# 5) read the document back through the app itself
[M]::SetForegroundWindow($npH); Start-Sleep -Milliseconds 400
[M]::Combo(0x41); Start-Sleep -Milliseconds 200        # Ctrl+A
[M]::Combo(0x43); Start-Sleep -Milliseconds 400        # Ctrl+C
$doc = Get-Clipboard
Write-Output ("step5 doc=[" + $doc + "]")
if ($doc -eq $Token) { Write-Output "RESULT PASS token-in-notepad" }
else { Write-Output "RESULT FAIL expected=[$Token] got=[$doc]" }

if ($Shot -ne "") {
  $bmp = New-Object System.Drawing.Bitmap(($sr.Rt - $sr.L), ($sr.B - $sr.T))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($sr.L, $sr.T, 0, 0, (New-Object System.Drawing.Size(($sr.Rt - $sr.L), ($sr.B - $sr.T))))
  $bmp.Save((Join-Path $dir $Shot)); $g.Dispose(); $bmp.Dispose()
  Write-Output ("shot " + $Shot)
}
