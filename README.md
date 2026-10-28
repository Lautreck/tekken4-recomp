# Tekken 4 Recomp

A static recompilation of **Tekken 4** (PlayStation 2) into a native Windows program, built on
[PS2Recomp](https://github.com/ran-j/PS2Recomp). The game's MIPS code is translated to C++ ahead of time and
linked against PS2Recomp's runtime, which stands in for the PS2 hardware.

Target: **PAL (Europe/Australia), v2.00, SCES-50878**. Addresses in this repo only match that build.

> This repo holds **no game data**: no ELF, no disc files, no BIOS, no generated C++ from the game.
> You need your own copy of the game and a BIOS dumped from your own console.

## Status

| Milestone | State |
|---|---|
| 0-2. Scaffold, toolchain, PS2Recomp tools | done |
| 3. Function map from Ghidra, recompiles (4809 functions, 0 errors) and links | done |
| 4. Boots past startup without crashing | **done**: main loop runs at ~47 fps |
| 5. Something on screen | in progress: the first screen's geometry draws, textures and text don't yet |
| 6-8. Menus and input, a fight, sound/saves | not started |

What it took to reach the main loop:
- **Functions Ghidra never found** (C++ static constructors and vtable methods, only reachable through
  pointer tables): `tools/find_missing_functions.py` adds 350 of them.
- **The sound driver** (Namco's IOP module, SIF RPC 0x12345) is answered by a silent stand-in in
  `runtime/tekken4_overrides.cpp` until the real module runs on PS2Recomp's emulated IOP.
- **MFIFO**: the game streams its GS packets through a scratchpad -> ring buffer -> VIF1 DMA ring.
  `patches/ps2xRuntime-dmac-mfifo.patch` adds that to the runtime (plus D_ENABLER readback).

The day-to-day log, findings and next steps are in [docs/status.md](docs/status.md).

## Layout

| Path | What |
|---|---|
| `config/` | PS2Recomp TOML and function maps (`function_map.csv` = Ghidra functions + `extra_functions.csv`) |
| `patches/` | Changes to PS2Recomp itself (hardware behaviour), applied automatically by the build script |
| `runtime/` | Game overrides for this ELF (`PS2_REGISTER_GAME_OVERRIDE`) |
| `tools/` | Helper scripts: ELF inspection, Ghidra helpers, missing-function finder, build/run scripts, PCSX2 PINE reader |
| `docs/status.md` | Current state, findings, next steps |
| `game/`, `ghidra/`, `generated/`, `external/` | Local only, gitignored |

## Building (Windows)

You need Visual Studio 2022 Build Tools (C++), CMake, Ninja, Git and Python 3. Ghidra (with
[ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)) is only needed to
redo the function map.

1. Clone PS2Recomp next to this folder as `../tekken4-fork-ps2recomp` (the default tree of every script; `TEKKEN4_SRC`
   points them at another one) at the commit pinned in `docs/status.md`.
2. Copy the files from your disc into `game/disc/` (the ELF is `SCES_508.78`).
3. `tools\build_ps2recomp_tools.cmd` builds the recompiler.
4. `..\tekken4-fork-ps2recomp\out\tools\ps2xRecomp\ps2_recomp.exe config\ghidra_export.toml` writes `generated/`.
5. `tools\stage_generated.cmd` copies the generated code and our overrides into the runtime.
6. `tools\build_runtime.cmd` applies `patches\*.patch` and builds `ps2EntryRunner.exe`.
7. `tools\run_game.cmd` runs it (F1 toggles the runtime's debug panel; the output goes to `logs\run.log`).

## Thanks

- [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp): the recompiler and runtime everything here builds on.
- [Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port): a reference port
  whose notes saved a lot of guesswork.
- [PCSX2](https://pcsx2.net/): the ground truth for how the hardware behaves.
- [chaoticgd/ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) and
  GhidraMCP.

## License

GPL-3.0, like PS2Recomp (the patches and overrides are derived from its code). See [LICENSE](LICENSE).
Tekken is a trademark of Bandai Namco Entertainment; this project is not affiliated with or endorsed by them.
