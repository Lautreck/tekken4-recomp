# Status

_Last updated: 2026-10-21 (end of session 3, stopped at the user's 80% usage)_

## Resume here
To continue, open Claude Code in this folder and say: "read docs/status.md and continue".
- Done in session 1: toolchain installed, PS2Recomp tools built, analyzer seed + full recompile with 0 errors,
  Ghidra project (`ghidra/Tekken4`, non-shared) analysed with SDK names applied, Ghidra export recompiled.
- Done in session 2: Ghidra MCP tools approved and working in Claude Code. **Runtime base decided: upstream
  ps2xRuntime** (cherry-pick from the GoW fork at GS/VU1 time). ps2EntryRunner builds and runs; first stop found
  (see "First run"); 350 missing functions added (4 constructors + 346 vtable/callback targets) (see "Missing
  functions fix"); silent sound-driver override (see "Sound driver override"); MFIFO + D_ENABLE runtime
  patch (see "MFIFO + D_ENABLE patch"). Milestone 4 published (see "Publishing").
- Done in session 3 (stopped at the user's 80% usage report): milestone 5 (menu with all text, PAL60 picked as controller 1, Namco logo); the DMA/VIF1
  fixes upstreamed as PR #373 (PATH2 continuation redone to keep upstream's tests passing), pk stub issue #374,
  milestone 5 page (see "Publishing"). Display fixed: interlaced FRAME mode now shows the half-height buffer
  line-doubled, full screen like PCSX2 (patch 03 = PR #375, see "Display line repeat").
  The intro movie now plays (see "Intro movie").
  Strip gaps fixed (patch 05, see "Sprite strip gaps").
  Vibrating picture fixed (patch 06, see "Field offset").
  After the movie (Start skips it) the first-run difficulty screen works (see "Difficulty screen").
  Its dashed lines are fixed too (patch 08).
  **Milestone 6 reached:** Cross on MEDIUM opens the main menu (MODE SELECT: STORY BATTLE .. OPTIONS), as in PCSX2.
  Frame rate fixed: 60 fps on every screen so far (patch 09, see "GS speed").
  Character select works (IPU images decoded through FFmpeg, see "Character select").
  A fight loads and renders (Stage 1, Kazuya vs Law) but at ~7 fps: the VU1 interpreter (see "First fight").
  Native VU1 port started (stopped at the user's 95% usage report): the GoW VU1 engine is in (patch 11, 465/465
  tests), no native blocks generated yet (see "Native VU1 port").
- **Resume at: "Native VU1 port", "Cause traced" -> Next (PCSX2 ring read at 0x13d350, then the drainMfifo fix).**
- **Main config is now `config/ghidra_export.toml`**, reading `config/function_map.csv` (Ghidra functions + `extra_functions.csv`). The analyzer seed is kept for reference.
- Build loop: `ps2_recomp config/ghidra_export.toml` -> `tools\stage_generated.cmd` -> `tools\build_runtime.cmd`
  (~3.5 min clean; applies `patches\*.patch` first) -> `tools\run_game.cmd` (close the window to stop; output in `logs\run.log`).
- Look at it: `powershell -File tools\run_capture.ps1 -Shots 8,25` (our build: hides the debug panel, screenshots +
  stdout in `logs\run_<time>\`), `powershell -File tools\pcsx2_capture.ps1 -Shots 15` (same in the user's PCSX2,
  the ground truth). Runtime tests: `tools\build_tests.cmd` (PS2Recomp's ps2x_tests with all patches, 465/465).
- Not in git (rebuild/re-extract if lost): `external/` (clones + Ghidra + PCSX2 + MCP venv, see Pinned
  dependencies), `game/disc/` (7-Zip from the ISO), `generated/` (rerun `ps2_recomp config/analyzer_seed.toml`).

## Milestones
- [x] 0. Project scaffold, disc files extracted, ELF inspected
- [x] 1. Toolchain installed: VS 2022 Build Tools 17.14 (MSVC), CMake 4.4.4, Ninja 1.13.2, Temurin JDK 21.0.12
      (JAVA_HOME set), Ghidra 12.1.4 + EE plugin v2.1.38 + GhidraMCP 2.0, PCSX2 2.8.2 portable
- [x] 2. PS2Recomp host tools build from source (`tools\build_ps2recomp_tools.cmd`)
- [x] 3. Ghidra analysis + function map exported, recompiled output compiles and links into ps2EntryRunner
- [x] 4. Boots past startup without crashing (main loop runs at ~47 fps, 2026-10-21)
- [x] 5. Something on screen (Namco logo / first menu) (2026-10-21: the display-mode menu renders, PAL60 picked
      as controller 1, confirmation answered, Namco logo shows; vertical scaling still off)
- [x] 6. Menus and controller input (2026-10-21: boot -> PAL60 -> movie -> difficulty -> main menu, matching PCSX2)
- [ ] 7. A fight: two characters on a stage, correct rendering, steady frame rate
- [ ] 8. Sound, saves, PC features

## Pinned dependencies
| Repo | Commit | Date |
|---|---|---|
| ran-j/PS2Recomp | eb8688b (local branch `dmac-mfifo` = eb8688b + PR #372) | 2026-10-14 |
| Waffle565/God-Of-War-Native-Port | 514f891 | 2026-10-18 |
| chaoticgd/ghidra-emotionengine-reloaded | v2.1.38 (asset for Ghidra 12.1.4) | 2026-10-04 |

## ELF findings (SCES_508.78)
- Stripped: no symbol table and no source-file strings, so names come from Ghidra analysis plus
  PS2Recomp's embedded SCE SDK symbol database (`ps2_analyzer` can match SDK functions).
- Section headers are intact, so section boundaries are reliable.
- 22 `.DVP.overlay` sections = VU microcode uploaded at runtime, so VU1 is heavily used (expected hard part).
- `libmpeg` + `libipu` present: FMVs are decoded through the IPU.
- No `libsd`/`libsdr` version strings: sound is probably a Namco IOP driver loaded from DATA.BIN.
- `.bss` is ~27 MB: the game statically claims most of EE RAM.
- PAL build: check whether a 60 Hz mode exists; frame timing is critical for a fighting game.

## First pass: analyzer seed + recompile (2026-10-20)
- `ps2_analyzer game/disc/SCES_508.78 config/analyzer_seed.toml` (1 s): 4513 functions, 567 SCE SDK
  matches from the embedded DB (399 renamed, 168 added), 224 stubs with runtime handlers, 339 SDK functions
  without handlers, **0 instruction patches** (so the GoW "SMC NOP patch" problem doesn't apply), only 1 jump table found.
- Entry `0x180088` calls `_InitSys`, `sub_00281260`, `sub_001969F8`.
- Seed TOML's `output` changed from `config/output/` to `generated/` (gitignored).
- `ps2_recomp config/analyzer_seed.toml` (3 s): 4372 recompiled + 141 stubs, 0 decode failures,
  0 unhandled instructions, 0 errors. 4527 files / 89 MB in `generated/`.
- 232 warnings, all "unresolved JR/JALR" in 116 functions (mostly switch tables), handled by fallback
  entries (27156). Ghidra's jump-table recovery should cut these down.
- Recompiler reports `gp=0x0`; crt0 presumably sets $gp at runtime. Check this if small-data accesses look wrong.

## Ghidra pass (2026-10-20)
- Ghidra 12.1.4 + EE plugin, Standard Defaults analysis (Decompiler Switch Analysis on, Aggressive Instruction
  Finder off): 4388 functions, all `FUN_` (Function ID named nothing).
- SDK names: `tools/ghidra_apply_sdk_names.py --apply` (via GhidraMCP HTTP) renamed 494 + 3; then
  `tools/ghidra_scripts/ApplySdkNames.java` (run from Script Manager) created 71 functions Ghidra had missed
  (kernel handlers, unused SDK code, C++ runtime). Now 4459 functions, 563/567 SDK names applied.
- 4 SDK functions sit inside other Ghidra functions and need a manual split check:
  `_initSeq` 0x0028c840, `_markOutput` 0x002914d8, `__eh_rtime_match` 0x0029be10, `__cp_exception_info` 0x0029ebb8.
- The export was taken before the last 3 renames: re-export (and rerun the post-process) next time in Ghidra.
- After each export run `python -I tools/postprocess_ghidra_export.py` (repo-relative paths, output to
  generated/, writes the functions-only CSV).

| Function map | Functions | Stubs | Unique unresolved JR sites | Output |
|---|---|---|---|---|
| analyzer seed | 4513 | 141 | 169 | 89 MB |
| Ghidra export incl. 23.5k code labels | 28004 | 273 | 312 | 286 MB |
| **Ghidra functions only (current)** | 4459 | 273 | 155 | ~80 MB |

- Correction to an earlier assumption: the export CSV has no jump-table data, so Ghidra's switch analysis does
  not resolve JR sites for the recompiler. The extra warnings with labels came from case/entry labels that
  start mid-function (131 of 152 only appear under `entry_*`, the rest under `caseD_*`/`default`).
  Unresolved JR sites still run via fallback dispatch. Real fix later: feed jump tables into `[jump_tables]`.

## Lessons from the God of War port (Waffle565)
Their runtime is a full PS2Recomp fork on the repo's `origin/runtime` branch (read with
`git -C external/God-Of-War-Native-Port show origin/runtime:<path>`); `main` holds docs, tools and GoW overrides.
- They don't use Ghidra for the function map: `tools/find_functions.py` (capstone heuristic) writes the CSV.
- Drop ps2_analyzer's "self-modifying code" NOP patches: all 73 in GoW were ordinary stores into
  writable+executable .data, and one broke loading. (Tekken's .text segment is also RWX.)
- Turn off `PS2X_ENABLE_AGRESSIVE_LOGS` and the other log options (default ON): 44% of time was spent in WriteFile.
- Upstream puts RPC/TLS pools inside the game heap and corrupts it: they moved them via `GuestReservedLayout`.
  Tekken's ~27 MB .bss makes this even more likely to bite.
- Recompiler bugs they hit (diffing vs PCSX2): 64-bit compares in BLTZ/BGEZ family, SQRT.S/RSQRT.S operands,
  DIV.S by zero = FLT_MAX, CVT.W.S truncation, MMI PMULTH, 15-bit `rand` stub. Check whether upstream has these.
- Disc extraction must be sector-exact: streaming IRX modules read raw sectors (their `tools/iso_extract.py --layout`).
- VU1: PCSX2 VU1 interpreter as reference + `tools/vu1/vu1n_gen.py` compiling captured micro-memory to C++
  per basic block. MAC/clip flags have 4-cycle latency across blocks. Reusable for our 22 overlays.
- IOP: `tools/iop/iopn_gen.py` AOT-compiles the game's IRX modules; SPU2 replaced at the libsd export boundary.
  Only carries over if Tekken's sound driver goes through libsd.
- GS: OpenGL 4.3 hardware renderer (raylib/GLFW window). MPEG: vendored libmpeg2 behind sceMpeg* stubs.
- Timing: VBlank host-paced at 60 Hz; guest time drifting from host time in poll loops slowed their FMVs.
- Never run two builds at once; touching a precompiled header costs a 20-40 min rebuild.
- Licensing: PS2Recomp and their runtime are GPL-3.0; PCSX2 VU code GPL-3.0+, libmpeg2 GPL-2+.

## Open questions
- Which IRX modules does the game load, and from where (IOPRP243.IMG vs DATA.BIN)?
- DATA.BIN / DATA2.BIN archive format (needed for the file I/O layer).
- How the PAL build handles 50/60 Hz.

## Runtime build + first run (session 2, 2026-10-20)
- Upstream ps2xRuntime at eb8688b, built by `tools\build_runtime.cmd` into `external/PS2Recomp/out/runtime`
  (RelWithDebInfo, so no /GL or LTCG; unity build + PCH; 259 Ninja steps, ~3.5 min on 8 cores). Downloads raylib 5.5,
  imgui, rlImGui and a prebuilt LGPL FFmpeg on first configure. Output: 47 MB exe + 191 MB pdb.
- Logs: `AGRESSIVE_LOGS` OFF (per-function trace with flush, the GoW slowdown); `RUNTIME_LOGS` and
  `IOP_RPC_TRACE` ON while chasing boot problems. Turn them off for speed later (full rebuild).
- Upstream expects the generated code inside its tree: `tools\stage_generated.cmd` mirrors `generated\*.cpp`
  into `ps2xRuntime/src/runner` (overwrites upstream's placeholder `register_functions.cpp`) and the two
  `ps2_recompiled_*.h` headers into `ps2xRuntime/include`.
- Paths: cdrom0:, host: and the ELF dir all map to `game/disc/`; mc0 is `game/disc/mc0/`. Raw `sceCdRead`
  LBNs only resolve inside files first found via `sceCdSearchFile` (pseudo-LBNs), unless `IoPaths::cdImage`
  points at an ISO (nothing sets it upstream; would need a game override).
- **First stop:** the game thread ends at `[guest-branch:missing-target] JALR source=0x29b27c target=0x26e078`.
  `FUN_0029b1f0` walks a table of code pointers at ~0x2a9840 backwards (0x27cec0, 0x27a318, 0x274218, 0x270b08
  ran, then 0x26e078). That is the C++ static-constructor list (`.ctors`, `__do_global_ctors`). 0x26e078 has no
  function in Ghidra: constructors only reached through that table were never discovered. The window stays up
  after the game thread dies. Startup before that was fine: _InitSys, sub_281260, sub_1969F8 -> ctors.

## Missing functions fix (session 2, 2026-10-20)
- There is no `.ctors` section: ee-gcc's lists are in `.data` as `[count, ptr * count, 0]`. Constructors at
  0x2a9830 (12 entries), destructors at 0x2a9868 (1). 4 constructors had no function (0x1fe8e8, 0x23dda0,
  0x2492e0, 0x26e078): small leaf loops that zero static arrays.
- `tools/find_missing_functions.py --data-pointers --write` finds those plus every aligned pointer in
  .data/.rodata/.sdata that lands in .text no function covers (vtable methods; many are 8-byte `jr ra; nop`).
  It traces each one through its branches to find the end and stops at the next known function.
  350 found; all 350 start right after a return/jump (+ padding) or a non-returning call, so none is the
  tail of a neighbour. Writes `config/extra_functions.csv` (committed).
- `tools/postprocess_ghidra_export.py` now writes `config/function_map.csv` = Ghidra functions + extras, and
  the TOML's `ghidra_output` points at it. `ghidra_functions_only.csv` stays Ghidra-only so the finder's
  input doesn't change when extras are added. These functions don't exist in Ghidra yet (no MCP endpoint
  creates functions); a Ghidra script could import extra_functions.csv later.
- Recompile: 4809 functions (4536 recompiled + 273 stubs), 0 errors, 218 unresolved-JR warnings. Rebuild ~3 min.
- **Second run:** constructors pass. `SifInitRpc`, one `sceSifSendCmd cid=0x80000001`, then the game thread
  sits in `FUN_002230d0` (0x2230f0): `while (sceSifBindRpc(&cd@0x1cccbc0, 0x12345, 0) >= 0 && cd.server == 0)
  sceGsSyncV(0);`. Caller `FUN_00223418` then calls RPC function 0x8000 and sets up 4 channels and volumes:
  this is **Namco's sound driver** (IOP side, SID 0x12345). Not a crash: a wait for an IOP server that never appears.
- Why it never binds: upstream `SifBindRpc` only binds when `PS2IopTransport::canBindRpc(sid)` says so, i.e.
  `ps2xIOP`'s `IopSubsystem` has an HLE route for the SID or its emulated IOP has an IRX that registered it
  (`ps2xIOP/src/iop_subsystem.cpp:188`). Neither knows 0x12345.
- The ELF has no `.IRX` file names except `cdrom:\IRX\IOPRP243.IMG;1` (0x3141f0) and SDK version strings
  for padman/mcman/mcserv, so the game's own modules are probably loaded from buffers (likely from DATA.BIN).

## Sound driver override (session 2, 2026-10-20)
- `runtime/tekken4_overrides.cpp`: first game override (`PS2_REGISTER_GAME_OVERRIDE`, matched on
  `SCES_508.78` + entry 0x180088 + CRC32 0xBEFFF97A). Staged into `ps2xRuntime/src/runner` by
  `tools\stage_generated.cmd`, which skips `*_overrides.cpp` when it mirrors generated/.
- Upstream has no API for a game to add an IOP HLE route, so the override replaces this ELF's
  `sceSifBindRpc` (0x282808), `sceSifCallRpc` (0x2829d8, `ps2_syscalls::`) and `sceSifCheckStatRpc` (0x282bc8,
  `ps2_stubs::`) via `runtime.registerFunction`. SID 0x12345 binds (client.server = the client's own address,
  never dereferenced), every call returns at once with a zeroed reply, status is never busy. Everything else
  is passed to the runtime. Calls go through `dispatchGuestBranch` -> function table, so the replacement is
  seen by all callers.
- Sound protocol (EE library 0x2230d0-0x224140, client 0x1cccbc0, all mode 0, args in a0-a3/t0-t3 + stack):
  send 0x1ccc700 (4 words), reply 0x1ccc780 (0x20). 0x8000 init (FUN_00223418), 0x100 per-frame (sends the
  0xf0-terminated command queue at 0x1ccc7c0, 0x80 bytes; reply words 1-7 go to globals, word 3 = flags),
  0x1100 and 0x3300 return reply[0] (no direct callers), 0x200/0x600/0x2100/0x2200/0x3000/0x3100/0x3200 commands.
- **Third run:** override applied, bind OK, calls 0x8000 and 0x2100 answered. Boot moves on and then spins in
  `FUN_001803b0` (PC 0x1803f0, called from `FUN_0022ae10`, which builds a GS packet in scratchpad).

## MFIFO stop (session 2, 2026-10-20)
- `FUN_001803b0(spr_addr, qwc)` kicks DMA channel 8 (fromSPR, 0x1000d000) into the MFIFO ring: the
  `.mfifo` section at 0x100000 (0x80040 bytes). Before each kick it waits for free ring space:
  `free = (drain.TADR - fromSPR.MADR) mod (1 << *(u8*)0x7000002f)`, with the drain channel's register block
  address at scratchpad 0x70000034 (+0x30 = TADR) and the cached free count at 0x70000030.
- Upstream runtime: `PS2Memory::tryProcessScratchpadDma` copies fromSPR linearly to MADR (no wrap at
  RBOR/RBSR) and nothing drains the ring as a GIF/VIF1 MFIFO source, so TADR never moves and the wait never ends.
  `Kernel/Stubs/DMA.cpp` only stores RBOR/RBSR. The GoW fork doesn't implement MFIFO either.
- This is hardware behaviour, not a Tekken quirk: the fix belongs in the runtime (keep it as a patch in this
  repo, offer it upstream), not in the game override.

## MFIFO + D_ENABLE patch (session 2, 2026-10-21)
- The game's setup, from Ghidra: `FUN_00232e48(idx)` picks the drain from a table at 0x2d1c20
  (8 bytes each: channel regs, two thresholds, MFD, ?): entries 0 and 1 = **VIF1 (0x10009000), MFD 2**,
  entry 2 = GIF (0x1000a000), MFD 0, so GIF gets ordinary DMA. Ring: RBOR 0x100000, RBSR 0x7fff0 (512 KB),
  fromSPR MADR and VIF1 TADR both start at 0x100000. `FUN_00180270(ch, tadr)` starts VIF1 chain (CHCR 0x105,
  TTE kept from `FUN_00233690`). VIF1's end interrupt (`FUN_00233430`, AddDmacHandler 1) steps a per-frame
  state machine (restart drain / hand over to GIF). `FUN_00232c30`/`FUN_00232c88` suspend/resume all DMA via
  D_ENABLEW and poll D_ENABLER. `FUN_002332a0` switches MFIFO off (MFD = 0) while VIF1 keeps STR.
- `patches/ps2xRuntime-dmac-mfifo.patch` (ps2_memory.cpp/.h), applied by `tools\build_runtime.cmd`:
  - fromSPR with MFD 2/3 writes into the ring, MADR wraps (`RBOR + (addr & RBSR)`), then drains;
  - the drain channel (VIF1 for MFD 2, GIF for MFD 3) started in chain mode is no longer walked at once:
    `drainMfifo()` consumes tags only while the ring has data (a tag + its following data must be complete),
    TADR wraps, CNT/NEXT/END data comes from the ring, REF/REFS/REFE data from anywhere (wrapped if inside the
    ring); END/REFE/IRQ+TIE complete the channel (D_STAT + DMAC handler), an empty ring leaves STR set.
    Data goes to `processVIF1Data` (or GIF PATH3). CALL/RET in the ring are reported and end the chain.
  - D_ENABLEW (0x1000F590) writes are mirrored to D_ENABLER (0x1000F520).
  - CHCR reads: upstream's `readIORegister` clears STR on every channel CHCR read (transfers are synchronous
    there). That silently stopped the MFIFO drain whenever the game polled VIF1, so later packets were skipped
    when the game restarted VIF1 further along the ring. Reads now leave STR alone on an active MFIFO drain
    channel (it ends on END/REFE/IRQ).
  - Not modelled: D_STAT MEIS (bit 14, the runtime only keeps bits 0-9), DMA suspension itself
    (transfers are synchronous anyway).
  - Tests added to upstream's `ps2xTest/src/ps2_memory_tests.cpp` (ring wrap + drain as data arrives, END
    completes VIF1, D_ENABLER readback); they're part of the patch. Upstream suite: 448/448 pass (run
    `ps2x_tests.exe` from the PS2Recomp root; the VU0 macro test reads `instructions.h` relative to the cwd).
- **PCSX2 comparison** (user's PCSX2 on E: and portable 2.8.2): the first screen is the display-mode menu
  ("Caution: Selecting Progressive Mode..." / PAL50 / PAL60 / Progressive Scan) on a brown box built from the
  same **10 vertical sprite strips** our runtime draws. So the ring -> VIF1 -> GS packet path carries the right
  geometry; colour (grey vs brown), text and vertical placement (top half vs centred) are milestone 5 issues.
  PINE can't read hardware registers (they come back 0), so the register setup was taken from the game code.
- **Result:** boot reaches the main loop `FUN_001969f8` and stays there: about **47 frames/s** (PAL wants 50),
  1000+ frames in 21 s (frame counter = the per-frame sound call 0x100, logged by the override at 1/10/100/...).
  Sound command 0x200 shows up. The window shows only a grey 560x224 block made of 10 vertical strips
  (a framebuffer clear?) in the top half, unchanged from 6 s to 45 s.
- The runtime's debug panel covers the picture: F1 toggles it.

## Milestone 5 investigation (session 2/3, 2026-10-21)
- Tools: `TEKKEN4_GS_DUMP=<frame>` (env var, read by the override) records two frames of the GS debug history
  and every GIF packet raw, then writes `gs_dump_frame<N>.txt` (display regs, contexts, CD snapshot, events)
  and `gif_packets_frame<N>.txt` into the run directory. `tools/pine.py dump <addr> <size>` reads PCSX2 RAM.
  `PS2X_MFIFO_LOG=<file>` is a LOCAL debug print in drainMfifo (not in the patch file; remove before export).
- Display: PMODE 0x66 (circuit 2 only), SMODE2 3 (interlaced, field mode), DISPFB2 = PSMCT16 at block 0xd2,
  640 wide, DISPLAY2 640x512 (DH 511, MAGH 3). The **10 strips are the game's frame copy**: 64-px sprites
  copying the 32-bit render buffer (block 0, read as CT24) into the 16-bit display buffer, then 10 untextured
  sprites clear the render buffer. They are not the menu box.
- Per frame the VIF1 ring carries only: GS setup (REF 0x320df0), a small CNT packet (A+D: RGBAQ, TEST_2),
  the clear (REF 0x320f40), two more static packets, padding (REF 0x321040 = 16 x "XYZ3 = 0", sent 3x per
  frame on purpose), and the font CLUT upload (16-colour grey/alpha ramp to block 0x3fe1). No menu draws.
- **Root cause found:** the CNT packet in our ring is stale bytes. The game builds such packets with
  `FUN_0022a748(&p, idx)` -> `FUN_00180320`: **toSPR (channel 9) in source-chain mode with TTE** (CHCR 0x145,
  tags from the table at 0x2cea10), then `FUN_0022a720` patches the copied tag to CNT. The runtime only did
  normal-mode SPR DMA, so chain mode copied nothing and the game sent old scratchpad contents. PCSX2's ring
  (read over PINE) has the correct CNT qwc 3 packet.
- **Fixed:** `patches/ps2xRuntime-dma-chain.patch` (was spr-chain) (`processSprToChain`: all tag types, TTE copies the whole tag
  qword, SADR wraps in 16 KB; applied after the MFIFO patch). Our ring's first block now matches PCSX2's exactly
  (REF 9, CNT 3 "FLUSHE; DIRECT 3", REF 16, REF 7, REF 7, REF 17, END). Not upstreamed yet (no test yet; add one
  and send it with the next milestone).
- **Now:** the screen is black (the grey came from the stale packet) and the loop runs at ~59.5 fps.
  - Second ring block: ours = REF 0x312900 (8), REF 0x312980 (2), CNT 12 (font CLUT upload), padding.
    PCSX2's (menu on screen) = the same two REFs, then CNT 5 "DIRECTHL" (BITBLTBUF...), an IMAGE of 0x1d7d qwords
    from **0x373820** (REF, DIRECTHL: the menu texture, ~120 KB), CNT 5 (TEX0_2 etc.) and a **0x152-qword draw
    list**. So the game is still waiting for the menu's data: 0x373820 has to be filled first (disc read through
    an IOP module, or decompression). Next: find who writes 0x373820 (Ghidra xrefs / PCSX2 memory compare) and
    what our build is waiting on.
  - 59.5 fps: a PAL game should run at 50. The runtime paces VSync at 60 Hz; check how it picks the rate
    (SMODE1/SMODE2 PAL) before trusting frame timing.
- **Ground truth with input:** `tools/pcsx2_capture.ps1 -Keys "12:Down","14:Cross" -Shots 13,20` boots the
  portable PCSX2 (pad 1 on the keyboard, see the script) and picks **PAL60** on the display-mode menu (the user
  wants 60 Hz chosen on every run); the Namco logo follows ~6 s later. The user's PCSX2 1.6 on E: ignores our
  key presses (LilyPad), its config was restored unchanged.
- **Data never loads in our build:** EE RAM 0x373000-0x395000 (menu texture at 0x373820) and 0x39d000-0x500000
  are filled in PCSX2 at the menu but all zero in ours. The game binds no RPC service other than the sound
  driver (0x12345) and makes no other SIF calls, so the loader is probably that same Namco IOP driver: the
  silent override answers its calls (0x200, 0x2100, ...) with zeros and nothing ever arrives. Next: decode the
  0x200/0x3000-family calls (FUN_00223ca8 etc.) as load requests, or run the real driver (it is embedded in
  the ELF at 0x2e4900).
- **Disc loader decoded (2026-10-21):** `FUN_002155d0` (main loop, every frame) runs a queue of 48 load
  requests at 0x1ccb700. Request: [0] result (1 ok, -4 bad checksum), [1] state (1 start, 2 waiting, 4 done),
  [2] type, [3] file index, [4] EE destination, [5] size override, [7] bytes. File table at **0x314360**,
  16 bytes per file: +0 disc sector, +4 size, +8 checksum, +0xe decrypt key (short, <0 = none).
  Start: `FUN_002297c0(0)` clears the done flag, then **driver call 0x200 = read from disc**:
  `FUN_00223ca8(sector, dest, size, mode)` (mode 0/6/7 by type), busy flag 0x1ccb7cc = 1.
  Wait: `FUN_002297b0()` returns the done flag (gp-0x5148 = **0x35a128**). Done: first 4 KB decrypted
  (`word = ~word + key`), then the sum of all words XOR the table checksum must be 0.
  Our silent override swallows 0x200, so nothing ever loads.
  **Next:** (1) find who sets 0x35a128 (xrefs; likely `FUN_00225ae0(flags)`, called by the per-frame sound
  call with reply word 3; `FUN_00229030`/`040` (flags 0x100/0x200) set 0x1ccd35c/0x1ccd360 instead);
  (2) HLE 0x200 in the override: read `size` bytes at `sector * 2048` of the ISO (project root, local only)
  into EE `dest`, then report completion the way (1) shows; (3) check against PCSX2 that 0x373820 and
  0x39d000+ fill the same, then look at the screen (pick PAL60 when the menu shows).
- **Disc-read HLE works (override, call 0x200):** the done flag 0x35a128 is bumped by callback `0x229328`
  (`flag += 1`), reached only through a table at .data 0x2ce490 of driver event callbacks (0x229290 ring
  read pointer, 0x2292d0, 0x229318, 0x229328, 0x229340); we apply its effect directly after each read.
  **Sectors count from the start of DATA.BIN** (disc LBN 1236): with that base every checksum matches
  (decrypt first 4 KB `~w + key`, key = the table's short at +0xe, -1 = plain). game/disc has no DATA.BIN
  (only the ELF, SYSTEM.CNF, IOPRP were extracted), so the override reads the ISO in the project root at
  (1236 + sector) * 2048 (TEKKEN4_ISO overrides the path). The ISO is always preferred (it also has DATA2.BIN
  after DATA.BIN); a loose game/disc/TEKKEN/DATA.BIN is only a fallback when no ISO is found. No need to extract.
  Mode 6/7 requests (e.g. sector 9378, 1.2 MB -> 0x5010) target sound/IOP memory: logged, not copied.
  Result: the first file loads and passes its checksum, the sound bank request follows, then the main loop
  stops after frame 1 and a few garbled coloured pixels appear at the bottom left.
- **Current stop:** game thread in `FUN_002331f8` (PC 0x233210), the MFIFO flush: it spins until the ring
  state (struct from FUN_00232c20) has state[4] == 0 and state[0] == 0. Those are updated only by the
  VIF1/GIF DMA end handlers (0x233430 via AddDmacHandler 1, 0x233548 via AddDmacHandler 2) as they step
  the per-frame state machine (FUN_00233030/00233130/00233188). With real data the frame now has more
  stages (texture upload through GIF = table entry 2, MFD 0), and one completion never arrives.
  Next: dump state[0..0x14] when stuck, check that the GIF channel's completion queues DMAC cause 2 and that
  the handler runs, compare with PCSX2's ring (texture upload + 0x152-qword draw list).
- **Why the flush hangs (found):** ring state at **0x1cda2c0**: [0] pending paths, [1] active, [2] VIF1 end
  count, [3] GIF end count, [4] frames pending. Per frame: `FUN_00232d20` (loop start) zeroes it,
  `FUN_00233a38` (end of frame, via FUN_00233360) does [4] += 1 and sends a 2-qword closing packet. GIF end
  handler 0x233548: [3]++; 1 -> restart VIF1 path 0; 2 -> [4]-- then paths 2/1; else DisableDmac(2). VIF1
  end handler 0x233430: [2]++; 1 -> path 1; 2 -> paths 1/2; 3 and [4] != 0 -> [3]--, [2]-- (another round).
  Watchdog (`TEKKEN4_WATCH_MS=9000`) while stuck: [0]=0 [1]=1 [2]=2 [3]=2 **[4]=-38**: once [4] goes
  negative every VIF1+GIF end pair drives it lower, and the flush (`FUN_002331f8`) waits for 0 forever. So we
  deliver more VIF1/GIF end interrupts per frame than the hardware, or in another order. Next: log every DMAC
  cause the runtime queues (channel, why: MFIFO END, chain end, normal transfer) for one frame and work out
  the hardware order from the game's packets; suspects: GIF completions from upstream's
  processPendingTransfers (one per call), VIF1 END completions in drainMfifo, the new empty-chain completion.
- **Surplus interrupts traced (TEKKEN4_IRQ_LOG=<n> wraps the VIF1/GIF end handlers and frame start/end):**
  frames 0-3 are balanced (start, GIF end, VIF1 end, frame end +1, VIF1 end, GIF end -1, VIF1 end stop).
  In frame 4 (first frame with the loaded menu data) path 1 ends early: our ring has REF 0x312900 (8),
  REF 0x312980 (2), then **CNT qwc 0xa** where PCSX2 has **CNT qwc 5** (FLUSHE; DIRECTHL 5 = BITBLTBUF..
  TRXDIR), CNT 1 (IMAGE giftag), REF 0x373820 qwc 0x1d7d (the texture), CNT 5, CNT 0x152 draw lists...
  The 10 qwords swallow the next tags, VIF1 lands on garbage that decodes as REFE (addr 0x12340) and ends
  path 1 before the frame-end +1; the handlers then loop and drive the counter negative.
  So the EE code that builds the texture-upload CNT computes qwc 10 instead of 5 (EE-side bug or another
  missing runtime feature, like the toSPR one). Builders that emit DIRECTHL (lui 0x5100): FUN_001eec80,
  FUN_001feef8, FUN_0020cb68, FUN_00227f80, FUN_002344e0 (the last uses sceVif1Pk* and emits REF tags, so
  not it). Next: find which one builds this CNT (its qwc comes from the GIF part it opens), compare its
  inputs/outputs with PCSX2 (PINE: dump scratchpad/ring right after it), fix the cause.
- **Fixed: doubled DMA tag counts (override, 0x292900).** The game's VIF packet library close-tag function
  (0x292900, not recognised as SDK code, so it runs as game code) ADDS (cur - tag)/16 - 1 to the open tag.
  The runtime's stubs for the recognised calls (sceGifPkAddGsAD, sceGifPkCloseGifTag, ...) already keep that
  qwc current as they write (`refreshPacketBuilderPendingCount` in Stubs/GS.cpp), so mixed tags got qwc x2
  (image-upload CNT 10 instead of 5). The override replaces 0x292900 with the same function that SETS the
  qwc. Upstream issue to report: live qwc refresh in the pk stubs + a game's own (unstubbed) close = double.
  Found by logging pk state around open (0x292970), sceGifPkCloseGifTag and close (TEKKEN4_IRQ_LOG).
- **Result: the menu draws.** All 24 files load once, the main loop runs (~44 fps over 1000+ frames) and the
  display-mode menu appears: brown panel in its real colour, "Caution:" readable, but most glyphs missing,
  the panel in the top half (field not scaled to the full picture) and three garbled squares at the bottom
  left. Screenshot: docs/img/m5_first_menu_partial.png (our build's output).
- **Fixed: PATH2 as one GIF stream (patch 02).** The game cuts its GIF stream into fixed CNT 0x152 / DIRECT
  0x152 chunks regardless of packet boundaries (e.g. a glyph IMAGE upload of 15 qwords spans two chunks).
  Upstream's VIF1 interpreter only tracked a pending IMAGE count and resumed it at the VIF level, eating the
  next chunk's TTE VIFcodes (NOP; DIRECT) as image data: everything after was misaligned by 8 bytes and
  became garbage GIF tags (stray points, psm 0x15). Now each DIRECT appends to `m_path2Stream` and only
  complete GIF packets (any format) go to the GS; the tail waits for the next DIRECT (cleared on VIF1 reset).
- **Redone for upstream (session 3):** the stream buffer broke upstream tests that expect each DIRECT payload to
  reach the GIF callback as it is (one feeds a lone garbage qword). Now `forwardPath2Payload` forwards every
  payload unchanged and remembers the packet cut at its end (`m_path2Cut*`). `continuePath2Packet` sends the rest
  from the next DIRECT under new tags: IMAGE as a shorter IMAGE; PACKED/REGLIST with REGS rotated to the register
  the cut stopped at (full loops, then a leftover tag), REGLIST padding rebuilt. Raw-qword continuation stays for
  IMAGE only, and not when the next VIFcodes lead to a DIRECT. An intermediate "hold PACKED/REGLIST until
  complete" version also drew the menu but crashed that test. Same picture in the game (logs/cont1).
- **Result: the whole display-mode menu renders** (docs/img/m5_menu_text.png): panel, all text lines, arrow
  on PAL50. Remaining for milestone 5: the picture is squashed into the top half (field mode not scaled to
  the full PAL frame), three garbled squares at the bottom left, ~44 fps, then the Namco logo after PAL60.
- Patches renamed so they apply in order: 01-ps2xRuntime-dmac-mfifo.patch (= PR #372), 02-ps2xRuntime-dma-
  fixes.patch (= PR #373 plus its tests: toSPR chain, empty-chain completion, PATH2 continuation). Checked on a
  clean upstream worktree. `build_runtime.cmd` treats them as a stack: if the last one reverses, all are in.
- **Milestone 5 reached (2026-10-21).** `tools/run_capture.ps1 -Keys "10:Down","12:Cross","15:Left","16.5:Cross"`
  (posted key presses, runtime maps arrows/X/Enter to pad 1): Down -> PAL60, Cross -> "Display mode changed to
  [PAL60 / 60Hz]. Start game? Yes/No" (No preselected), Left + Cross = Yes -> **Namco logo** at ~20 s, then a
  white screen at ~34 s (next scene; check against PCSX2). Screenshots: docs/img/m5_menu_pal60.png,
  m5_confirm_yes.png, m5_namco_logo.png. Still off: only the top half of the picture is used and the rest
  shows other GS memory (field-mode display scaling), stray texture blocks, ~38-44 fps.
- Also in the dma-chain patch: a chain that ends without data (lone END/REFE/IRQ tag) now completes its
  channel (upstream left STR set and raised no interrupt). Correct per hardware, but didn't fix the hang.
- Side findings: the menu text isn't in the ELF; the IOP module package is embedded LZ-compressed and
  bit-inverted at 0x2e4900 (`FUN_00197dd8` = LZ decoder, `FUN_00214fa0` = IOP boot + module loading), so the
  real sound driver ships inside the ELF. `tools/build_runtime.cmd` exit codes now propagate (an error inside a
  bracketed block used to be reported as success) and `.cmd` files are kept CRLF.

## Display line repeat (session 3, 2026-10-21)
- Display registers on the menu: PMODE 0x66 (CRT2 only), SMODE2 3 (INT=1, FFMD=1), DISPFB2 FBP 0xd2 FBW 10 PSMCT16,
  DISPLAY2 DX 636 DY 50 MAGH 3 MAGV 0 DW 2559 DH 511. The game draws 640x256 per field.
- PCSX2 shows the menu box twice as tall as we did, in the middle of the screen: in interlaced FRAME mode
  (FFMD=1) each field reads every buffer line while DH counts both fields' lines, so the display shows
  (DH+1)/2 = 256 buffer lines, each twice. Upstream read DH+1 = 512 lines in every mode (picture in the top half,
  other VRAM below, which was the "three garbled squares"); it only special-cased FFMD=0 (bob of one parity).
- **Fixed (patch 03 = PR #375):** `gs_cpu_backend.cpp` reads ceil(height / repeat) lines, repeat = (MAGV+1),
  x2 in interlaced FRAME mode, and repeats them to DH+1 (`decodeDisplayLineRepeat`, `repeatPresentationLines`),
  for both circuits. New test in ps2_gs_tests.cpp. Menu, PAL60 confirmation and logo now sit where PCSX2 puts
  them (docs/img/disp_menu_full_height.png, disp_pal60_confirm.png, disp_namco_logo.png). The PCSX2 shots in
  logs/pcsx2_pal60e are in German (portable PCSX2's language), so its menu box is wider: the text is longer.
- After the logo (logs/after_logo1, PAL60 + Yes): black from ~24 s, then a near-white screen (mean 244/255) from
  ~36 s to at least 56 s.

## Intro movie (session 3, 2026-10-21)
- PCSX2 (logs/pcsx2_after_logo): logo 18-22 s, then the intro movie from ~24 s. Ours: black, then white: the
  player (FUN_00228520, states 2 init, 3 buffer, 4 play, 5/6 stop) gave up.
- **Stream.** Movies are files in the table at 0x314360 (movie table 0x31fdd0: file, first/last frame; the intro is
  file 1 = entry 0xb2, sector 58196, 129 MB). They interleave 0x27 sectors of MPEG-2 video (ES, 640x448) with 2
  sectors of SPU ADPCM audio. Driver call 0x3000 (sector, size, file, mode) opens the stream, 0x3200 (EE dest,
  sectors) reads the next video sectors into the player's ring (0xea000 bytes = 12 x 0x13800), 0x3100 closes.
  The driver answers a read with SIF command 4: handler 0x229350 stores the byte count at 0x35a12c (handlers set
  up in FUN_00229540: cid 1 = disc read done, 4 = stream read). PCSX2's ring (PINE, logs/pcsx2_movie_probe) is
  the video units back to back. The override now serves 0x3000/0x3200/0x3100 from the ISO.
- **Decoder input.** sceMpegGetPicture is the runtime's FFmpeg stub, fed by sceMpegAddBs or PSS demuxing. The game
  feeds the IPU itself: libmpeg calls its no-data callback (0x2281d0), which points a toIPU DMA (ch 4, REFE tag at
  0x1ccd2b0) at the next 0x9c00 ring bytes. The override's GetPicture runs that callback (a nested guest call)
  and passes each transfer to sceMpegAddBs until a picture is decoded.
- **End flag.** The game's own sceMpegIsEnd (0x28bc58) returns the first word of mp->sys, which the runtime's
  sceMpegCreate never cleared: the movie "ended" after one picture. Patch 04 clears it (generic libmpeg behaviour).
- **Display start.** The picture is shown by the vsync handler (FUN_002280d0) only after the player started the
  movie audio, which waits for status bit 0x200 in the driver's per-frame reply (0x100 call, word 3; 0x100 =
  stream ended). The override reports 0x200 while a movie is open and 0x100 when its file runs out.
- Result: the intro plays (docs/img/movie_intro_volcano.png, movie_intro_scene.png), no sound. Thin vertical lines
  every 64 px are gaps between the picture's sprite strips (the menu box has them too); PCSX2 has none.

## Sprite strip gaps (session 3, 2026-10-21)
- The display buffer (FBP 0xd2) is filled each frame by a copy of the 640x448 back buffer (FBP 0) into a 224-line
  field: ~280 bilinear sprites (REGLIST PRIM/UV/XYZ2/UV/XYZ2, 64-px columns of 16-row bands, a second 50%-blended
  pass one texel row lower). Each column runs from X 64k to 64k+63.5 (0x6ff8 - 0x6c00 = 63.5).
- DrawSprite truncated the vertex X to int, so 63.5 became 63 and column 63 of every strip was never written:
  the black line every 64 px on the menu box and the movie. GS rule: a sprite covers X0 <= x < X1, so 63.5
  covers column 63. **Patch 05** computes the covered pixels as ceil(X0) .. ceil(X1)-1 and maps the texture over
  the exact (fractional) width; integer-aligned sprites are unchanged. Tests 454/454; docs/img/movie_no_strip_gaps.png.
  Not upstreamed yet (PR + a test with a 63.5-wide sprite to do).

## Field offset (session 3, 2026-10-21)
- User report: the whole game vibrates. In interlaced FRAME mode the game draws one field per frame: its vsync
  handler (0x22aaf8) reads CSR.FIELD and copies the even or odd lines of the 448-line back buffer into the
  224-line display buffer (the copy's V starts at 0 or 1). A TV shows the odd field one line lower; patch 03
  line-doubled both at the same place, so the picture moved by a line whenever the field changed (~40 times a
  second, the game's frame rate).
- **Patch 06:** the GS frontend records CSR.FIELD whenever a draw targets a displayed buffer (FRAME.FBP ==
  DISPFB1/2.FBP) and passes it in the presentation request (`displayField`); in interlaced FRAME mode an odd
  field is shown one display line lower. Keyed to the field the picture was drawn for, not the current vsync,
  because below 60 fps a picture stays up for several fields. New test; 455/455.
- Measured with TEKKEN4_PRESENT_LOG=<s> (override debug aid: brightness-weighted vertical centre of the presented
  frame, min..max per second): menu 0.79 line of motion per frame without the offset, 0.21 with it (edges still
  flicker by a line between fields, as on a TV); PAL60 dialog 0.01-0.06; logo 0 (logs/field_off, field_on,
  field_final). A blend of the last two fields would remove the edge flicker too, if it is still visible.

## Difficulty screen (session 3, 2026-10-21)
- PCSX2 and ours, same input (PAL60, Yes, Start at 30 s to skip the movie): the first-run screen "MODE SELECT /
  SET GAME DIFFICULTY" (EASY .. ULTRA HARD, MEDIUM selected; PCSX2's copy is in German). logs/pcsx2_title,
  logs/title1..4.
- The menu titles and the TEKKEN 4 logo were upside down: DrawSprite sorted the corners but not their UVs, so a
  sprite sent bottom-right first was mirrored. **Patch 07** swaps U/V with X/Y (also PR #377, second commit, with
  a test). docs/img/difficulty_screen.png.
- Tried and dropped: making a zero-size sprite draw nothing (GS behaviour). 8 upstream tests draw sprites with
  both corners on one point and expect a pixel, and it did not remove the dashed lines.
- Thin dashed lines along the panels' edges (not in PCSX2): `tools/gif_sprites.py` (new; lists the sprites in a
  TEKKEN4_GS_DUMP GIF log with UV, texture, CLAMP, filtering) showed the UI drawn as 64-px strips from X -0.5 to
  63.5 over U 0..64 (Y the same way, given bottom-up). The GS samples u = u0 + (x - X0) * du/dx = x + 0.5 (texel
  x); DrawSprite added half a pixel more, so it read texel x+1 and, at each image's right/bottom edge, the GS
  memory beside the image. **Patch 08** takes UV at the pixel position and keeps FST UV fractions (they were
  >> 4). Integer-aligned point sampling is unchanged; PR #377 third commit + test, 448/448. The screen now
  matches PCSX2 (docs/img/difficulty_screen.png).

## Main menu = milestone 6 (session 3, 2026-10-21)
- Input: `run_capture.ps1 -Keys "10:Down","12:Cross","15:Left","16.5:Cross","30:Start","36:Cross"` (PCSX2:
  12/14/16/17/30/36). Both show MODE SELECT with STORY BATTLE selected, the character portrait and the help text
  (docs/img/m6_main_menu.png; PCSX2 logs/pcsx2_menu, German).
- Differences: VS BATTLE is greyed out in PCSX2 (only pad 1 plugged in) but enabled in ours, so the runtime
  probably reports pad 2 as connected. At 55 s PCSX2 has gone to the attract demo (VS screen); ours is still on
  the menu (its idle timer likely counts frames and we run at ~36 fps).

## GS speed (session 3, 2026-10-21)
- Measured with two new debug aids in the override: TEKKEN4_PERF_LOG=<s> (game frames and host time in GS
  packet processing per second) and TEKKEN4_PROFILE=<start s>:<s> (samples the game thread's instruction pointer
  every ms, names the hottest functions from the PDB via dbghelp; windows.h needs NOGDI/NOUSER next to raylib in
  the unity build). The game thread spent 860-930 ms of every second drawing in the software GS: ~40 fps on the
  first menus, 17 fps on the main menu.
- Fixes, in order (first menu / main menu fps): C-runtime rounding in bilinear filtering (lround/floor are calls
  without /arch:AVX, a fifth of the time) replaced by exact inline arithmetic 46/22; RelWithDebInfo with /Ob2
  (tools/build_runtime.cmd; CMake's /Ob1 kept the per-pixel helpers as calls) 52/24; forced inlining of the
  per-pixel helpers 55/26; bilinear skips texels with weight 0 (the screen copy samples on texel columns) 60/28;
  WritePixel's per-primitive part computed once (PixelPipeline) 60/29; then **drawing threads**: 60/60.
- **Drawing threads** (PS2X_GS_THREADS=<n>|auto, off by default in the runtime; tools/run_game.cmd and
  run_capture.ps1 set auto = logical CPUs - 2): every primitive is queued for all workers, each draws the rows of
  its own 16-row bands (no two threads share a byte in any format). Everything else that touches GS memory or
  the CLUT (uploads, CLUT loads, TEXFLUSH, readbacks, presentation, reset) first waits until the queue is drawn,
  and all texture page caches are invalidated there. A primitive that samples pages a queued one draws into, or
  draws into pages a queued one samples, also waits (the bands run ahead of each other). Without the cache
  invalidation and that check the main menu had black rectangles. New test: the same sequence (40 blended
  bilinear sprites, a sprite sampling the frame just drawn, a full-screen fill) gives identical GS memory with 0
  and 4 threads; 456/456.
- All of it is patch 09 (not upstreamed yet: it builds on PR #377's DrawSprite). The movie plays at 30 fps (the
  player shows a picture every second vsync), every other screen at 60.

## Character select (session 3, 2026-10-21)
- The stall after Arcade (and in the attract loop): game thread in FUN_00227eb0 (0x227eb8), spinning until
  0x1ccd18c reaches -1. The game decodes animated images with the IPU itself: FUN_002277d0 makes one job per image
  of an image file (resource 0x77, or 0x8a in mode 6) at 0x1ccd200 (0x54 bytes each), FUN_00227be8(step) advances
  each animation every frame and starts the decodes (FUN_002278f0: FDEC 8 bits = IPU_CTRL flags, IDEC to RGB32 via
  toIPU/fromIPU DMA), the channel-3 DMA handler 0x227cf8 starts the next image and counts 0x1ccd18c down. The
  runtime has no IPU, so the count never moved.
- PCSX2 at the select screen (PINE, logs/pcsx2_ipu_probe): one image, 15x11 macroblocks (240x176, the cloud
  background), 75 frames, output RGB32 at 0xf4a380. Image file: count, (offset, size) per image; image: u16 mb
  width, height, frames, speed, 4 x 0; then a frame table (offset, size). Each frame is the IPU flags byte (0x60:
  MPEG-2 intra VLC, non-linear quantiser), the macroblocks, 00 00 01 B0, 00 00 01 B1: FFmpeg's "ipu" format.
  Some frames have a stuffing byte before B0 that FFmpeg rejects (it wants exactly the end code after the
  picture).
- **Patch 10** (runtime, MPEG.cpp): `ps2_stubs::decodeIpuIntraPicture(data, size, mbw, mbh, out)` decodes one
  IPU intra picture with FFmpeg's ipu codec (retrying with up to 8 stuffing bytes dropped) and writes the IPU's
  RGB32 output (16x16 macroblocks in raster order, the IPU's fixed-point BT.601 conversion, alpha 0x80).
  **Override:** FUN_00227be8 replaced: same animation step, then the current frame of each image decoded at once
  and 0x1ccd18c set to -1. Formats 1/2 (RGB16, dithered) are not handled yet (logged).
- Result: CHOOSE YOUR FIGHTER as in PCSX2 (docs/img/character_select.png): animated clouds, Kazuya's portrait,
  the grid, the countdown. Ours shows PRESS P2 START where PCSX2 has CREDIT 0 (pad 2 reported as connected, the
  same as VS BATTLE being enabled). The countdown went 24 -> 19 in 7 s, so check the frame rate here.

## First fight (session 3, 2026-10-21)
- Keys `...,"40:Down","41.5:Cross","46:Cross"` (Arcade, Kazuya with the cursor's default). The select screen runs
  at 48-49 fps; after the pick the game loads Stage 1 and shows the fight: HUD (STAGE 1, timer 60, KAZUYA / LAW
  health bars), the 3D stage (palms, glass hall), Kazuya's intro animation (docs/img/first_fight.png,
  first_fight_stage.png). PCSX2 not compared yet.
- **~7 fps** in the fight (TEKKEN4_PERF_LOG, logs/fight1). Profile (TEKKEN4_PROFILE=62:10, logs/fightprof1): ~65 %
  of the game thread in the VU1 interpreter (commitReadyPipelines 22 %, normalizeOperand 8 %, run 8 %,
  calculatePairReadyCycle 6 %, execUpper 5 %, FMAC flags/results ~8 %), 18 % waiting (NtWaitForAlertByThreadId:
  the GS worker barriers or the vsync wait), little GS.
- The God of War port hit the same wall (docs/performance.md there: VU1 61 % of a frame) and reached 60 fps in
  gameplay with: native VU1 code generated ahead of time from captured micro-memories (tools/vu1/vu1n_gen.py,
  bit-exact replay check), VIF1 + VU1 on a worker thread pipelined with the GS frontend, and a VIF1 UNPACK fast path.
  Those live in their fork, not upstream. Tekken's ELF has 22 .DVP.overlay sections (its VU1 micro-programs).

## Native VU1 port (session 3, 2026-10-21)
Goal: the fight at full speed. Port of the God of War native port's lean VU1 backend (Waffle565/God-Of-War-Native-Port,
branch `runtime`, GPL-3.0 like PS2Recomp and this repo). Done so far = **patch 11** (`patches/11-native-vu1.patch`):
- Whole-file copies from GoW `origin/runtime` (our VU files were identical to their base c5a9d02 apart from line
  endings): `ps2_vu1.h`, `ps2_vu1_native.h`, `ps2_perf.h` (diagnostics, header only), `ps2_vu1_core.cpp`,
  `ps2_vu1_lower.cpp`, `ps2_vu1_upper.cpp`, `ps2_vu1_step.inl`, `ps2_vu1_native.inl`, and their `ps2_vu1_tests.cpp`.
  Their `ps2_vu_tests.cpp` changes were left out (they test GoW's EE FPU / VU0 helpers we don't have).
- `ps2_runtime.cpp`: `m_vu1.setDirectRegisterWrites(true)` like GoW (no register write queues, faster; needed by the
  capture, which lives in `compiledProgramFor`); `GOW_VU1_COLAS=1` goes back to the queued model.
- `ps2xRuntime/CMakeLists.txt`: `PS2X_VU1_MICROCODE_DIR` (folder of captured `.bin` micro memories, empty = interpreter
  only) and `PS2X_VU1N_GEN` (default `<project>/tools/vu1/vu1n_gen.py`); generates `vu1n_part0..5.cpp` in the build
  folder (`vu1n_heads.txt` next to the micro-memory folder = extra block heads) and links them into ps2EntryRunner.
- With no blocks linked the engine is the interpreter (each pair first misses the native table). Tests 465/465.
Next, in order:
1. **The generator's licence (deferred by the user).** `tools/vu1/vu1n_gen.py` (copied unchanged from GoW's `main`
   branch, which has no LICENSE file; only `runtime` is GPL-3.0) is committed at the user's request; the user made the
   GitHub repo private (2026-10-21). Settle the licence (ask Waffle565) before making the repo public again.
2. **Capture:** run to the fight with `GOW_VU1_NATIVE_OFF=1` and `GOW_VU1_CAPTURA=<existing folder>` (use
   `game/vu1/micro`, gitignored: the micro memories are game code). Writes `vu1_<fnv64>.bin` per distinct micro memory.
3. Configure `-DPS2X_VU1_MICROCODE_DIR=<that folder>` (add it to `tools/build_runtime.cmd` when the folder exists),
   build, run with `GOW_VU1N_DIAG=1` to list `[vu1n-miss]` pcs, put them in `game/vu1/vu1n_heads.txt`, rebuild.
4. Verify: `GOW_VU1N_VERIFY=<runs>` (tolerance compare against the interpreter); bit-exact: record with a build
   without blocks (`GOW_VU1N_RECORD=<file>`, `GOW_XGKICK_IMMEDIATE=1`), replay with the native build
   (`GOW_VU1N_REPLAY=<file>`), expect "0 differ". Risks for Tekken: Q/P have no latency in native code, status flags
   not modelled, missing heads. `GOW_VU1_STATS=1` prints VU1 time and interpreted steps every 5 s.
5. Measure the fight (`TEKKEN4_PERF_LOG=200` = seconds to log, `TEKKEN4_PROFILE`), compare with PCSX2, then milestone 7.

Progress (2026-10-21, commits a31b778, fda9e02):
- Capture done: **one** micro memory (`game/vu1/micro/vu1_62802e7b8abf4848.bin`, 0x0-0x3e88 = the 18-overlay group):
  Tekken uploads its whole VU1 set once. 220 blocks, 0 unsupported. `toolsuild_runtime.cmd` now links them.
- Fixed with `GOW_VU1N_VERIFY` + `GOW_VU1N_BISECT=1` + `GOW_VU1N_DUMP=1` (new: bisect any mismatch):
  (1) vf0 corrupted: Tekken's backface test is `OPMULA.xyz ACC,vf21,vf22` + `OPMSUB.xyz vf0,vf22,vf21` (flags only);
  native callouts skipped the interpreter's per-pair vf0/vi0 restore. (2) OPMULA/OPMSUB generated natively so FMAND
  sees their MAC flags (it read an older MUL's).
- Left in verify: "mac"-only end-state differences, and single ADC words where the interpreter recomputes a nearly
  cancelling MSUB from the unrounded product while native subtracts the rounded one (edge-on triangles; harmless).
- Fight with native VU1: 11-25 fps (interpreter: 7-9), stage and both fighters drawn (logs/vu1n_fight4).
- **The stall is not native VU1.** About 4 s into the fight (clock 00:03:68, timer 56) the program at 0x32a0 runs to its
  65536-cycle budget every time (`GOW_VU1_LONGRUNS=40000` prints `[vu1-long] startPc=32a0 ... budget`), then garbage
  XGKICKs follow (the runaway loop writes over VU memory: "[VU1 reserved lower] ... 0xfffffffb/8" = rejected GIF tags).
  `GOW_VU1N_CHECKLONG=1` re-runs each such run with the exact engine from the same entry state: it loops too. And the
  interpreter-only build (`GOW_VU1_NATIVE_OFF=1`, logs/vu1_interp_long) stalls the same way at the same fight time
  (134 s there, it is slower). The loop counter is `vi8 = vi5 & vi12` (pc 0x3390; vi5 = count from the packet header,
  vi12 = mask, normally 0x7fff), decremented per pass until 0: in the stalled runs vi12 is 0 at entry (VI registers
  carry over between VU1 programs), so the count is 0 and the loop wraps 65535 times.
- **Cause traced (2026-10-21, late), not fixed yet.** The garbage header comes from the VIF1 stream, not VU1:
  1. VIF1 runs in MFIFO mode (ring D_RBOR 0x100000, D_RBSR 0x7FFF0, filled by fromSPR copies of SPR packets). A
     frame's list ends with REF qwc=0 at 0x13d330 + REFE at 0x13d340; the fromSPR copy that carried them (qwc 0x18 from
     SPR 0x0050, ring 0x13d310-0x13d490) also put 20 more qwords after the REFE.
  2. The game (FUN_00180270, per Grok) restarts VIF1 with TADR = 0x13d350 (qword after the REFE). The "tag" there is
     NEXT qwc=0x3000 (192 KB, more than ever arrives), so our drain waits there forever while new packets arrive behind
     it (ring write pos 0x13d490 -> 0x142f70).
  3. The game's recovery path (FUN_002332a0): D_ENABLEW suspend, VIF1 CHCR STR off, resume, D_CTRL.MFD = 0, VIF1 CHCR
     start: a normal chain at 0x13d350 reads NEXT 0x3000 = 192 KB of ring memory as VIF data; its DMA tags parse as VIF
     codes and one of them is STMOD 2 (difference mode), which corrupts the next UNPACKs (the 0x32a0 header).
  On a real PS2 that path cannot be taken this way, so the open question is what sits at 0x13d350 on hardware when
  VIF1 restarts (is our SPR packet tail different, or does hardware treat that NEXT differently: PCSX2's
  mfifoVIF1transfer streams a tag's data as it arrives instead of waiting for the whole packet like our drainMfifo).
  Next: read ring 0x13d300-0x13d4a0 in PCSX2 (tools/pine.py) at the same point of the fight and compare; then fix
  drainMfifo (stream partial CNT/NEXT data, see PCSX2 Vif1_MFIFO.cpp) or the SPR packet source.
- **Patch 12** (`patches/12-ps2xRuntime-vif1-carry-and-mfifo-wait.patch`, tests 466/466): (a) a VIF1 command whose
  payload runs past the end of a DMA transfer (UNPACK / MPG / STROW / STCOL / STMASK) is kept and finished with the next
  transfer, like PCSX2 (before: the rest was parsed as VIF codes; Tekken does this when a drain ends mid-command); (b) a
  CHCR write to the MFIFO channel while it waits is ignored unless it clears STR or the DMAC is suspended (PCSX2
  DmaExec). Neither fixes the stall by itself.
- Debug instrumentation used to find it: `patches/debug/vif1-dma-trace.patch` (not applied by build_runtime; apply on
  top of 01-12 with `git -C external/PS2Recomp apply ../../patches/debug/vif1-dma-trace.patch`): with
  GOW_VU1N_CHECKLONG=1 it prints, at the first invalid VIF1 code ("[vif1-bad]"), the last VIF1 codes, the DMA tags
  read (chain walker c / MFIFO drain m), drain ends with the tag at TADR, fromSPR ring copies, VIF1 CHCR / TADR /
  D_CTRL / D_ENABLEW writes, plus VU1 run history and the input header at TOP for runs that hit the budget.
- New debug switches in patch 11: `GOW_VU1_LONGRUNS=<cycles>` (log runs longer than that), `GOW_VU1N_CHECKLONG=1`.

## Grok in the pipeline (from 2026-10-21)
The user added Grok (xAI Grok Build CLI 1.0.62, `C:\Users\pablo\.grok\bin\grok.exe`, signed in with the user's
grok.com account, default model grok-4.7) as a parallel worker, second opinion and reviewer; it may see everything that
is not personal (never the disc files, ISO or BIOS). Heavy analysis / coding jobs go to it too (`-Effort high`).
- `tools\grok_task.ps1 -Name <n> -Mode ask|work [-Owns <files>] [-Effort high] (-Prompt <text> | -PromptFile <file>)`,
  run in the background; result in `logs\grok\<n>_<time>.md` (answer + files changed in both repos), prompt in
  `.prompt.md`. The prompt goes through `--prompt-file` (PowerShell 5.1 breaks double quotes in native arguments).
  Project rules are passed with `--rules` (Grok does not load the project's CLAUDE.md: the project is not "trusted").
- A headless run cannot approve anything, and a refused tool ends it without a final answer, so each mode allows
  exactly what its tasks need (`--allow` rules; `--always-approve` is refused by Claude Code's auto mode):
  - ask (analysis, reviews, research): reads anything, writes only `logs/grok/**`, runs `python -I` and read-only git.
  - work (a change, in the main project): edits only the `-Owns` files and `logs/grok/**`, runs
    `cmd /c tools\build_runtime.cmd` and `powershell -File tools\run_capture.ps1` with `TEKKEN4_BUILD_DIR=grok`
    (build in `external\PS2Recomp\out\grok`, never Claude's `out\runtime`), plus python and read-only git; never
    commits. Claude keeps off the owned files meanwhile, reviews the diff and commits.
- The build/run scripts take `TEKKEN4_BUILD_DIR` (default `runtime`). Both builds compile the same PS2Recomp source
  tree, so an owned file Grok is mid-way through editing also lands in Claude's next build.
- Findings so far: the pad-2 bug is `scePadGetState` (external/PS2Recomp/.../Kernel/Stubs/Pad.cpp:453) returning
  "stable" for any opened port without checking that a pad is connected, so port 2 looks plugged in (PRESS P2 START,
  VS BATTLE). **Fixed by Grok = patch 13** (`patches/13-ps2xRuntime-pad-empty-port-disconnected.patch`, Pad.cpp only):
  port 0 = keyboard + first host gamepad as before; port 1+ connected only with another host gamepad; an empty open
  port reads scePadStateDiscon, scePadRead returns 0 (data 0xFF), info/req calls 0. Verified (logs/grok/pad2-fix-notes.md):
  VS BATTLE greyed and skipped by the cursor, character select CREDIT 0 like PCSX2; tests 466/466. Not run with a
  second physical gamepad.
- Grok "vu1-vi12" (cut off by a refused write before the modes above, partial): vi12 is used as the count mask once
  (0x3390) and then overwritten inside the same program, so the mask at the next start of 0x32a0 is whatever an
  earlier run left. Rerun it with the new ask mode.

## Publishing (from 2026-10-21)
The user pre-authorized publishing every successful milestone on three channels:
- **Project repo (private since 2026-10-21, the user's choice; was public):** https://github.com/Lautreck/tekken4-recomp (`origin`, branch `master`). Commits use
  `Pablo <8440795+Lautreck@users.noreply.github.com>` (set in this repo's git config; history was rewritten to it
  before the first push). Never push game files, Ghidra DBs, generated C++, BIOS or disc data.
- **Upstream PRs:** fork https://github.com/Lautreck/PS2Recomp (remote `fork` in `external/PS2Recomp`).
  PR #372 https://github.com/ran-j/PS2Recomp/pull/372 = `patches/ps2xRuntime-dmac-mfifo.patch` (branch
  `dmac-mfifo`, commit 38d6396). `external/PS2Recomp` now sits on that branch (eb8688b + the patch), so
  `build_runtime.cmd` finds the patch already applied. If the PR is merged, move back to upstream `main` and
  drop the patch file. Upstream PR style: plain title, problem / per-file changes / found while / verified with,
  "Made by Pablo and assisted with Claude Code."
- PR #373 https://github.com/ran-j/PS2Recomp/pull/373 = patch 02 without MFIFO (branch `dma-vif1-fixes`, commit
  8e97bf7, built and tested in the worktree `external/ps2recomp-pr2`, 450/450; its `build_pr_tests.cmd` is local
  only, never commit it). Issue #374 https://github.com/ran-j/PS2Recomp/issues/374 = the pk stubs' live QWC
  doubling the count when a game's own close runs (our `closeDmaTag` override works around it).
- PR #375 https://github.com/ran-j/PS2Recomp/pull/375 = patch 03, display line repeat (branch `display-line-repeat`,
  commit d030383, worktree `external/ps2recomp-pr3`, 446/446).
- PR #376 https://github.com/ran-j/PS2Recomp/pull/376 = patch 04 + a test, sceMpegCreate clears mp->sys word 0
  (branch `mpeg-create-clears-end`, commit ccaa06f, worktree `external/ps2recomp-pr4`, 446/446).
- PR #375 also carries patch 06 (second commit d9141f2, field offset; title and description updated, 447/447).
- PR #377 https://github.com/ran-j/PS2Recomp/pull/377 = patches 05 + 07 + 08 with tests (three commits) (two strips 0..63.5 and 64..127.5),
  branch `sprite-edge-coverage`, worktree `external/ps2recomp-pr5`, 446/446. The test is only in the PR branch.
- **Milestone pages** (private until the user shares them from the page's Share menu): milestone 4 at
  https://claude.ai/artifact/BTDUfBQqwaDsn83wdRh2gf (our runtime screenshot next to PCSX2's), milestone 5 at
  https://claude.ai/artifact/CoN3nkpepFjyn5LSbPEQ7j (menu, PAL60 confirmation, Namco logo; four fixes). Milestone 6 at
  https://claude.ai/artifact/CGjVKCtqc6WGA2aat476gt (boot to main menu, ours next to PCSX2; PRs #375-#377).
  A session-3 progress page for the user: https://claude.ai/artifact/Y2iCxSTrfwM5mY5rWZxWK2.

## Next steps
1. Upstream patch 09 (GS speed + drawing threads) once PR #377 is in, as a follow-up PR.
2. Milestone 7, a fight: it loads and renders (see "First fight") at ~7 fps, VU1-bound. Options, cheapest
   first (now doing (c), see "Native VU1 port"): (a) speed up the VU1 interpreter's hot paths (pipeline bookkeeping per pair); (b) VIF1 + VU1 on a worker
   thread (the GoW port's PS2X_GS_WORKER); (c) native VU1: translate Tekken's 22 micro-program overlays ahead of
   time (the GoW port's vu1n_gen.py approach, with a bit-exact replay check). Then compare the fight with PCSX2,
   test fight input (pad 1 moves), and look at pad 2 being reported as connected (PRESS P2 START, VS BATTLE).
3. Sound for real (milestone 8): find how the game loads its IOP modules (sceSifRebootIop with
   IOPRP243.IMG, then module buffers probably from DATA.BIN), extract the sound IRX and run it on ps2xIOP's
   emulated IOP. Then the override's sound part goes away.
4. Watch PRs #372/#373/#375/#376/#377 and issue #374: when merged, move `external/PS2Recomp` back to upstream main and drop the
   patch files.
5. Re-export from Ghidra (3 late SDK renames) whenever Ghidra is next used for an export; rerun post-process.
6. Later: export Ghidra's switch tables into `[jump_tables]` to cut the unresolved JR sites.
7. Watch: upstream keeps its RPC server pool at guest 0x01F10000, above the game's stack top (sp starts
   ~0x1f00000). If a heap ever lives there, see the GoW `GuestReservedLayout` lesson.
