param([string]$RunDir = "", [int[]]$Shots = @(20), [string[]]$Keys = @())
# Runs ps2EntryRunner on the Tekken 4 ELF, hides the runtime's debug panel (F1), presses pad buttons at given
# times, saves window captures at the given second marks, then closes it. Output: stdout.txt, stderr.txt and
# shot_NNs.png in $RunDir (default logs\run_<time>).
# Keys are "<seconds>:<key>" with key = Up, Down, Left, Right, Start, Cross, Circle, Square, Triangle, posted to
# the window as key presses (held ~250 ms) that the runtime maps to pad 1 (arrows, Enter, X, C, Z, V).
# Always pick PAL60 on the display-mode menu (user's request):
#   powershell -File tools\run_capture.ps1 -Keys "10:Down","12:Cross" -Shots 9,11,16,25
$root = Split-Path -Parent $PSScriptRoot
if (-not $RunDir) { $RunDir = Join-Path $root ("logs\run_" + (Get-Date -Format "yyyyMMdd_HHmmss")) }
$vk = @{ Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27; Start = 0x0D; Cross = 0x58; Circle = 0x43; Square = 0x5A; Triangle = 0x56 }
$scan = @{ Up = 0x48; Down = 0x50; Left = 0x4B; Right = 0x4D; Start = 0x1C; Cross = 0x2D; Circle = 0x2E; Square = 0x2C; Triangle = 0x2F }
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
# Posted key messages reach the GLFW window without a focus change (lParam: repeat 1, scancode, extended flag).
function Send-Key([IntPtr]$h, [int]$vkCode, [int]$scanCode, [bool]$extended, [int]$holdMs) {
    $flags = [int64]1 -bor ([int64]$scanCode -shl 16)
    if ($extended) { $flags = $flags -bor (1 -shl 24) }
    [void][RunCaptureWin]::PostMessage($h, 0x0100, [IntPtr]$vkCode, [IntPtr]$flags)
    Start-Sleep -Milliseconds $holdMs
    [void][RunCaptureWin]::PostMessage($h, 0x0101, [IntPtr]$vkCode, [IntPtr]($flags -bor [int64]0xC0000000))
}
New-Item -ItemType Directory -Force $RunDir | Out-Null
$exe = Join-Path $root "external\PS2Recomp\out\runtime\ps2xRuntime\ps2EntryRunner.exe"
$elf = Join-Path $root "game\disc\SCES_508.78"
# Draw on several threads like tools\run_game.cmd (set PS2X_GS_THREADS=0 beforehand to draw on one).
if (-not $env:PS2X_GS_THREADS) { $env:PS2X_GS_THREADS = "auto" }
$events = @($Shots | ForEach-Object { [pscustomobject]@{ t = [double]$_; kind = "shot"; key = "" } })
foreach ($k in $Keys) { $t, $name = $k -split ":", 2; $events += [pscustomobject]@{ t = [double]$t; kind = "key"; key = $name } }
$events = $events | Sort-Object t
$p = Start-Process -FilePath $exe -ArgumentList "`"$elf`"" -WorkingDirectory $RunDir `
    -RedirectStandardOutput "$RunDir\stdout.txt" -RedirectStandardError "$RunDir\stderr.txt" -PassThru
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 3 -and -not $p.HasExited) { Start-Sleep -Milliseconds 250 }
$p.Refresh(); if ($p.MainWindowHandle -ne [IntPtr]::Zero) { Send-Key $p.MainWindowHandle 0x70 0x3B $false 100 }
foreach ($e in $events) {
    while (((Get-Date) - $t0).TotalSeconds -lt $e.t -and -not $p.HasExited) { Start-Sleep -Milliseconds 100 }
    if ($p.HasExited) { "exited early after {0:N1} s" -f ((Get-Date) - $t0).TotalSeconds; break }
    $p.Refresh(); $h = $p.MainWindowHandle
    if ($e.kind -eq "key") {
        if (-not $vk.ContainsKey($e.key)) { "unknown key $($e.key)"; continue }
        Send-Key $h $vk[$e.key] $scan[$e.key] (@("Up", "Down", "Left", "Right") -contains $e.key) 250
        "pressed $($e.key) at {0:N1} s" -f ((Get-Date) - $t0).TotalSeconds
        continue
    }
    $r = New-Object RunCaptureWin+RECT; [void][RunCaptureWin]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
    if ($w -le 0 -or $hgt -le 0) { "no window at $($e.t) s"; continue }
    $bmp = New-Object System.Drawing.Bitmap $w, $hgt
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc(); [void][RunCaptureWin]::PrintWindow($h, $hdc, 2); $g.ReleaseHdc($hdc); $g.Dispose()
    $out = Join-Path $RunDir ("shot_{0:D2}s.png" -f [int]$e.t)
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "captured $out"
}
if (-not $p.HasExited) {
    $null = $p.CloseMainWindow()
    if (-not $p.WaitForExit(8000)) { "did not close, killing"; $p.Kill(); $null = $p.WaitForExit(5000) }
}
"ran {0:N1} s, output in $RunDir" -f ((Get-Date) - $t0).TotalSeconds
