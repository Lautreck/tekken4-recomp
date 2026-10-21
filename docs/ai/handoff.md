# handoff (Claude -> Grok while Claude is out of usage; AI-facing, dense)
Run: `powershell -File tools\grok_lead.ps1` (steps below, in order; stops at the first step whose JSON has contra[],
fails to parse, or a work step that changes no owned file). Journal: logs\grok\lead_journal.json.
Base state: last pushed commit on origin/master (repo) + external/PS2Recomp = patches 01-13 applied, clean.
Grok never commits, pushes, opens PRs or publishes; all changes stay uncommitted for Claude to review.
Each step reads docs/ai/grok-brief.md + docs/ai/collaboration.md first.

### STEP stall-writers | ask |
TASK: confirm the EE functions behind the fight stall packet bug (CNT qwc=4 swallowing 2 REF texture-upload tags).
CTX: docs/status.md "Native VU1 port" -> "Root cause found one level up"; logs/grok/cnt-qwc_*.json (your earlier answer,
read first); logs/ringdump_fight1/stderr.txt lines "[vif1-bad]   ring"; logs/pcsx2_ring_probe/ring_70.bin.
ASK: 1. the texture-upload writer and the CNT open/close code (addresses, generated/ file:line); 2. caller context
(main loop / interrupt handler / DMA callback / thread) and trigger; 3. one mechanism for qwc=4 in our runtime and
the exact addresses + registers to log to prove it.
LIMITS: ask mode. DOD: JSON with fix = the log points (function address + what to print).

### STEP stall-hook | work | runtime/tekken4_overrides.cpp
TASK: add a debug hook (env TEKKEN4_PKT_LOG=1) in runtime/tekken4_overrides.cpp that wraps the functions named by
step stall-writers (registerFunction wrap like the existing TEKKEN4_MOVIE_LOG wrapper of FUN_00228520) and logs each
call: function, ra, the packet pointer / CNT tag address and qwc it writes, and whether it runs inside an interrupt
handler (the runtime's interrupt depth if available).
CTX: logs/grok/lead-stall-writers_*.json; runtime/tekken4_overrides.cpp (existing wrappers, TEKKEN4_* switches).
LIMITS: edit only runtime/tekken4_overrides.cpp; build `cmd /c tools\stage_generated.cmd` is NOT needed (overrides are
staged by build_runtime? check tools\build_runtime.cmd; if staging is needed run `cmd /c tools\stage_generated.cmd`);
build `cmd /c tools\build_runtime.cmd`; run `powershell -File tools\run_capture.ps1 -RunDir logs\grok\stall-hook-1
-Keys 10:Down,12:Cross,15:Left,16.5:Cross,30:Start,36:Cross,40:Down,41.5:Cross,46:Cross -Shots 100,110` with
TEKKEN4_PKT_LOG=1 (and GOW_VU1N_CHECKLONG=1).
DOD: build ok; stderr shows the logged calls around the first stalled frame (~105 s); notes file logs/grok/stall-hook.md
with the call order that produces CNT qwc=4; JSON files_changed lists the override file.
