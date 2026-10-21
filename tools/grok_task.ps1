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
# Hands one task to Grok (xAI's Grok Build CLI, `grok`) so it works in parallel with Claude. Grok <-> Claude exchanges
# are AI-to-AI: prompts are terse keyed blocks (TASK / CTX / FACTS / ASK / LIMITS) and Grok answers with a JSON object
# forced by --json-schema (verdict, conf, facts[{c,ref,v}], fix[{where,change}], checks[], open[]). Result:
# logs\grok\<Name>_<time>.json = {task, mode, answer, files_changed, turns, cost_usd, seconds, session}.
#   ask:  analysis in the project (second opinions, reviews, research): reads anything, runs commands, writes only
#         under logs\ (its scripts and notes in logs\grok\).
#   work: a change in the main project, limited to the files given with -Owns (Claude keeps off them meanwhile): it
#         builds into out\grok (TEKKEN4_BUILD_DIR=grok, so it never overwrites Claude's out\runtime) and runs the game
#         into logs\grok\. It never commits: Claude reviews the diff and commits.
# Permissions: Grok's auto mode (its own check approves safe commands; a headless run cannot ask), plus deny rules: no
# writes in top-level folders the task has no business in, no git command that changes a repository. A denied action
# is reported to Grok, which carries on. Run it in the background for parallel work, e.g.
#   powershell -File tools\grok_task.ps1 -Name pad2 -Mode work -Owns external/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/Pad.cpp -PromptFile <file>
$root = Split-Path -Parent $PSScriptRoot
$grok = (Get-Command grok -ErrorAction SilentlyContinue).Source
if (-not $grok) { $grok = Join-Path $env:USERPROFILE ".grok\bin\grok.exe" }
if (-not (Test-Path $grok)) { "grok not found (install Grok Build: irm https://x.ai/cli/install.ps1 | iex)"; exit 1 }
if ($PromptFile) { $Prompt = Get-Content -Raw -Encoding UTF8 $PromptFile }
if (-not $Prompt) { "no prompt (-Prompt or -PromptFile)"; exit 1 }
if ($Mode -eq "work" -and $Owns.Count -eq 0) { "work mode needs -Owns <files the task may modify>"; exit 1 }
# Grok does not load the project's CLAUDE.md until the project is trusted, so the rules come with every task.
$rules = "PROJECT: Tekken 4 PS2 PAL SCES-50878 static recomp (PS2Recomp). PEER: Claude works on other files now. FIRST: read " +
    "CLAUDE.md (rules, obey) + docs/status.md (state). NEVER: commit/publish game files, Ghidra DBs, VU1 micro memories, " +
    "generated C++; send disc/ISO/BIOS anywhere; git commands that change a repo (Claude commits). TRUTH: PCSX2 behaviour. " +
    "PY: python -I. SCRATCH: logs/grok/. OUTPUT: read by an AI, not a human: max density, no narration, no pleasantries, " +
    "no markdown; only the final JSON object counts (interim turns discarded); every fact needs ref (file:line | addr | " +
    "url | log:line) and v=true only if you checked it yourself this session; conf = 0..1 for the verdict."
# Top-level entries the task may not write: everything but logs\ and the folders holding its own files.
$open = @("logs") + @($Owns | ForEach-Object { ($_ -replace '\\', '/').Split('/')[0] })
$deny = @()
foreach ($e in Get-ChildItem -Force $root | Where-Object { $_.Name -ne ".git" -and $open -notcontains $_.Name }) {
    $pattern = if ($e.PSIsContainer) { "$($e.Name)/**" } else { $e.Name }
    if ($pattern -match '[(),]') { $pattern = "*" + $e.Extension }  # e.g. the ISO: rule syntax has no quoting
    if ($deny -notcontains "Write($pattern)") { $deny += @("Write($pattern)", "Edit($pattern)") }
}
foreach ($c in "commit", "push", "reset", "checkout", "switch", "restore", "clean", "stash", "rebase", "merge", "apply",
    "am", "rm", "mv", "add", "tag", "branch -d", "branch -D", "worktree") { $deny += "Bash(git ${c}:*)" }
