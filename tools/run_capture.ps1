param([string]$RunDir = "", [int[]]$Shots = @(20))
# Runs ps2EntryRunner on the Tekken 4 ELF, hides the runtime's debug panel (F1), saves window captures at the
# given second marks, then closes it. Output: stdout.txt, stderr.txt and shot_NNs.png in $RunDir
# (default logs\run_<time>). Example: powershell -File tools\run_capture.ps1 -Shots 8,25
$root = Split-Path -Parent $PSScriptRoot
if (-not $RunDir) { $RunDir = Join-Path $root ("logs\run_" + (Get-Date -Format "yyyyMMdd_HHmmss")) }
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class RunCaptureWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
}
"@
# F1 toggles the debug panel; posting it to the window needs no focus change.
function Send-F1([IntPtr]$h) {
    [void][RunCaptureWin]::PostMessage($h, 0x0100, [IntPtr]0x70, [IntPtr](0x3B -shl 16 -bor 1))
    Start-Sleep -Milliseconds 100
    [void][RunCaptureWin]::PostMessage($h, 0x0101, [IntPtr]0x70, [IntPtr]([int64]0xC0000001 -bor (0x3B -shl 16)))
}
New-Item -ItemType Directory -Force $RunDir | Out-Null
$exe = Join-Path $root "external\PS2Recomp\out\runtime\ps2xRuntime\ps2EntryRunner.exe"
$elf = Join-Path $root "game\disc\SCES_508.78"
$p = Start-Process -FilePath $exe -ArgumentList "`"$elf`"" -WorkingDirectory $RunDir `
    -RedirectStandardOutput "$RunDir\stdout.txt" -RedirectStandardError "$RunDir\stderr.txt" -PassThru
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 3 -and -not $p.HasExited) { Start-Sleep -Milliseconds 250 }
$p.Refresh(); if ($p.MainWindowHandle -ne [IntPtr]::Zero) { Send-F1 $p.MainWindowHandle }
foreach ($m in $Shots) {
    while (((Get-Date) - $t0).TotalSeconds -lt $m -and -not $p.HasExited) { Start-Sleep -Milliseconds 250 }
    if ($p.HasExited) { "exited early after {0:N1} s" -f ((Get-Date) - $t0).TotalSeconds; break }
    $p.Refresh(); $h = $p.MainWindowHandle
    $r = New-Object RunCaptureWin+RECT; [void][RunCaptureWin]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
    if ($w -le 0 -or $hgt -le 0) { "no window at $m s"; continue }
    $bmp = New-Object System.Drawing.Bitmap $w, $hgt
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc(); [void][RunCaptureWin]::PrintWindow($h, $hdc, 2); $g.ReleaseHdc($hdc); $g.Dispose()
    $out = Join-Path $RunDir ("shot_{0:D2}s.png" -f $m)
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "captured $out"
}
if (-not $p.HasExited) {
    $null = $p.CloseMainWindow()
    if (-not $p.WaitForExit(8000)) { "did not close, killing"; $p.Kill(); $null = $p.WaitForExit(5000) }
}
"ran {0:N1} s, output in $RunDir" -f ((Get-Date) - $t0).TotalSeconds
