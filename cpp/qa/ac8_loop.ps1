# AC-8 offline copy/paste loop driver (ReleaseChecklist.md section 3.5)
# ASCII only: Windows PowerShell 5.1 decodes BOM-less UTF-8 as GBK and eats characters.
#
# What it does, per iteration:
#   set clipboard -> our own WinForms textbox target -> Ctrl+` (summon) -> Space (paste newest)
# It reads the target back and compares, so it needs no external application.
#
# SAFETY GUARD: refuses to run when a real SuperClip history is present (see Test-RealData).
# Run it ONLY inside the throwaway acceptance VM.
#
# Usage (on the VM, as the interactive user, SuperClip installed and running):
#   powershell -NoProfile -ExecutionPolicy Bypass -File ac8_loop.ps1 -ConfirmVm -Iterations 100
# Prerequisite: paste mode must be QUICK -- click the mode button in the title bar yourself.
# v2.5.0 removed settings persistence, so quick mode can no longer be probed from disk; pass
# -ConfirmQuickMode to attest that you have switched to quick mode manually.

param(
    [int]$Iterations = 100,
    [int]$DelayMs = 350,
    [string]$OutCsv = "",
    [switch]$ConfirmVm,
    [switch]$ConfirmQuickMode
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$appDataDir = Join-Path $env:APPDATA 'SuperClip'
$histPath = Join-Path $appDataDir 'history.json'
$logPath = Join-Path $appDataDir 'error.log'

if (-not $ConfirmVm) {
    Write-Output 'ABORT: pass -ConfirmVm. This script writes 100 clipboard entries into whatever'
    Write-Output '       SuperClip instance is running. Never run it on a machine with real history.'
    exit 2
}

function Get-JsonArrayCount([string]$text) {
    # history.json is a bare array; count top-level objects by counting "Id" keys.
    return ([regex]::Matches($text, '"Id"')).Count
}

function Test-RealData {
    if (-not (Test-Path $histPath)) { return 0 }
    $raw = Get-Content $histPath -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) { return 0 }
    return (Get-JsonArrayCount $raw)
}

$existing = Test-RealData
if ($existing -gt 5) {
    Write-Output ("ABORT: history.json already holds {0} entries. This looks like a real machine," -f $existing)
    Write-Output '        restore the VM snapshot first (AC-7/AC-8 both assume a clean profile).'
    exit 3
}

Write-Output ("preflight: existing_history={0}" -f $existing)
if (-not $ConfirmQuickMode) {
    Write-Output 'ABORT: pass -ConfirmQuickMode after switching SuperClip to QUICK paste mode'
    Write-Output '       (click the mode button in the title bar). Since v2.5.0 settings are not'
    Write-Output '       persisted, quick mode cannot be probed from disk.'
    exit 4
}
if (-not (Get-Process -Name SuperClip -ErrorAction SilentlyContinue)) {
    Write-Output 'ABORT: SuperClip.exe is not running in this session.'
    exit 5
}

$logBytesBefore = 0
if (Test-Path $logPath) { $logBytesBefore = (Get-Item $logPath).Length }

# --- target window: one multiline textbox we own, so paste-back is verifiable ---------------
$form = New-Object System.Windows.Forms.Form
$form.Text = 'SuperClip AC-8 Target'
$form.Size = New-Object System.Drawing.Size(620, 220)
$form.StartPosition = 'Manual'
$form.Location = New-Object System.Drawing.Point(40, 40)
$box = New-Object System.Windows.Forms.TextBox
$box.Multiline = $true
$box.ScrollBars = 'Vertical'
$box.Dock = 'Fill'
$box.Font = New-Object System.Drawing.Font('Consolas', 11)
$form.Controls.Add($box)
$form.Show()
$form.Topmost = $true
[System.Windows.Forms.Application]::DoEvents()

# --- key injection ---------------------------------------------------------------------------
Add-Type -Namespace Sc -Name Key -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, System.UIntPtr dwExtraInfo);
public static void Tap(byte vk) {
    keybd_event(vk, 0, 0, System.UIntPtr.Zero);
    System.Threading.Thread.Sleep(40);
    keybd_event(vk, 0, 2, System.UIntPtr.Zero);
}
public static void Chord(byte mod, byte vk) {
    keybd_event(mod, 0, 0, System.UIntPtr.Zero);
    System.Threading.Thread.Sleep(40);
    keybd_event(vk, 0, 0, System.UIntPtr.Zero);
    System.Threading.Thread.Sleep(120);
    keybd_event(vk, 0, 2, System.UIntPtr.Zero);
    System.Threading.Thread.Sleep(40);
    keybd_event(mod, 0, 2, System.UIntPtr.Zero);
}
'@

$VK_CONTROL = 0x11
$VK_OEM_3 = 0xC0   # the backtick key, SuperClip default summon hotkey
$VK_SPACE = 0x20

function Set-ClipboardText([string]$text) {
    for ($t = 0; $t -lt 8; $t++) {
        try { [System.Windows.Forms.Clipboard]::SetText($text); return $true }
        catch { Start-Sleep -Milliseconds 80 }   # listener contention is normal
    }
    return $false
}

$rows = New-Object System.Collections.ArrayList
$failCount = 0
for ($i = 1; $i -le $Iterations; $i++) {
    $token = "AC8-{0:D3}-{1}" -f $i, ([guid]::NewGuid().ToString('N').Substring(0, 8))
    $clip = Set-ClipboardText $token
    Start-Sleep -Milliseconds $DelayMs

    $box.Clear()
    $form.Activate()
    $box.Focus()
    [Sc.Key]::Chord($VK_CONTROL, $VK_OEM_3)      # summon SuperClip (global hotkey)
    Start-Sleep -Milliseconds $DelayMs
    [Sc.Key]::Tap($VK_SPACE)                     # quick mode: row 1 is the newest entry (C14)
    Start-Sleep -Milliseconds $DelayMs

    $seen = $box.Text
    $matched = $seen.Contains($token)
    if (-not $matched) { $failCount++ }
    [void]$rows.Add([pscustomobject]@{
        iter = $i; token = $token; clipboard_ok = $clip; pasted = $matched; received_len = $seen.Length
    })
    if ($i % 10 -eq 0) { Write-Output ("iter {0}/{1} matched={2}" -f $i, $Iterations, $matched) }
}

$form.Close()

$logBytesAfter = 0
if (Test-Path $logPath) { $logBytesAfter = (Get-Item $logPath).Length }
$histAfter = Test-RealData

if ([string]::IsNullOrWhiteSpace($OutCsv)) {
    $OutCsv = Join-Path $env:TEMP 'sc-ac8-result.csv'
}
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $OutCsv

Write-Output '---------- AC-8 result ----------'
Write-Output ("iterations={0} matched={1} failed={2}" -f $Iterations, ($Iterations - $failCount), $failCount)
Write-Output ("history_after={0} (cap is 100; older entries may be evicted - that is expected)" -f $histAfter)
Write-Output ("error.log grew by {0} bytes" -f ($logBytesAfter - $logBytesBefore))
Write-Output ("csv={0}" -f $OutCsv)
if ($failCount -eq 0) { Write-Output 'VERDICT: pass' } else { Write-Output 'VERDICT: FAIL - see csv rows with pasted=False' }