if ($Mode -eq "work") {
    $rules += " MODE: work. EDIT ONLY: " + ($Owns -join ", ") + " (+ logs/grok/). BUILD: cmd /c tools\build_runtime.cmd " +
        "(TEKKEN4_BUILD_DIR=grok set -> external\PS2Recomp\out\grok). RUN: powershell -File tools\run_capture.ps1 -RunDir " +
        "logs\grok\$Name-<n> -Keys ... -Shots ... (same build; display-mode menu: always PAL60 = 10:Down,12:Cross). " +
        "files_changed + checks must list what you changed and how you tested."
} else {
    $rules += " MODE: ask. No repository edits."
}
# The answer shape, forced by --json-schema (compact keys: c = claim, v = verified by you).
$schema = '{"type":"object","properties":{' +
    '"verdict":{"type":"string"},"conf":{"type":"number"},' +
    '"facts":{"type":"array","items":{"type":"object","properties":{"c":{"type":"string"},"ref":{"type":"string"},"v":{"type":"boolean"}},"required":["c","ref","v"]}},' +
    '"fix":{"type":"array","items":{"type":"object","properties":{"where":{"type":"string"},"change":{"type":"string"}},"required":["where","change"]}},' +
    '"checks":{"type":"array","items":{"type":"string"}},"open":{"type":"array","items":{"type":"string"}},' +
    '"files_changed":{"type":"array","items":{"type":"string"}}},' +
    '"required":["verdict","conf","facts","fix","checks","open","files_changed"]}'
# Prompt via a file and \" in the schema: Windows PowerShell mangles double quotes inside native-command arguments.
$logDir = Join-Path $root "logs\grok"
New-Item -ItemType Directory -Force $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$promptPath = Join-Path $logDir ("{0}_{1}.prompt.md" -f $Name, $stamp)
[System.IO.File]::WriteAllText($promptPath, $Prompt, (New-Object System.Text.UTF8Encoding $false))
$grokArgs = @("--prompt-file", $promptPath, "--cwd", $root, "--rules", $rules, "--json-schema", ($schema -replace '"', '\"'),
    "--max-turns", "$MaxTurns", "--permission-mode", "auto")
foreach ($d in $deny) { $grokArgs += @("--deny", $d) }
if ($Model) { $grokArgs += @("--model", $Model) }
if ($Effort) { $grokArgs += @("--reasoning-effort", $Effort) }  # heavy analysis / coding tasks: -Effort high
$repos = @($root, (Join-Path $root "external\PS2Recomp"))
$before = @{}
foreach ($r in $repos) { $before[$r] = @(git -C $r status --short 2>$null) }
$t0 = Get-Date
$raw = (& $grok @grokArgs 2>&1 | ForEach-Object { "$_" }) -join "`n"
# text holds one JSON object per assistant turn: the last complete one is the answer.
$answer = $null; $meta = $null
try {
    $meta = $raw.Substring($raw.IndexOf('{'), $raw.LastIndexOf('}') - $raw.IndexOf('{') + 1) | ConvertFrom-Json
    $text = [string]$meta.text
    $depth = 0; $start = -1; $last = $null; $inStr = $false; $esc = $false
    for ($i = 0; $i -lt $text.Length; $i++) {
        $ch = $text[$i]
        if ($inStr) { if ($esc) { $esc = $false } elseif ($ch -eq '\') { $esc = $true } elseif ($ch -eq '"') { $inStr = $false }; continue }
        if ($ch -eq '"') { $inStr = $true } elseif ($ch -eq '{') { if ($depth -eq 0) { $start = $i }; $depth++ }
        elseif ($ch -eq '}') { $depth--; if ($depth -eq 0 -and $start -ge 0) { $last = $text.Substring($start, $i - $start + 1) } }
    }
    if ($last) { $answer = $last | ConvertFrom-Json }
} catch { }
$changed = @()
foreach ($r in $repos) {
    $rel = if ($r -eq $root) { "" } else { "external/PS2Recomp/" }
    $changed += @(git -C $r status --short 2>$null | Where-Object { $before[$r] -notcontains $_ } | ForEach-Object { $rel + $_.Substring(3) })
}
$result = [ordered]@{
    task = $Name; mode = $Mode; answer = $(if ($answer) { $answer } else { $raw }); files_changed = $changed
    turns = $(if ($meta) { $meta.num_turns }); cost_usd = $(if ($meta) { $meta.total_cost_usd })
    seconds = [int]((Get-Date) - $t0).TotalSeconds; session = $(if ($meta) { $meta.sessionId })
}
$log = Join-Path $logDir ("{0}_{1}.json" -f $Name, $stamp)
[System.IO.File]::WriteAllText($log, ($result | ConvertTo-Json -Depth 12 -Compress), (New-Object System.Text.UTF8Encoding $false))
"grok $Name done {0}s parsed={1}: $log" -f $result.seconds, [bool]$answer
