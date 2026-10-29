# Missing background music (BGM): cause, fix, evidence

US build SLUS-20328, `TEKKEN4_SOUND=1` (real Namco driver zsiopdrv on PS2Recomp's emulated IOP). Before the
fix: effects and voices played, the music did not. Session 2026-10-29 (worktree `D:\tekken4-fork-music`, branch
`t4-music` = `t4-us` + 3 commits). IOP addresses below come from an IOP RAM dump of this build.

## Cause

There are two faults. The first one alone is enough to silence the music.

1. **The SPU2 interrupt was never delivered.** zsiopdrv streams the BGM from the disc itself. Its CD thread
   (`sceCdRead` call at IOP 0x48e48, queue at 0x48a28, type 2) reads 64 KB blocks into IOP 0x160100..0x1a0100
   about every 1.3 s (= 44.1 kHz stereo ADPCM). A refill thread copies them on to the SPU ring at 0x1bc000/0x1bc400,
   which is played by core 1 voices 16/17 (pitch 0x0eb2 = 44.1 kHz, volume left-only/right-only). That thread runs
   on the **SPU2 interrupt**: IRQA on core 1 ping-pongs between 0x1b8000 and 0x1b8100, and the handler 0x4f644
   (registered with `sceSdSetSpu2IntrHandler`) signals the thread's semaphore. The thread then refills with
   `sceSdVoiceTrans` 256/512 B pieces and moves IRQA. The libsd HLE stored the handler but never called it. The
   sound engine only counted IRQA crossings for `TEKKEN4_SOUND_TRACE`. Without the interrupt, the stream ring kept
   the silent loop it was given at start-up, so voices 16/17 played silence.
   (The old notes said "the IOP read only two banks": the `[cdvd]` log line is capped at 6 lines, but the reads
   go on all the time.)
2. **The EE's SIF commands never reached the IOP.** zsiopdrv also pulls sound data from EE RAM. It sends SIF
   command 2 (`0xe993/0x1390/0x1391/..` + sector, IOP destination, size) to the EE handler 0x228a08. The EE main
   loop (0x228d90) answers with FUN_00228cc8 = `sceSifSendCmd(cid 0, 0x1ccacc0, 0x24, EE src, IOP dest, size)`.
   That broke in two places. `ps2_syscalls::sceSifSendCmd` read `dest`/`size` from the stack, but on the EE ABI
   they are in `$t0`/`$t1`, and it copied into **EE** RAM. The IOP sifcmd HLE also returned 0 for
   `sceSifAddCmdHandler` and dropped it, so the driver's cid-0 handler 0x4f4c0 (stores the sector count at
   0x521d0) never ran. The payload is 8-16 KB pieces to IOP 0x148100/0x150100, sent when the character select
   loads. The earlier "music-3" note "refuted" this because the handler "only stores a size". The data itself
   travels in the SIF DMA payload, which was lost.

## Fix (fork branch t4-music, D:\tekken4-fork-music)

| commit | what |
|---|---|
| `8f5b93b` SIF: EE sceSifSendCmd payload ($t0/$t1) to IOP RAM, packet to the IOP module's handler | `RPC.cpp` reads `$t0/$t1`, writes the payload with `writeIopMemory`, queues the packet (`ps2x::iop::queueEeSifCommand`). The `SIF.cpp` stub delegates to it. The IOP keeps `sceSifAddCmdHandler`/`Remove` per cid (sound route only) and calls the handler with the SIF header words (size \| dsize<<8, dest, cid) at the start of each `runCycles`. |
| `9774f77` ps2xIOP: deliver the SPU2 interrupt to libsd's handler | The engine sets a core's IRQ bit when a voice decodes the block that holds that core's IRQA (IRQ enable on). After each mixer advance the IOP clears the cores' IRQ enable and calls the `sceSdSetSpu2IntrHandler` handler (core bits, arg) or the `sceSdSetIRQCallback` callback, as ps2sdk libsd's `Spu2Interrupt` does. |
| `16bc431` ps2xIOP: sound debug aids (env-gated) | `TEKKEN4_SOUND_VOICES=1` (voices, libsd call counts, IRQs, IOP->EE commands), `TEKKEN4_SOUND_SOLO=<c0 mask>,<c1 mask>`, `TEKKEN4_IOP_DUMP=<s>,..` (IOP + SPU RAM), `TEKKEN4_IOP_TRACE=<pc>,..`. |

Game override (main repo, `runtime/tekken4_overrides.cpp`, sound part, not committed): `sifSendCmd` now leaves
every cid except SET_SREG to the fixed `ps2_syscalls::sceSifSendCmd`. `TEKKEN4_BGM=0` brings back the old
behaviour for A/B runs: the EE command is dropped here, and the IOP skips the SPU2 interrupt.
New tools: `tools\wav_sections.py` (level + spectrum per named section), `tools\run_music_ab.ps1` (the evidence
runs below).

## Evidence

All runs use the standard path `8:Start,13:Start,17:Start,21:Cross,26:Start` (movie skip, MEDIUM, Story Battle,
Kazuya, prologue skip; fight from ~32 s), with WAVs in `logs\music_ab_<variant>\mix.wav`. Sections are in WAV
seconds. "stream" = only the driver's stream voices (core 1: 16-17 and 20-23) in the mix
(`TEKKEN4_SOUND_SOLO=0,f30000`). "fx" = every voice except those. "base" = `TEKKEN4_BGM=0` (before the fix).

