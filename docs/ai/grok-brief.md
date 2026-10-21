# grok-brief (AI-facing, dense; read FIRST, instead of all of docs/status.md)

## MAP
- game/ disc files + ELF SCES_508.78 (gitignored; read with python -I only; never copy out)
- external/PS2Recomp/ toolchain+runtime; branch dmac-mfifo HEAD = patch 01; working tree = patches 02..12 applied
- patches/NN-*.patch our PS2Recomp changes, applied in order by tools/build_runtime.cmd; patches/debug/* not applied
- runtime/tekken4_overrides.cpp game overrides (PS2_REGISTER_GAME_OVERRIDE); config/function_map.csv (Ghidra names)
- generated/ recompiled C++ of the whole ELF, per function (game code: never publish)
- external/PS2Recomp/out/runtime (Claude's build), out/grok (yours), out/test (tests); vu1n_generated/ in each
- docs/status.md long state log: `Select-String -Pattern '^## '` then read only the section you need
- logs/ runs (gitignored); logs/grok/ yours (notes, scripts, results: <task>_<time>.json)

## COMMANDS
- build: `cmd /c tools\build_runtime.cmd` (TEKKEN4_BUILD_DIR=grok set by the wrapper in work mode -> out\grok; ~3-10 min)
- tests: `cmd /c tools\build_tests.cmd` (shared out\test: only when the task says so)
- run+capture: `powershell -File tools\run_capture.ps1 -RunDir logs\grok\<x> -Keys k1,k2 -Shots s1,s2` -> stdout.txt stderr.txt shot_NNs.png
- keys: PAL60 `10:Down,12:Cross` | +main menu `15:Left,16.5:Cross,30:Start,36:Cross` | +Arcade fight `40:Down,41.5:Cross,46:Cross` (fight ~49 s, stall ~105 s)
- PCSX2 truth: `powershell -File tools\pcsx2_capture.ps1 -OutDir <d> -Keys .. -Shots ..`; RAM while it runs: `python -I tools\pine.py read32 <a>..` / `dump <a> <len> --out <f>`
- VU1 generator: `python -I tools/vu1/vu1n_gen.py <out> game/vu1/micro --parts 6`
- MFIFO ring: `powershell -File tools\mfifo\pcsx2_ring_probe.ps1` (PCSX2 ring+SPR dumps in the fight -> logs/pcsx2_ring_probe);
  `python -I tools/mfifo/ring_scan.py <dump> <base hex>` (list-end patterns), `python -I tools/mfifo/ring_chain.py <ring dump>` (walk tags)
- reviews / process: docs/ai/reviews/*.json (mutual reviews), `python -I tools/ai/session_digest.py <transcript.jsonl> <out>`

## ENV (runtime)
TEKKEN4_PERF_LOG=<s> fps+GS ms/s | TEKKEN4_PROFILE=<start>:<s> sampled hot functions | TEKKEN4_GS_DUMP=<frame> GIF packets |
TEKKEN4_MOVIE_LOG TEKKEN4_PRESENT_LOG TEKKEN4_WATCH_MS TEKKEN4_IRQ_LOG | TEKKEN4_VU1N=0 build without native VU1 |
PS2X_GS_THREADS=n|auto | GOW_VU1_NATIVE_OFF=1 interpreter | GOW_VU1_CAPTURA=<dir> micro memories | GOW_VU1_STATS=1 |
GOW_VU1N_DIAG=1 misses | GOW_VU1N_VERIFY=<runs> +GOW_VU1N_BISECT=1 +GOW_VU1N_DUMP=1 native vs exact | GOW_VU1_LONGRUNS=<cyc> |
GOW_VU1N_CHECKLONG=1 budget runs re-run exact (+debug patch: [vif1-bad] VIF/DMA trace) | GOW_VU1N_RECORD/REPLAY=<f> bit-exact

## FACTS (verified; ref = docs/status.md section unless noted)
- ELF entry 0x180088, .text 0x180080-0x2a91f8, stripped ee-gcc C++, 22 .DVP.overlay VU sections
- display: interlaced FRAME mode, display copy fb0 CT32 640x448 -> FBP 0xd2 CT24; odd field 1 line lower (patches 03,06)
- VU1: game uploads ONE micro memory (0x0-0x3e88) -> 220 native blocks; vf0=(0,0,0,1)/vi0=0 hardwired (Tekken writes vf0 for flags: OPMSUB vf0 = backface test); native lacks Q/P latency + status flags
- fight: native VU1 11-25 fps, interpreter 7-9; OPEN stall ~4 s into fight ("Native VU1 port" -> "Cause traced")
- VIF1 = MFIFO: ring D_RBOR 0x100000 / D_RBSR 0x7FFF0 (game FUN_00232e48); fromSPR (normal mode) copies SPR packets (SADR 0x0050/0x2120/0x2ed0) into the ring; drain = PS2Memory::drainMfifo (ps2_memory.cpp): waits for a whole packet (PCSX2 mfifoVIF1transfer streams); CHCR.TTE on (tag hi 64 bits go to VIF, also mid-UNPACK)
- game MFIFO fallback: FUN_00180270 restarts VIF1 at saved TADR; FUN_002332a0: suspend, STR off, resume, D_CTRL.MFD=0, CHCR start (normal chain)
- sound: IOP modules = ELF 0x2e4900 package (LZ, bitwise NOT, 6 IRX: sio2man padman mcman mcserv libsd zsiopdrv); zsiopdrv serves RPC 0x12345 (today a silent override)
- movies: MPEG-2 in DATA.BIN streams (0x27 video + 2 SPU sectors per unit), driver calls 0x3000/0x3200/0x3100
- IPU still images decoded through FFmpeg "ipu" (patch 10); pad: empty port must read scePadStateDiscon (your Pad.cpp fix, under review)

## RULES (CLAUDE.md, short)
never commit/publish game files, Ghidra DBs, VU1 micro memories, generated C++; never send disc/ISO/BIOS anywhere; per-game fixes = game overrides, hardware behaviour = PS2Recomp patch; PCSX2 is ground truth; one goal per task; python -I on game files; no git command that changes a repo (Claude commits)

## PROTOCOL (Claude <-> Grok, AI-to-AI)
- prompt blocks: TASK | CTX (refs) | FACTS | ASK | LIMITS | DOD (definition of done)
- answer: JSON forced by --json-schema: verdict, conf 0..1, facts[{c,ref,v}], fix[{where,change}], checks[], open[], files_changed[]; only the final object counts
- A1 check every proposed fix against EVERY number/address in the provided logs; contradictions -> open[]; conf<=0.5 if not run
- A2 this brief first; status.md only the named section; prior logs/grok notes before re-deriving
- A3 targeted reads (grep to the function, read +-60 lines); PCSX2 source: only the named functions
- A4 work mode: DOD met -> notes file + final JSON, stop
- A5 v=true only for what you observed (log line, run) or read in code this session; separate observed / inferred / assumed
- A6 missing tool or failed command -> open[], continue with what is possible
- G1 no fix unless FACTS hold the numbers it must match; else the missing lines -> open[], conf<=0.5
- G2 one mechanism in the verdict; another mechanism fitting the same bytes -> open[]
- G3 work mode: DOD met -> notes + final JSON, stop (no extra run, no extra reading)
- G4 CPU: before a big script (>~1 min CPU: ELF / DATA.BIN / generated C++ scans) check logs\run.lock; while it holds
  a live PID (a timed game / PCSX2 run) wait or do light work first. Builds wait by themselves (build_runtime.cmd)
- contra[]: every contradiction between your fix/claims and the given logs or code (empty only if you checked)
- ROLES / handoff / verification / run lock / disagreement: docs/ai/collaboration.md
