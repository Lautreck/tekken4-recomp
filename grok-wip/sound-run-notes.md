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

## 2026-10-22 clip / heap

- ps2_sif_dma_tests.cpp:203 wants the first sceSifAllocIopHeap in [0x120000, 0x200000). HeapBase stays 0x80000 (the 0xE9400 block does not fit in 0x120000..0x1F0000). allocate() now bumps down from HeapLimit, so that 0x880/align-64 alloc is 0x1EF780. build_tests: Total 477 Passed 477 Failed 0, including "IOP heap DMA uses private backing instead of aliasing EE RDRAM".
- Mixer: dry sum is clamped to s16 before the master multiply, and the multiply is int64. The old `(sum * volumeOf) >> 14` in int32 wraps when sum > ~131075 (4 full-scale voices * 0x3FFF), and the wrap is the negative rail. PCSX2 Mixer.cpp clamp_mixes before ApplyVolume(MasterVol).
- sound-run-11 per-channel rails (wav_clips.py), not the mono-average peak: movie windows 8+44+19+436+670 = 1177. movie_peak.py on the ISO, same ADPCM as the runtime, also 1177 rails across 299 units. Those rails are the decoder saturating the source, which PCSX2's XA_decode_block does too. SPU-only rails: 42 in 55-60 s, 0 elsewhere. 0-5 s is 0 samples, and the mixer is what the WAV records (no zero pad). Sound init is before frame 1; the bed starts at 5 s, still on the display menu (frame 100 at 1.63 s, keys at 10 s).

## 2026-10-22 later (runs 12-14, mix measured)

Route unchanged: hybrid. TEKKEN4_SOUND=1 forwards sound RPCs to zsiopdrv; disc-read mode 0 and movie 0x3000/0x3100/0x3200 stay on the local HLE. Movie PCM is added after the SPU clamp (ps2_audio.cpp onMixedAudio).

Heap: allocate() bumps down from HeapLimit 0x1F0000. First sceSifAllocIopHeap(0x880, align 64) is alignDown(0x1F0000-0x880, 64) = 0x1EF780, which is inside the DMA test's [0x120000, 0x200000). HeapBase stays 0x80000 so AllocSysMemory(0xE9400) still fits. This heap direction is not behind TEKKEN4_SOUND. cmd /c tools\build_tests.cmd: Total Tests 478, Passed 478, Failed 0. "IOP heap DMA uses private backing instead of aliasing EE RDRAM" Passed. The test file was not edited. vswhere.exe warning is pre-existing and the run still exited 0.

Mix vs PCSX2: dry sum is clamped to s16 before the master multiply, multiply is int64 (sound_engine.cpp step). MVOL in sound-run-12 stderr is 0x3fff then 0x3f00 on both cores. volumeOf(0x3f00)=16128, and 32767*16128/16384 = 32256. After the fix, non-movie windows peak at 32256 with 0 rails. Before (sound-run-11, same keys) the 55-60 s window had 42 rails and mono peak 32768.

Per-channel rails, python -I logs/grok/wav_clips.py (sample <= -32768 or >= 32767):

sound-run-11 (73.000 s, 3504000 frames) before the clamp:
- 0-5 peak 0 rails 0
- 5-20 rails 0 (peaks 10751, 10924, 10924)
- 20-25 rails 8; 25-30 rails 44; 30-35 rails 19; 35-40 rails 436; 40-45 rails 670
- 45-55 rails 0; 55-60 rails 42 (peak 32768); 60-73 rails 0
- movie windows 8+44+19+436+670 = 1177

