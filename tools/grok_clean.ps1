param([Parameter(Mandatory = $true)][string]$Path)
# The only way a Grok task may delete anything (recursive deletes are denied in tools\grok_task.ps1 after the
# 2026-10-22 09:55 wipe of the project folder). Deletes $Path only if it resolves to a folder or file STRICTLY inside
# logs\grok\ or out\grok-*\ of this project, with no junction/symlink in the way.
$root = (Split-Path -Parent $PSScriptRoot).TrimEnd('\')
$full = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $root $Path })).TrimEnd('\')
$okLogs = $full.StartsWith("$root\logs\grok\", [System.StringComparison]::OrdinalIgnoreCase)
$okOut = $full.StartsWith("$root\out\grok-", [System.StringComparison]::OrdinalIgnoreCase) -and $full.Substring("$root\out\".Length).Contains("\")
if (-not ($okLogs -or $okOut)) { "refused: $full is not inside logs\grok\ or out\grok-*"; exit 1 }
if (-not (Test-Path -LiteralPath $full)) { "nothing to delete: $full"; exit 0 }
$bad = Get-ChildItem -LiteralPath $full -Force -Recurse -Attributes ReparsePoint -ErrorAction SilentlyContinue | Select-Object -First 1
if ($bad -or ((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { "refused: link inside $full"; exit 1 }
Remove-Item -LiteralPath $full -Recurse -Force
"deleted $full"
