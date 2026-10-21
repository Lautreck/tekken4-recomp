param([int[]]$DumpAt = @(66, 72, 78, 84), [string]$OutDir = "")
# PCSX2 ground truth for the fight camera (compare with our TEKKEN4_CAMERA_LOG, runtime/tekken4_camera_overrides.cpp):
# runs to the first fight (PAL60, Yes, Start, Medium, Arcade, Kazuya; the fight starts ~59 s) and, at the given seconds,
# reads the camera pointer words 0x2AA0D0 / 0x2AA0D4 through PINE, then dumps cam = p + 0xF08 (0x200 bytes) for each
# and the matrix slots at 0x01CE2270 (64 slots of 0xC0). Output: logs\pcsx2_camera_probe (or -OutDir).
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root "logs\pcsx2_camera_probe" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$shots = @($DumpAt | ForEach-Object { $_ }) -join ","
$job = Start-Job -ScriptBlock {
    param($root, $out, $shots)
    & (Join-Path $root "tools\pcsx2_capture.ps1") -OutDir $out -Shots $shots `
        -Keys "12:Down","14:Cross","16:Left","17:Cross","30:Start","36:Cross","40:Down","41.5:Cross","50:Cross"
} -ArgumentList $root, $OutDir, $shots
$pine = Join-Path $root "tools\pine.py"
# pcsx2_capture may first wait for the run lock (Claude's or Grok's game runs): time the dumps from PCSX2's own start.
$launched = Get-Date
$t0 = $null
while (-not $t0) {
    $p = Get-Process pcsx2-qt -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -ge $launched.AddSeconds(-2) } |
        Select-Object -First 1
    if ($p) { $t0 = $p.StartTime } else { Start-Sleep -Milliseconds 250 }
    if (((Get-Date) - $launched).TotalMinutes -gt 35) { throw "PCSX2 did not start" }
}
foreach ($at in $DumpAt) {
    while (((Get-Date) - $t0).TotalSeconds -lt $at) { Start-Sleep -Milliseconds 250 }
    $words = python -I $pine read32 0x2AA0D0 0x2AA0D4 2>&1
    $words | Out-File -Encoding utf8 (Join-Path $OutDir "camera_$at.txt")
    foreach ($line in $words) {
        if ($line -match '^0x([0-9a-fA-F]+):\s*0x([0-9a-fA-F]+)') {
            $p = [Convert]::ToUInt32($Matches[2], 16)
            if ($p -ne 0 -and $p -lt 0x2000000) {
                $cam = "0x{0:x}" -f ($p + 0xF08)
                python -I $pine dump $cam 0x200 --out (Join-Path $OutDir ("cam_{0}_{1}.bin" -f $Matches[1], $at)) 2>&1 |
                    Out-File -Encoding utf8 -Append (Join-Path $OutDir "camera_$at.txt")
            }
        }
    }
    python -I $pine dump 0x01CE2270 0x3000 --out (Join-Path $OutDir "slots_$at.bin") 2>&1 |
        Out-File -Encoding utf8 -Append (Join-Path $OutDir "camera_$at.txt")
}
Wait-Job $job | Out-Null
Receive-Job $job
Remove-Job $job
Get-ChildItem $OutDir | Select-Object Name, Length | Format-Table -AutoSize | Out-String
