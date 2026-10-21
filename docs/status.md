# Status

_Last updated: 2026-10-21 (end of session 2)_

## Resume here
To continue, open Claude Code in this folder and say: "read docs/status.md and continue".
- Done in session 1: toolchain installed, PS2Recomp tools built, analyzer seed + full recompile with 0 errors,
  Ghidra project (`ghidra/Tekken4`, non-shared) analysed with SDK names applied, Ghidra export recompiled.
- Done in session 2: Ghidra MCP tools approved and working in Claude Code. **Runtime base decided: upstream
  ps2xRuntime** (cherry-pick from the GoW fork at GS/VU1 time). ps2EntryRunner builds and runs; first stop found
  (see "First run"); 350 missing functions added (4 constructors + 346 vtable/callback targets) (see "Missing
  functions fix"); silent sound-driver override (see "Sound driver override"); MFIFO + D_ENABLE runtime
  patch (see "MFIFO + D_ENABLE patch"). Milestone 4 published (see "Publishing").
- **Resume at: milestone 5, texture-upload CNT built with qwc 10 instead of 5** (see "Surplus interrupts traced").
- **Session 3 goal: milestone 5.** The first screen (display-mode menu) draws its 10-strip box, but grey, without
  text, in the top half only. Start from "Next steps" #1.
- **Main config is now `config/ghidra_export.toml`**, reading `config/function_map.csv` (Ghidra functions + `extra_functions.csv`). The analyzer seed is kept for reference.
- Build loop: `ps2_recomp config/ghidra_export.toml` -> `tools\stage_generated.cmd` -> `tools\build_runtime.cmd`
  (~3.5 min clean; applies `patches\*.patch` first) -> `tools\run_game.cmd` (close the window to stop; output in `logs\run.log`).
- Look at it: `powershell -File tools\run_capture.ps1 -Shots 8,25` (our build: hides the debug panel, screenshots +
  stdout in `logs\run_<time>\`), `powershell -File tools\pcsx2_capture.ps1 -Shots 15` (same in the user's PCSX2,
  the ground truth). Runtime tests: `tools\build_tests.cmd` (PS2Recomp's ps2x_tests, 448/448).
- Not in git (rebuild/re-extract if lost): `external/` (clones + Ghidra + PCSX2 + MCP venv, see Pinned
  dependencies), `game/disc/` (7-Zip from the ISO), `generated/` (rerun `ps2_recomp config/analyzer_seed.toml`).

## Milestones
- [x] 0. Project scaffold, disc files extracted, ELF inspected
- [x] 1. Toolchain installed: VS 2022 Build Tools 17.14 (MSVC), CMake 4.4.4, Ninja 1.13.2, Temurin JDK 21.0.12
      (JAVA_HOME set), Ghidra 12.1.4 + EE plugin v2.1.38 + GhidraMCP 2.0, PCSX2 2.8.2 portable
- [x] 2. PS2Recomp host tools build from source (`tools\build_ps2recomp_tools.cmd`)
- [x] 3. Ghidra analysis + function map exported, recompiled output compiles and links into ps2EntryRunner
- [x] 4. Boots past startup without crashing (main loop runs at ~47 fps, 2026-10-21)
- [ ] 5. Something on screen (Namco logo / first menu) - GS work starts here
      (so far: a grey cleared block only)
- [ ] 6. Menus and controller input
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
  (1236 + sector) * 2048 (or a loose game/disc/TEKKEN/DATA.BIN if present; TEKKEN4_ISO overrides the path).
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
- Also in the dma-chain patch: a chain that ends without data (lone END/REFE/IRQ tag) now completes its
  channel (upstream left STR set and raised no interrupt). Correct per hardware, but didn't fix the hang.
- Side findings: the menu text isn't in the ELF; the IOP module package is embedded LZ-compressed and
  bit-inverted at 0x2e4900 (`FUN_00197dd8` = LZ decoder, `FUN_00214fa0` = IOP boot + module loading), so the
  real sound driver ships inside the ELF. `tools/build_runtime.cmd` exit codes now propagate (an error inside a
  bracketed block used to be reported as success) and `.cmd` files are kept CRLF.

## Publishing (from 2026-10-21)
The user pre-authorized publishing every successful milestone on three channels:
- **Project repo (public):** https://github.com/Lautreck/tekken4-recomp (`origin`, branch `master`). Commits use
  `Pablo <8440795+Lautreck@users.noreply.github.com>` (set in this repo's git config; history was rewritten to it
  before the first push). Never push game files, Ghidra DBs, generated C++, BIOS or disc data.
- **Upstream PRs:** fork https://github.com/Lautreck/PS2Recomp (remote `fork` in `external/PS2Recomp`).
  PR #372 https://github.com/ran-j/PS2Recomp/pull/372 = `patches/ps2xRuntime-dmac-mfifo.patch` (branch
  `dmac-mfifo`, commit 38d6396). `external/PS2Recomp` now sits on that branch (eb8688b + the patch), so
  `build_runtime.cmd` finds the patch already applied. If the PR is merged, move back to upstream `main` and
  drop the patch file. Upstream PR style: plain title, problem / per-file changes / found while / verified with,
  "Made by Pablo and assisted with Claude Code."
- **Milestone page:** milestone 4 at https://claude.ai/artifact/BTDUfBQqwaDsn83wdRh2gf (private until the user
  shares it from the page's Share menu). Source: our runtime screenshot next to PCSX2's.

## Next steps
1. Milestone 5, the display-mode menu: our runtime draws its 10-strip box but grey, without text, squashed
   into the top half. Check (a) colour/texture: is the box textured (TEX0/TME) and is the texture upload
   (GIF IMAGE via PATH3 = table entry 2, or VIF1 DIRECT) reaching GS memory; (b) text: font texture upload and
   the sprites that draw it; (c) placement: the game renders fields (224 lines) and the display should
   scale it to 448/512 (DISPLAY1/2 DH, MAGV, SMODE2 FFMD). Ground truth: `E:\God Hand + Emulator\pcsx2.exe
   --nogui "<iso>"` (English) shows the full screen; the runtime's debug panel (F1) has GS and DMA/IO tabs.
   Main loop mode = `FUN_001979a0()` (switch at the end of FUN_001969f8).
2. Frame pacing: 47 vs 50 fps. The loop busy-waits on `RCNT0_COUNT >= 0x724` and on vsync.
3. Sound for real (milestone 8): find how the game loads its IOP modules (sceSifRebootIop with
   IOPRP243.IMG, then module buffers probably from DATA.BIN), extract the sound IRX and run it on ps2xIOP's
   emulated IOP. Then the override's sound part goes away.
4. Upstream PR for the MFIFO/D_ENABLE patch: see "Publishing" below.
5. Re-export from Ghidra (3 late SDK renames) whenever Ghidra is next used for an export; rerun post-process.
6. Later: export Ghidra's switch tables into `[jump_tables]` to cut the unresolved JR sites.
7. Watch: upstream keeps its RPC server pool at guest 0x01F10000, above the game's stack top (sp starts
   ~0x1f00000). If a heap ever lives there, see the GoW `GuestReservedLayout` lesson.
