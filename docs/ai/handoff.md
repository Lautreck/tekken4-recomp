# handoff (Claude -> Grok while Claude is out of usage; AI-facing, dense)
Run: `powershell -File tools\grok_lead.ps1` (steps below, in order; stops at the first step whose JSON has contra[],
fails to parse, or a work step that changes no owned file). Journal: logs\grok\lead_journal.json.
Base state: origin/master + external/PS2Recomp = patches 01-13 applied, clean. Every task is checkpointed on grok/wip.
Grok never commits, pushes, opens PRs or publishes; all changes stay uncommitted for Claude to review.
Each step reads docs/ai/grok-brief.md + docs/ai/collaboration.md first.

## DONE (not rerun)
- cnt-qwc (logs/grok/cnt-qwc_20261021_221410.json, conf 0.5): CNT qwc=4 = FUN_00292c18 ADDs ((cur-tag)>>4)-1 onto a
  qwc the PS2Recomp stubs sceVif1PkAddGsAD / closePacketGifTag already SET to 2 (refreshPacketBuilderPendingCount,
  Stubs/GS.cpp); builder FUN_002344e0; our override closeDmaTag (SET instead of ADD) is registered only at 0x292900.

### STEP stall-fix | work | runtime/tekken4_overrides.cpp
TASK: fix the fight stall: in runtime/tekken4_overrides.cpp apply(), next to registerFunction(0x292900, closeDmaTag),
also register closeDmaTag for 0x292c18 (same pk+8 open-tag, qwc = ((cur-tag)>>4)-1 SET, not ADD; check that
FUN_00292c18's register use / return matches what closeDmaTag expects: read generated/FUN_00292c18_0x292c18.cpp and
the existing closeDmaTag; if they differ, write a closeDmaTag variant for it). Add debug log behind env
TEKKEN4_PKT_LOG=1 at that hook: pk, cur, tag addr, word0 before/after, ra (first 20 calls).
CTX: logs/grok/cnt-qwc_20261021_221410.json (fix[0..1], facts); runtime/tekken4_overrides.cpp:601-626 (closeDmaTag),
:873 (registration); docs/status.md "Native VU1 port".
FACTS: stall ~4 s into the first fight (~105 s into run_capture): fps -> 0, VU1 program 0x32a0 runs to its 65536-cycle
budget ("[vu1-long] startPc=32a0 ... budget" with GOW_VU1_LONGRUNS=40000). Our ring: 0013d290 0000000010000004
5000000200000000 = CNT qwc=4 NOP;DIRECT 2 followed by GIF 1000000000008001/000000000000000e + A+D TEXFLUSH (2 qwords).
BUILD: `cmd /c tools\stage_generated.cmd` (copies runtime\*_overrides.cpp into PS2Recomp) then
`cmd /c tools\build_runtime.cmd` (out\grok). RUN (holds the run lock; set env first):
TEKKEN4_PERF_LOG=300 GOW_VU1_LONGRUNS=40000 TEKKEN4_PKT_LOG=1, then
`powershell -File tools\run_capture.ps1 -RunDir logs\grok\stall-fix-1 -Keys 10:Down,12:Cross,15:Left,16.5:Cross,30:Start,36:Cross,40:Down,41.5:Cross,46:Cross -Shots 40,48,90,110,130,150`.
LIMITS: edit only runtime/tekken4_overrides.cpp (+ notes under logs/grok/); do not touch external/PS2Recomp sources.
DOD: build ok; stderr has NO "[vu1-long] startPc=32a0 ... budget" line; "perf second" lines 100..150 all > 0 frames;
shot_130s/150s show the fight still running (round timer below 56, fighters drawn); shot_40s main menu and shot_48s
character select look like before (menus use the 0x292900 path); notes logs/grok/stall-fix.md (change, log excerpt,
fps per second 90..150). If the stall remains, put the first [vu1-long] line and the TEKKEN4_PKT_LOG lines before it in
open[] verbatim.
