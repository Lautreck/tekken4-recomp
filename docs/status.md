# Status

_Last updated: 2026-10-20 (end of session 2)_

## Resume here
To continue, open Claude Code in this folder and say: "read docs/status.md and continue".
- Done in session 1: toolchain installed, PS2Recomp tools built, analyzer seed + full recompile with 0 errors,
  Ghidra project (`ghidra/Tekken4`, non-shared) analysed with SDK names applied, Ghidra export recompiled.
- Done in session 2: Ghidra MCP tools approved and working in Claude Code. **Runtime base decided: upstream
  ps2xRuntime** (cherry-pick from the GoW fork at GS/VU1 time). ps2EntryRunner builds and runs; first stop found
  (see "First run").
- **Main config is now `config/ghidra_export.toml`** (Ghidra functions only). The analyzer seed is kept for reference.
- Build loop: `ps2_recomp config/ghidra_export.toml` -> `tools\stage_generated.cmd` -> `tools\build_runtime.cmd`
  (~3.5 min clean) -> `tools\run_game.cmd` (close the window to stop; output in `logs\run.log`).
- Not in git (rebuild/re-extract if lost): `external/` (clones + Ghidra + PCSX2 + MCP venv, see Pinned
  dependencies), `game/disc/` (7-Zip from the ISO), `generated/` (rerun `ps2_recomp config/analyzer_seed.toml`).

## Milestones
- [x] 0. Project scaffold, disc files extracted, ELF inspected
- [x] 1. Toolchain installed: VS 2022 Build Tools 17.14 (MSVC), CMake 4.4.4, Ninja 1.13.2, Temurin JDK 21.0.12
      (JAVA_HOME set), Ghidra 12.1.4 + EE plugin v2.1.38 + GhidraMCP 2.0, PCSX2 2.8.2 portable
- [x] 2. PS2Recomp host tools build from source (`tools\build_ps2recomp_tools.cmd`)
- [x] 3. Ghidra analysis + function map exported, recompiled output compiles and links into ps2EntryRunner
- [ ] 4. Boots past startup without crashing
- [ ] 5. Something on screen (Namco logo / first menu) - GS work starts here
- [ ] 6. Menus and controller input
- [ ] 7. A fight: two characters on a stage, correct rendering, steady frame rate
- [ ] 8. Sound, saves, PC features

## Pinned dependencies
| Repo | Commit | Date |
|---|---|---|
| ran-j/PS2Recomp | eb8688b | 2026-10-14 |
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

## Next steps
1. Milestone 4, first fix: give every `.ctors`/`.dtors` entry a function (read the section bounds from the ELF,
   create missing functions in Ghidra, re-export + post-process + recompile). Then scan .data for other
   pointers into .text that don't start a function (vtables, callback tables): same failure class.
2. Rerun and take the next stop. Re-export also picks up the 3 late SDK renames.
3. Later: export Ghidra's switch tables into `[jump_tables]` to cut the 155 unresolved JR sites.