| section | base stream rms | **fix stream** rms / quiet share / L-R corr / energy <250 Hz | fix fx rms | base full rms | fix full rms |
|---|---|---|---|---|---|
| title 9-12.5 s | 0.000 | 0.000 (no BGM on the title) | 0.207 | 0.209 | 0.202 |
| mode select 14-17 s | 0.000 | 0.058 / 0.93 / 0.45 / 0.59 (music starts ~16 s) | 0.274 | 0.273 | 0.297 |
| char select 18-26 s | 0.000 | **0.229 / 0.04 / 0.54 / 0.70** | 0.382 | 0.374 | 0.439 |
| prologue 27-31 s | 0.000 | 0.094 / 0.78 / 0.53 (track change) | 0.407 | 0.405 | 0.435 |
| fight 34-58 s | 0.000 | **0.189 / 0.00 / 0.92 / 0.78** | 0.593 | 0.562 | 0.616 |

Stream-only rms in 2 s windows. In the fix it is about 0.16-0.30 from 16 s to 26 s, 0 during the 26-30 s
transition, then about 0.17-0.28 without a gap to the end of the fight. In the base it is 0.000 in every window
after the movie (the 6-10 s values in both are the movie PCM, which is mixed after the voices).
The music is a real stereo signal (L/R correlation 0.45-0.92, while every effect voice here is centred: 1.00),
has no quiet gaps and is bass-heavy, and the full mix of the fix is stereo for the first time (L/R 0.89 in char
select vs 1.00 in the base).

Effects and voices show no regression: the effects-only mix of the fix matches the full base mix in every section
(rms 0.207/0.274/0.382/0.407/0.593 vs 0.209/0.273/0.374/0.405/0.562, same spectrum shape, mono).

Driver activity with the fix (`TEKKEN4_SOUND_VOICES=1`, logs\music3): about 49 SPU2 IRQs/s and 295
`sceSdVoiceTrans`/s, steady from boot to the fight. EE->IOP: 4 commands in 50 s (char-select data), each
answered (`IOP->EE cid=2 body e993 f7 148100 4000` -> `EE cmd cid=0x0 -> handler 0x4f4c0` -> `b922` ack).

FPS (`TEKKEN4_PERF_LOG=70`, mean frames/s over perf seconds 36-58 = the fight, two alternating pairs,
`logs\music_fps{1,2}_{fix,base}_full`): fix **55.1 / 53.3**, base **53.0 / 53.9**. The fix costs nothing
measurable. However, **neither** variant reached 60 tonight: every fight second was below 58 in all four runs,
other agents' runs/builds shared the machine, and the earlier sound-on runs logged 60.1. So the run-to-run
difference here is about +-1 fps; recheck the absolute 60 on a quiet machine.

The round timer in the fight screenshots agrees: from `shot_40s` to `shot_60s` (20 s wall time) the
in-game fight clock moves 00:00:70 -> 00:19:23 in the fix (`music_fps1_fix_full`) and 00:00:66 -> 00:19:28 in the
base (`music_ab_base_full`). That is ~18.5 game seconds per 20 s in both (the round timer counts 59 -> 40/41),
which matches the ~54-55 fps above. It is the same in both, so it is not caused by the fix.

IOP unit tests (`tools\build_iop_tests.cmd`, TEKKEN4_SRC=D:\tekken4-fork-music): emulator, import and
compatibility tests (18/18) all pass; `tools\build_tests.cmd`: ps2x_tests 482/482.

## Reproduce

```
set TEKKEN4_SRC=D:\tekken4-fork-music
set TEKKEN4_BUILD_DIR=music
tools\build_runtime.cmd            (after tools\stage_generated.cmd)
powershell -File tools\run_music_ab.ps1 -Prefix music_ab
external\pytools\Scripts\python.exe -I tools\wav_sections.py logs\music_ab_fix_stream\mix.wav title=9-12.5 mode=14-17 charsel=18-26 prologue=27-31 fight=34-58
```

## Not fixed / found on the way

- **Effect voices that never stop (pre-existing, also with `TEKKEN4_BGM=0`).** Every menu press keys a core 1
  voice (SSA 0xcef0, 0x77a0, 0x9dc0, ...) that plays on through the whole 1.15 MB bank at 0x5010. That bank (it
  matches the disc byte for byte, LSN 20188) has no ADPCM end flag between 0x5010 and 0xb6030, and the SSAs point
  into the middle of continuous audio. The EE frees voices only through ENDX (reply words 1/2 of call 0x100 ->
  FUN_00223ea0) and almost never sends KOFF (1 in 50 s). So the voices pile up: the full mix rises to rms ~0.6
  and clips in the fight (5-9 % of samples at the rail) and drowns the music. This has to be checked against
  PCSX2: what stops these voices there (an EE-side duration, different SSAs, or a different bank layout).
- The host warning "AUDIO: Failed to initialize playback device" only means there is no output device. At night
  the RME ADI-2 DAC endpoints are all "Unknown" (off); only a microphone is present. The IOP mix and the WAV do not
  depend on the device (`MixedOutput::push` writes the WAV first, `pumpDevice` waits for `IsAudioDeviceReady`).
- The core 1 voices 20-23 (22 kHz, ring 0x1b8000, IRQ ping-pong) are a second stream that stays silent in these
  scenes (voice lines?). It is refilled the same way.