sound-run-12 (79.012 s, 3792553 frames) after the clamp, same keys, wall 95.2 s:
- 0-5 rails 0; 5-20 rails 0
- 20-25 rails 21; 25-30 rails 39; 30-35 rails 11; 35-40 rails 559; 40-45 rails 591 (sum 1221)
- 45-79 rails 0. 55-60 peak 32256 rails 0 (was 42). 75-79 peak 29267 rails 0
- RMS: 0-5 0; 5-10 0.020598/10751; 10-15 0.038847/10924; 15-20 0.032937/10924; 20-25 0.185903/28154; 25-30 0.213608/29461.5; 30-35 0.222394/29917.5; 35-40 0.220248/31506; 40-45 0.217270/32767; 45-50 0.022288/10460; 50-55 0.032937/10924; 55-60 0.111552/32256; 60-65 0.176262/25761; 65-70 0.107248/24401.5; 70-75 0.083275/17537; 75-79 0.103649/27662.5

Movie rails are the ADPCM decoder saturating, then the menu bed is added on top in onMixedAudio, so a WAV window can exceed the decoder's own rail count. Do not scale the movie down. sound-run-12 movie closed after 301 units (run 11 was 299).

Frames, sound-run-12 (frame_NNs.bmp via bmp_to_png, not the shot PNGs): frame_40s intro (Heihachi), frame_55s MODE SELECT, frame_75s round start STAGE 1 timer 60 Kazuya vs Law. frame_95s.bmp was not written: the capture closes at the last shot and the frameshot clock starts at override-apply, so the last mark loses the race. stdout line 154 is the window close right after frame_75 in the log; the process still ran 95.2 s.

sound-run-13 (same keys, shots through 110, wall 110.8 s, WAV 89.012 s): frame_40 intro, frame_55 MODE SELECT, frame_75 and frame_95 are CHOOSE YOUR FIGHTER (timer 20 then 08, Kazuya/Kazuya). The 0.5 s later key times landed on character select instead of the round. RMS 65-85 is 0 (digital silence on that screen), then 85-89 rms 0.173390 peak 32256 rails 0. Movie-window rails 17+43+11+504+637 = 1212.

sound-run-14 (Cross at 62/68/74, no Down; wall 130.2 s, WAV 101.000 s, 4848000 frames): frame_55 MODE SELECT, frame_75 and frame_95 are the attract GAME OVER demo (PRESS P1 START, INSERT COIN), frame_115 is the credit crawl. Not a played round. RMS 55-65 is 0, then the demo is loud and unclipped: 65-70 0.106471/26180, 70-75 0.052955/16823, 75-80 0.072453/29850, 80-85 0.188236/32256, 85-90 0.207656/32256, 90-95 0.209351/32256, 95-100 0.238801/32256, 100-101 0.131083/26619. Rails 0 from 45 s on. Movie-window rails 21+39+11+525+650 = 1246.

sound-run-15 (run-12 keys, shots 75,100,115,130,145, wall 145.1 s, WAV 100.012 s, 4800560 frames). Frames from frame_NNs.bmp: frame_75 round start STAGE 1 timer 60 Kazuya vs Law (00:00:00); frame_100 the round in progress (Law kicking, timer 56, clock 00:03:96); frame_115 Law on the ground (timer 49, clock 00:10:06); frame_130 still down (timer 43, clock 00:16:55). Timer moved, so this is a played round, not the attract demo. frame_145 was not written (last shot races the close). WAV ends at 100 s while the process ran to 145 s: samples follow the IOP clock (CyclesPerSample 768) and step() drops the output buffer past 2 s, so the 115 s and 130 s pictures are past the end of the file. Fight-window rails are 0. RMS:
- 0-5 0; 5-10 0.020598/10751; 10-15 0.038844/10924; 15-20 0.032941/10924
- 20-25 0.175495/27005.5; 25-30 0.212734/29461.5; 30-35 0.224194/29917.5; 35-40 0.216162/31506; 40-45 0.223931/32768
- 45-50 0.046470/21964; 50-55 0.032937/10924
- 55-60 0.100673/32256; 60-65 0.182278/29674; 65-70 0.107522/24401.5; 70-75 0.081888/17547
- 75-80 0.099361/27547.5; 80-85 0.168134/32256; 85-90 0.200156/32256; 90-95 0.198956/32256; 95-100 0.186557/32255
Movie-window rails 8+44+19+450+691 = 1212. 55-100 s rails 0 (sound-run-11 had 42 rails in 55-60). 32256 is 32767 * 0x3f00 / 16384, the post-clamp full-scale sample through MVOL 0x3f00.

