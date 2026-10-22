# sound run notes

Route: hybrid. TEKKEN4_SOUND=1 plus an ISO sets cdImage and forwards SID 0x12345 except disc-read 0x200 mode 0 (the EE loader checksums that copy) and movie calls 0x3000/0x3100/0x3200 (existing stream HLE, word-3 status bits still OR'd onto forwarded 0x100). Sound calls 0x8000, 0x100, 0x2100, 0x2200, 0x600, 0x1100, 0x3300 and mode-6 0x200 go to zsiopdrv through ps2_syscalls::sceSifCallRpc. Mixer is the GoW SoundEngine (GPL-3.0, origin/runtime) at the libsd import boundary inside iop_emulator.cpp, not the unused EE RPC in modules/libsd.cpp. Host sink is 48 kHz s16 stereo (raylib) plus TEKKEN4_SOUND_WAV.

Why hybrid: mode-0 reads and the movie were already correct on the local HLE, and the real driver never has to serve them. The silence was the real driver not finishing init.

## What was blocking audio

1. IOP sifcmd GetSreg (ordinal 6) returned 0. zsiopdrv's 0x8000 handler spins at 0x4ca40 until GetSreg(31) != 0. The EE does send it, before the bind, from 0x229608: SIF_CMD_SET_SREG cid 0x80000001, packet+16 = index 31, packet+20 = 1. ps2_syscalls::sceSifSendCmd dropped it. Fix (sound route only): tekken4 replaces sceSifSendCmd 0x281F40 and stores the 32 software registers; iop_emulator answers sifcmd ordinals 6 and 7 from that store.
2. AllocSysMemory(0xE9400) did not fit in the old heap 0x120000..0x1F0000 (0xD0000). The driver then filled from address 0 with 0xffffffff and wiped its own loop at 0x49958 (reserved-instruction storm, sound-run-7). HeapBase is now 0x80000 (still above zsiopdrv, which ends near 0x53000). This is not behind the env var: module load addresses are unchanged; later sysmem blocks move down. The no-sound run still boots.
3. An IOP exception with no BIOS spins in low RAM until the 2e6 RPC budget (sound-run-6 pc=0x80000a00). On the sound route the emulator logs the EPC and resumes past the fault. sound-run-8 took no exception.

## sound-run-8 (TEKKEN4_SOUND=1) — audible

- stdout: SIF_CMD_SET_SREG index=31 value=1. Forwarded 0x8000, 0x2100, 0x100. Mode-6 disc read sector 9378 size 1204352. Frame 1000 at 16.71 s (~60 fps). Window close at 40.2 s, PC=0x196bf8.
- stderr: GetSreg 31 = 0x1. VoiceTrans ch=1 10240 bytes then 2048, then ch=0 banks of 131072 (sceCdRead lsn 10614.. sectors=64 ok) down to a 24704 tail. KON mask 0xf30000 then voice 0. No exception, no budget hit, no ioman open (the bank path is sceCdRead, not ioman).
- out.wav: 37.002 s, 48000 Hz, stereo s16, 1776074 frames. python -I logs/grok/wav_rms.py:
  - 0-5 s rms 0 peak 0
  - 5-10 s rms 0.020598 peak 10751
  - 10-15 s rms 0.038847 peak 10924
  - 15-20 s rms 0.032937 peak 10924
  - 20-25 s rms 0 peak 0
  - 25-30 s rms 0.075808 peak 25920
  - 30-35 s rms 0.054426 peak 21964
  - 35-37 s rms 0.021294 peak 9259
- Shots 14/25/40 s are a white window. MODE SELECT was not readable off the image. The loop was still running at 60 fps when the capture closed.

## sound-run-9-nosound (env unset)

- params.txt has only TEKKEN4_BUILD_DIR=grok-sound-run.
- stdout: bound to silent HLE, client 0x1cccbc0. Frame 1000 at 16.76 s. Window close at 40.2 s, PC=0x22aaf8 RA=0x196a58 (main-loop caller). No sound-route line.
- shot_40s.png is the same white window. Menu text not confirmed visually. Boot is not stuck on the bind spin.

## How to run

cmd /c tools\grok_build.cmd sound-run

powershell -File tools\grok_run.ps1 -Task sound-run -RunDir logs\grok\sound-run-N -Keys "10:Down","12:Cross","15:Left","16.5:Cross","30:Start","36:Cross" -Shots 14,25,40 -Env TEKKEN4_SOUND=1,TEKKEN4_SOUND_WAV=logs\grok\sound-run-N\out.wav

One -Env, comma-separated. Two -Env flags fail to bind. Display mode is PAL60 (10 Down, 12 Cross). Omit the -Env pair for the silent HLE. python -I logs\grok\wav_rms.py <wav>

## What plays

Non-zero mixer output from about 5 s (after the silent logo window) through 20 s, silence 20-25 s, then louder output 25-37 s (peak 25920). Content is zsiopdrv VoiceTrans into the libsd mixer, not the movie HLE. Nobody listened; the WAV is the evidence.

## Known bugs

- 0-5 s and 20-25 s are digital silence. Fidelity (pitch, missing voices, reverb) is unverified. Reverb is stored, not mixed.
- Shots are white, so the menu was not confirmed on screen. Same in the no-sound run.
- Exception resume (sound route only) skips a faulting IOP instruction. It did not run in sound-run-8. A repeating EPC stops the call after 64 hits.
- HeapBase 0x80000 is process-wide. Modules still load where they did (under 0x54000). Sysmem allocations start lower.
- ioman cdrom open/read exists but this boot used sceCdRead.
- Movie calls stay on the local HLE, so movie PCM is not mixed from zsiopdrv.

## Files changed

- runtime/tekken4_overrides.cpp — SendCmd hook only when TEKKEN4_SOUND=1
- external/PS2Recomp/ps2xIOP/include/ps2x/iop/iop_host.h — sreg store API
- external/PS2Recomp/ps2xIOP/src/emulator/iop_emulator.cpp — sifcmd 6/7, budget log, exception resume
- external/PS2Recomp/ps2xIOP/src/emulator/core/iop_memory.h — HeapBase 0x80000
- external/PS2Recomp/ps2xIOP/src/emulator/imports/iop_ioman.cpp — first-read log
- external/PS2Recomp/ps2xIOP/src/emulator/imports/iop_cdvd.cpp — first sceCdRead log
- logs/grok/wav_rms.py and the zsiop_*.py dumps
