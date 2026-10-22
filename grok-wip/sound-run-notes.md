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

## Movie PCM (sound-run-11)

Route stays hybrid. The 0x3200 HLE still strips the 2 audio sectors. When TEKKEN4_SOUND=1 those sectors are decoded (SPU ADPCM, 16-byte L then R, flag 0x02, predictor kept across units) and added in ps2_audio's mix callback. 3584 stereo frames per unit = 74.7 ms at 48 kHz. ps2ClearMoviePcm on open and on 0x3100. Probe of unit 0 (sector 58196): L/R correlation +0.80.

sound-run-11 stdout: movie open file 0x1 sector 58196 size 128974848 mode 0; "movie audio: unit 0 -> 3584 stereo frames"; "movie audio closed after 299 units" then call 0x3100. 299 units * 74.7 ms = 22.3 s of PCM. The 20-45 s windows are that movie (frame_40s.bmp is the intro picture). After the close, 45-55 s drops to the menu level.

## Reverb

sceSdSetEffectAttr layout is int mode at +4, short depth_L at +8, short depth_R at +10, int delay at +12, int feedback at +16 (ps2sdk sceSdEffectAttr). The old code read depth_R from +12, which is the delay word. Presets are the 32 SPU words from ps2sdk iop/sound/libsd/src/effect.c. step() runs the psx-spx reverb recurrence at 24 kHz. Wet input is VMIXEL/VMIXER, output scale is EVOL (depth).

sound-run-11 stderr: boot sets mode 7 (echo) depth 0/0 delay 8 feedback 112 vmixE 0/0. Later, mode 6 (space) depth 10240/10240 delay 0 feedback 0 vmixE 0/0 on both cores. Depth 10240 is a real EVOL. VMIXE is 0 at both calls, so the wet send is empty unless a later SetSwitch (not logged) turns voices on. This WAV does not prove a wet tail.

## sound-run-11 (TEKKEN4_SOUND=1, fight)

Keys that landed: Down 10.3, Cross 12.4, Left 15.4, Cross 16.8, Start 45.3, Cross 52.3, Down 58.3, Cross 60.3, Cross 66.3. Start is at 45 s because frame_40 is still the movie; Start at 30 (sound-run-10) fell in the black before the picture and did not skip.

Frames: frame_40s.bmp intro movie, frame_55s.bmp MODE SELECT (Arcade highlighted), frame_75s.bmp round start (timer 60, Kazuya/Law). frame_95s.bmp was not written. shot_95s.png is the fight, timer 59. Guest WAV is 73.000 s (3504000 frames) for a 95.2 s wall run.

python -I logs/grok/wav_rms.py logs/grok/sound-run-11/out.wav:
- 0-5 s rms 0 peak 0
- 5-10 s rms 0.020598 peak 10751
- 10-15 s rms 0.038837 peak 10924
- 15-20 s rms 0.032948 peak 10924
- 20-25 s rms 0.175205 peak 27005.5
- 25-30 s rms 0.212659 peak 29461.5
- 30-35 s rms 0.224137 peak 29917.5
- 35-40 s rms 0.215849 peak 31506
- 40-45 s rms 0.224307 peak 31597.5
- 45-50 s rms 0.040711 peak 21964
- 50-55 s rms 0.038847 peak 10924
- 55-60 s rms 0.185350 peak 32768
- 60-65 s rms 0.141915 peak 24802
- 65-70 s rms 0.074811 peak 17547
- 70-73 s rms 0.083482 peak 15947

20-25 s was rms 0 in sound-run-8. It is the movie now.

## How to run

cmd /c tools\grok_build.cmd sound-run

powershell -File tools\grok_run.ps1 -Task sound-run -RunDir logs\grok\sound-run-N -Keys "10:Down","12:Cross","15:Left","16.5:Cross","45:Start","52:Cross","58:Down","60:Cross","66:Cross" -Shots 40,55,75,95 -Env TEKKEN4_SOUND=1,TEKKEN4_SOUND_WAV=logs\grok\sound-run-N\out.wav

One -Env, comma-separated. Check frame_NNs.bmp. python -I logs/grok/wav_rms.py <wav>

## Known bugs

- 0-5 s is still digital silence (logo).
- Movie PCM is hot (peaks 27000-31600, one later window hits 32768). Pitch and channel assignment are from the probe, not from a listen.
- Reverb send VMIXE is 0 at the mode-6 call, so the space preset may not be in the WAV.
- frame_95s.bmp was not written on the fight run. shot_95s.png shows the round.
- sound-run-10 used the same binary with Start at 30. Left landed at 18.9 s, the movie was mode 1 (logo, file 0x1d), and it never closed. Not the fight.

## Files changed (this step)

- runtime/tekken4_overrides.cpp — decode the movie ADPCM pair and queue it when TEKKEN4_SOUND=1
- external/PS2Recomp/ps2xRuntime/include/runtime/ps2_audio.h — ps2PushMoviePcm / ps2ClearMoviePcm
- external/PS2Recomp/ps2xRuntime/src/lib/ps2_audio.cpp — add that queue in the mix callback
- external/PS2Recomp/ps2xIOP/src/emulator/core/sound_engine.h — setEffectAttr, reverb state
- external/PS2Recomp/ps2xIOP/src/emulator/core/sound_engine.cpp — SPU reverb recurrence + libsd presets
- external/PS2Recomp/ps2xIOP/src/emulator/iop_emulator.cpp — sceSdEffectAttr field offsets
- logs/grok/movie_audio_probe.py — layout probe, not linked
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