0-5 s: every TEKKEN4_SOUND WAV this session (runs 11, 12, 13, 14) is digital zero for exactly 5 s, then the same bed (rms 0.020598 peak 10751). The WAV writer stores mixer frames, not a pad (header is 44 bytes; wav_rms counts frames). sound-run-12 stdout: main loop frame 100 at 1.63078 s, so the EE is in the menu loop during the silent window. The repeated peak means this is not a dropped buffer. No PCSX2 WAV was recorded. docs/status.md:418 puts the PCSX2 logo at 18-22 s including BIOS, which this runtime does not run.

Reverb: not changed. sound-run-12 stderr: mode 6 depth 10240/10240 vmixE 000000/000000 on both cores, and EVOL (id 0x0b/0x0c) is 0x2800 at that call. setEffectAttr turns the effect bit on, but the wet send is VMIXEL/VMIXER and both are 0 at the only effect calls. A later SetSwitch is not logged.

How to run (fight, and give the last shot ~15 s of spare so frame_NNs.bmp at the previous mark is written):

cmd /c tools\grok_build.cmd sound-run

powershell -File tools\grok_run.ps1 -Task sound-run -RunDir logs\grok\sound-run-N -Keys "10:Down","12:Cross","15:Left","16.5:Cross","45:Start","52:Cross","58:Down","60:Cross","66:Cross" -Shots 40,55,75,95,110 -Env TEKKEN4_SOUND=1,TEKKEN4_SOUND_WAV=logs\grok\sound-run-N\out.wav

One -Env, comma-separated. python -I logs/grok/wav_rms.py and python -I logs/grok/wav_clips.py. Judge frame_NNs.bmp. The literal keys 40:Down, 41.5:Cross, 46:Cross land during the movie (sound-run-10); Start at 45 is what reaches MODE SELECT.

## Files changed

- external/PS2Recomp/ps2xIOP/src/emulator/core/iop_memory.h — heap cursor starts at HeapLimit
- external/PS2Recomp/ps2xIOP/src/emulator/core/iop_memory.cpp — top-down allocate, alignDown
- external/PS2Recomp/ps2xIOP/src/emulator/core/sound_engine.cpp — clamp the dry sum before the int64 master multiply; capped [snd-mix] log
- logs/grok/wav_clips.py, logs/grok/movie_peak.py — measurements, not linked
- logs/grok/sound-run-notes.md

The [snd-mix] fprintf is not behind TEKKEN4_SOUND. It stops after 32 new values. A run with the sound route prints those lines; a run without the route does not init libsd.

## Known bugs

- 0-5 s is digital silence from the mixer (same bed starts at 5 s on every sound run). Not a WAV pad.
- Movie PCM still hits the rails. movie_peak.py on the ISO: 2143232 samples, peak 32768, 1177 decoder rails. The mix callback adds that PCM after the SPU clamp, so WAV movie windows can count more rails than 1177. Not a 2x gain bug.
- Reverb send VMIXE is 0 at the mode-6 call (depth 10240, EVOL 0x2800). Wet input is empty at that call.
- The WAV is shorter than the wall clock (sound-run-15: 100.012 s of audio, 145.1 s wall). Samples follow the IOP clock and the mix buffer drops past 2 s. frame_115 and frame_130 of that run are past the end of the WAV.
- sound-run-13 character select (timer counting 20 then 08) has 20 s of digital silence (WAV 65-85).
- The capture closes at the last -Shots mark, and the frameshot clock starts later than process start, so the last frame_NNs.bmp is not written. Give the last shot about 15 s of spare.
- sound-run-10 Start at 30 fell in the movie and did not skip. Start at 45 does.
- Key timing changes the screen: the same keys reached a round (runs 12 and 15), character select (run 13), or the attract demo (run 14, extra Cross, no Down).

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
