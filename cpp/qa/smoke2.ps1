# M1 冒烟 2：全局热键呼出/收起 + 收起状态下继续采集（FR-01/15/16）
$ErrorActionPreference = "Continue"
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class Win {
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls,string title);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte sc,uint fl,UIntPtr extra);
}
'@

$hwnd = [Win]::FindWindowW("SuperClipMain","SuperClip")
Write-Output "HWND_FOUND=$($hwnd -ne [IntPtr]::Zero)"
Write-Output "VISIBLE_BEFORE=$([Win]::IsWindowVisible($hwnd))"

function TapHotkey {
  [Win]::keybd_event(0x11,0,0,[UIntPtr]::Zero)          # Ctrl down
  [Win]::keybd_event(0xC0,0,0,[UIntPtr]::Zero)          # `  down
  [Win]::keybd_event(0xC0,0,2,[UIntPtr]::Zero)          # `  up
  [Win]::keybd_event(0x11,0,2,[UIntPtr]::Zero)          # Ctrl up
}

TapHotkey
Start-Sleep -Seconds 1
Write-Output "VISIBLE_AFTER_TAP1=$([Win]::IsWindowVisible($hwnd))"

$tag = "M1HIDDEN-" + (Get-Random -Maximum 999999)
Set-Clipboard -Value $tag
Start-Sleep -Seconds 2
$hist = Get-Content "$env:APPDATA\SuperClip\history.json" -Raw -Encoding UTF8
Write-Output "CAPTURED_WHILE_HIDDEN=$($hist.Contains($tag))"
Write-Output "TAG=$tag"

TapHotkey
Start-Sleep -Seconds 1
Write-Output "VISIBLE_AFTER_TAP2=$([Win]::IsWindowVisible($hwnd))"
