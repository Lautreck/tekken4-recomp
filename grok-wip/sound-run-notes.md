# sound run notes

Route: hybrid. TEKKEN4_SOUND=1 + ISO sets cdImage and forwards SID 0x12345 except mode-0 disc reads (0x200 mode 0) and movie calls 0x3000/0x3100/0x3200. Bind calls real sceSifBindRpc, then if client.server is still 0 plants a 64-byte guestCalloc placeholder (not the client). Sound calls go through ps2_syscalls::sceSifCallRpc. Mixer is GoW SoundEngine at the libsd import boundary; host sink is 48 kHz s16 stereo plus TEKKEN4_SOUND_WAV.

## sound-run-3 (placeholder exe, TEKKEN4_SOUND=1)

- Bind unblocked: stdout `[tekken4] sound RPC 0x12345 bound, client 0x1cccbc0 server 0x1e40f90 (placeholder, zsiopdrv not registered yet)`. Exit PC=0x196a78, not 0x2230f0. Frame 1000 at 19.99 s.
- stderr: `[libsd] sound route on` twice. Four `[IOP/RPC trace:unhandled] sid=0x12345` with `loadedModules=[]` (rpc 0x8000, 0x2100, 0x100, 0x200 mode 6). No `[IOP] loaded IRX`.
- out.wav: 24.000 s, 48000 Hz stereo s16, RMS 0.00 peak 0 in every 5 s window (python -I).
- Shots 14/25/40 s are a white window. Same exe also linked Claude's newer GS sources. No default-run control on this exe yet.
- Package at ELF file+0x165900 decodes (FUN_00197dd8 + word NOT) to 236160 bytes, count 6, all six entries ELF magic. Loader never saw them.

## Blocker found

sceSifSetDma writes the package into IOP RAM (`IopMemory`). `IopModuleLoader::readElfFromGuest` uses `IopHost::readGuest`, and the runtime adapter reads EE RAM. loadModuleBuffer therefore rejected the DMA'd ELFs with no log. Fix in iop_emulator.cpp: read an ELF from IOP RAM first, fall back to readGuest so the unit tests (image planted in the test host) still pass.

## How to run

```
cmd /c tools\stage_generated.cmd
set TEKKEN4_BUILD_DIR=grok
cmd /c tools\build_runtime.cmd
powershell -File tools\run_capture.ps1 -RunDir logs\grok\sound-run-N -Keys "10:Down","12:Cross","15:Left","16.5:Cross","30:Start","36:Cross" -Shots 14,25,40
```
Env: TEKKEN4_SOUND=1 and TEKKEN4_SOUND_WAV=logs\grok\sound-run-N\out.wav. Pass -Keys from PowerShell, not cmd /c. Display mode is PAL60 (10 Down, 12 Cross).
