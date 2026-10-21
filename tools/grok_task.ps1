param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Prompt = "",
    [string]$PromptFile = "",
    [ValidateSet("ask", "work")][string]$Mode = "ask",
    [string[]]$Owns = @(),
    [string]$Model = "",
    [string]$Effort = "",
    [int]$MaxTurns = 150
)
# Hands one task to Grok (xAI's Grok Build CLI, `grok`) so it works in parallel with Claude, and saves the result in
# logs\grok\<Name>_<time>.md (prompt, answer, files changed).
#   ask:  analysis in the project (second opinions, reviews, research): it reads anything, runs python -I and read-only
#         git, and writes only under logs\grok\ (its scripts and notes).
#   work: a change in the main project: it may edit only the files given with -Owns (Claude keeps off them meanwhile)
#         and logs\grok\, and run only the build (into out\grok: TEKKEN4_BUILD_DIR=grok, so it never overwrites
#         Claude's out\runtime), the game capture script, python -I and read-only git. It never commits: Claude
#         reviews the diff and commits.
# A headless run cannot approve anything and a refused tool ends it without an answer, so each mode allows exactly what
# its tasks need. Run it in the background for parallel work, e.g.
#   powershell -File tools\grok_task.ps1 -Name pad2 -Mode work -Owns external/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/Pad.cpp -PromptFile <file>
$root = Split-Path -Parent $PSScriptRoot
$grok = (Get-Command grok -ErrorAction SilentlyContinue).Source
if (-not $grok) { $grok = Join-Path $env:USERPROFILE ".grok\bin\grok.exe" }
if (-not (Test-Path $grok)) { "grok not found (install Grok Build: irm https://x.ai/cli/install.ps1 | iex)"; exit 1 }
if ($PromptFile) { $Prompt = Get-Content -Raw -Encoding UTF8 $PromptFile }
if (-not $Prompt) { "no prompt (-Prompt or -PromptFile)"; exit 1 }
if ($Mode -eq "work" -and $Owns.Count -eq 0) { "work mode needs -Owns <files the task may modify>"; exit 1 }
# Grok does not load the project's CLAUDE.md until the project is trusted, so the rules come with every task.
$rules = "You work on the Tekken 4 (PS2, PAL SCES-50878) static recompilation in this repository, alongside Claude, who " +
    "works on other files at the same time. Before anything else read CLAUDE.md (project rules, follow them) and " +
    "docs/status.md (current state). Never commit or publish game files, Ghidra databases, captured VU1 micro memories " +
    "or generated C++; never send the disc files, ISO or BIOS anywhere. PCSX2 is the ground truth for hardware " +
    "behaviour. Do not run git commands that change the repository. Say what you verified and what you assume."
$allow = @("Write(logs/grok/**)", "Edit(logs/grok/**)", "Bash(python -I:*)", "Bash(git log:*)", "Bash(git show:*)",
    "Bash(git diff:*)", "Bash(git status:*)", "Bash(Get-Content:*)", "Bash(Select-String:*)", "Bash(Get-ChildItem:*)",
    "Bash(Test-Path:*)")  # read-only shell: Grok reads its long commands' output files with Get-Content
if ($Mode -eq "work") {
    $rules += " You may modify only these files: " + ($Owns -join ", ") + "; analysis scripts and notes go under logs/grok/. " +
        "Build with cmd /c tools\build_runtime.cmd (TEKKEN4_BUILD_DIR=grok is set: it builds external\PS2Recomp\out\grok) " +
        "and run the game with powershell -File tools\run_capture.ps1 -RunDir logs\grok\$Name-<n> (same build). On the " +
        "display-mode menu always pick PAL60 (keys 10:Down, 12:Cross). End with a summary: files changed, how you " +
        "tested, results."
    foreach ($f in $Owns) { $allow += @("Write($f)", "Edit($f)") }
    $allow += @("Bash(cmd /c tools\build_runtime.cmd:*)", "Bash(powershell -File tools\run_capture.ps1:*)")
    $env:TEKKEN4_BUILD_DIR = "grok"
} else {
    $rules += " Do not modify repository files: write your scripts and notes only under logs/grok/."
}
# The prompt goes through a file: Windows PowerShell mangles double quotes inside native-command arguments.
$logDir = Join-Path $root "logs\grok"
New-Item -ItemType Directory -Force $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$promptPath = Join-Path $logDir ("{0}_{1}.prompt.md" -f $Name, $stamp)
[System.IO.File]::WriteAllText($promptPath, $Prompt, (New-Object System.Text.UTF8Encoding $false))
$grokArgs = @("--prompt-file", $promptPath, "--cwd", $root, "--rules", $rules, "--output-format", "plain",
    "--max-turns", "$MaxTurns", "--permission-mode", "default")
foreach ($a in $allow) { $grokArgs += @("--allow", $a) }
if ($Model) { $grokArgs += @("--model", $Model) }
if ($Effort) { $grokArgs += @("--reasoning-effort", $Effort) }  # heavy analysis / coding tasks: -Effort high
$repos = @($root, (Join-Path $root "external\PS2Recomp"))
$before = @{}
foreach ($r in $repos) { $before[$r] = @(git -C $r status --short 2>$null) }
$log = Join-Path $logDir ("{0}_{1}.md" -f $Name, $stamp)
$t0 = Get-Date
$answer = & $grok @grokArgs 2>&1 | ForEach-Object { "$_" }
$body = @("# Grok task: $Name ($Mode)", "", "## Prompt", "", $Prompt, "", "## Answer", "", ($answer -join "`n"), "", "## Files changed")
foreach ($r in $repos) {
    $new = @(git -C $r status --short 2>$null | Where-Object { $before[$r] -notcontains $_ })
    $body += @("", "$r", '```', ($(if ($new.Count) { $new -join "`n" } else { "(none new)" })), '```')
}
$body += @("", ("({0:N0} s)" -f ((Get-Date) - $t0).TotalSeconds))
$body -join "`n" | Out-File -Encoding utf8 $log
"Grok task $Name done in {0:N0} s: $log" -f ((Get-Date) - $t0).TotalSeconds
