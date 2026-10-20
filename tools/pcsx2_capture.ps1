param([string]$OutDir = "", [int[]]$Shots = @(20), [string]$Pcsx2 = "E:\God Hand + Emulator\pcsx2.exe")
# Ground truth: boots the Tekken 4 ISO in PCSX2 (default: the user's install, BIOS and ISO already set up,
# English), saves captures of its game window at the given second marks, then stops PCSX2.
# Output: pcsx2_NNs.png in $OutDir (default logs\pcsx2_<time>). Example: powershell -File tools\pcsx2_capture.ps1 -Shots 10,30
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root ("logs\pcsx2_" + (Get-Date -Format "yyyyMMdd_HHmmss")) }
$iso = Join-Path $root "Tekken 4 (Europe, Australia) (En,Fr,De,Es,It) (v2.00).iso"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class Pcsx2CaptureWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    public static List<IntPtr> Visible(uint pid) {
        var r = new List<IntPtr>();
        EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) r.Add(h); return true; }, IntPtr.Zero);
        return r;
    }
}
"@
New-Item -ItemType Directory -Force $OutDir | Out-Null
$p = Start-Process -FilePath $Pcsx2 -WorkingDirectory (Split-Path $Pcsx2) -ArgumentList "--nogui", "`"$iso`"" -PassThru
$t0 = Get-Date
foreach ($m in $Shots) {
    while (((Get-Date) - $t0).TotalSeconds -lt $m -and -not $p.HasExited) { Start-Sleep -Milliseconds 250 }
    if ($p.HasExited) { "PCSX2 exited after {0:N1} s" -f ((Get-Date) - $t0).TotalSeconds; break }
    # With --nogui the only big visible window is the GS one; take the largest.
    $best = $null; $area = 0
    foreach ($h in [Pcsx2CaptureWin]::Visible([uint32]$p.Id)) {
        $r = New-Object Pcsx2CaptureWin+RECT; [void][Pcsx2CaptureWin]::GetWindowRect($h, [ref]$r)
        $a = ($r.Right - $r.Left) * ($r.Bottom - $r.Top); if ($a -gt $area) { $area = $a; $best = $h; $rect = $r }
    }
    if (-not $best) { "no window at $m s"; continue }
    $w = $rect.Right - $rect.Left; $hh = $rect.Bottom - $rect.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $hh; $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc(); [void][Pcsx2CaptureWin]::PrintWindow($best, $hdc, 2); $g.ReleaseHdc($hdc); $g.Dispose()
    $f = Join-Path $OutDir ("pcsx2_{0:D2}s.png" -f $m); $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "captured $f ($w x $hh)"
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
"done, output in $OutDir"
