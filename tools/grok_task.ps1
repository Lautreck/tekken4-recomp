param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Prompt = "",
    [string]$PromptFile = "",
    [ValidateSet("ask", "work")][string]$Mode = "ask",
    [string]$Model = "",
    [string]$Effort = "",
    [int]$MaxTurns = 80
)
# Hands one task to Grok (xAI's Grok Build CLI, `grok`) so it works in parallel with Claude, and saves the result in
# logs\grok\<Name>_<time>.md (prompt, answer, and what it changed).
#   ask:  in the project without file edits (python -I scripts and read-only git allowed): second opinions, patch
#         reviews, research.
#   work: Grok edits files in its own clone (~\.grok\worktrees\<repo>\<Name>, tracked files only: no game\, external\
#         or logs\, so builds and game runs stay with Claude) without running commands. Review its changes with
#         git -C <clone> diff, bring them over, then remove the clone: grok worktree rm <id>.
# Run it in the background for parallel work:
#   powershell -File tools\grok_task.ps1 -Name vu1-loop -Mode ask -PromptFile <file>
$root = Split-Path -Parent $PSScriptRoot
$grok = (Get-Command grok -ErrorAction SilentlyContinue).Source
if (-not $grok) { $grok = Join-Path $env:USERPROFILE ".grok\bin\grok.exe" }
if (-not (Test-Path $grok)) { "grok not found (install Grok Build: irm https://x.ai/cli/install.ps1 | iex)"; exit 1 }
if ($PromptFile) { $Prompt = Get-Content -Raw -Encoding UTF8 $PromptFile }
if (-not $Prompt) { "no prompt (-Prompt or -PromptFile)"; exit 1 }
# Grok does not load the project's CLAUDE.md until the project is trusted, so the rules come with every task.
$rules = "You work on the Tekken 4 (PS2, PAL SCES-50878) static recompilation in this repository, alongside Claude. " +
    "Before anything else read CLAUDE.md (project rules, follow them) and docs/status.md (current state). Never commit or " +
    "publish game files, Ghidra databases, captured VU1 micro memories or generated C++; never send the disc files, ISO " +
    "or BIOS anywhere. PCSX2 is the ground truth for hardware behaviour. Say what you verified and what you assume."
if ($Mode -eq "work") {
    $rules += " Make the change in your working directory only; do not run commands or commit: Claude reviews your diff."
} else {
    $rules += " Do not modify files in the repository (scripts you write for analysis go in logs/grok/)."
}
# The prompt goes through a file: Windows PowerShell mangles double quotes inside native-command arguments.
$logDir = Join-Path $root "logs\grok"
New-Item -ItemType Directory -Force $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$promptPath = Join-Path $logDir ("{0}_{1}.prompt.md" -f $Name, $stamp)
[System.IO.File]::WriteAllText($promptPath, $Prompt, (New-Object System.Text.UTF8Encoding $false))
$grokArgs = @("--prompt-file", $promptPath, "--cwd", $root, "--rules", $rules, "--output-format", "plain", "--max-turns", "$MaxTurns")
if ($Model) { $grokArgs += @("--model", $Model) }
if ($Effort) { $grokArgs += @("--reasoning-effort", $Effort) }  # heavy analysis / coding tasks: -Effort high
if ($Mode -eq "ask") {
    # File edits need approval, which a headless run never gets; scripts (e.g. a disassembler) and read-only git may run.
    $grokArgs += @("--permission-mode", "default", "--allow", "Bash(python -I:*)", "--allow", "Bash(git log:*)",
        "--allow", "Bash(git show:*)", "--allow", "Bash(git diff:*)", "--allow", "Bash(git status:*)")
} else {
    $grokArgs += @("--worktree", $Name, "--permission-mode", "acceptEdits", "--allow", "Write", "--allow", "Edit")
}
$log = Join-Path $logDir ("{0}_{1}.md" -f $Name, $stamp)
$t0 = Get-Date
$answer = & $grok @grokArgs 2>&1 | ForEach-Object { "$_" }
$body = @("# Grok task: $Name ($Mode)", "", "## Prompt", "", $Prompt, "", "## Answer", "", ($answer -join "`n"))
if ($Mode -eq "work") {
    $clone = Get-ChildItem (Join-Path $env:USERPROFILE ".grok\worktrees\*\$Name") -Directory -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($clone) { $body += @("", "## Changes in $clone", "", '```', ((git -C $clone status --short 2>&1) -join "`n"), '```') }
    else { $body += @("", "## No clone named $Name found under ~\.grok\worktrees") }
}
$body += @("", ("({0:N0} s)" -f ((Get-Date) - $t0).TotalSeconds))
$body -join "`n" | Out-File -Encoding utf8 $log
"Grok task $Name done in {0:N0} s: $log" -f ((Get-Date) - $t0).TotalSeconds
