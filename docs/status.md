# Status

_Last updated: 2026-10-20 (end of session 1)_

## Resume here
To continue, open Claude Code in this folder and say: "read docs/status.md and continue".
- Done in session 1: toolchain installed, PS2Recomp tools built, analyzer seed + full recompile with 0 errors.
- **Waiting on the user:** Ghidra import is not done yet (no `ghidra/` folder). Steps: run
  `external\ghidra_12.1.4_PUBLIC\ghidraRun.bat` -> new project in `ghidra\` -> import `game\disc\SCES_508.78`
  -> accept auto-analysis -> CodeBrowser: File > Configure > Developer > tick GhidraMCPPlugin ->
  restart Claude Code here and approve the `ghidra` MCP server (`.mcp.json`).
- **Open decision:** runtime base (see Next steps #2). Recommendation given: upstream first.
- Not in git (rebuild/re-extract if lost): `external/` (clones + Ghidra + PCSX2 + MCP venv, see Pinned
  dependencies), `game/disc/` (7-Zip from the ISO), `generated/` (rerun `ps2_recomp config/analyzer_seed.toml`).

## Milestones
- [x] 0. Project scaffold, disc files extracted, ELF inspected
- [x] 1. Toolchain installed: VS 2022 Build Tools 17.14 (MSVC), CMake 4.4.4, Ninja 1.13.2, Temurin JDK 21.0.12
      (JAVA_HOME set), Ghidra 12.1.4 + EE plugin v2.1.38 + GhidraMCP 2.0, PCSX2 2.8.2 portable
- [x] 2. PS2Recomp host tools build from source (`tools\build_ps2recomp_tools.cmd`)
- [ ] 3. Ghidra analysis + function map exported, recompiled output compiles
      (analyzer-seeded recompile done, see below; Ghidra map and runtime build still to do)
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

## Next steps
1. Ghidra: create project in `ghidra/`, import the ELF with the EE plugin, auto-analyse, enable
   GhidraMCPPlugin, run `ExportPS2Functions.java` into `config/`. Compare its function count and
   unresolved-JR count with the analyzer seed.
2. Decide runtime base: upstream ps2xRuntime (cleaner, has newer FPU fixes) vs Waffle565's fork
   (`origin/runtime`, much further on GS/VU1/IOP but GoW-tuned, based on older c5a9d02).
   Leaning: upstream first, cherry-pick from the fork when we reach GS/VU1.
3. Build ps2EntryRunner with the generated code, logs OFF (`PS2X_ENABLE_AGRESSIVE_LOGS`,
   `PS2X_ENABLE_RUNTIME_LOGS`, `PS2X_ENABLE_IOP_RPC_TRACE`), RelWithDebInfo.
