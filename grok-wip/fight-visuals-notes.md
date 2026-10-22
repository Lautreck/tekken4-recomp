# fight visuals 2026-10-22

One command: `powershell -File tools\fight_compare.ps1`
Defaults zip wall seconds, not a shared fight-second offset (our fight clock ~0.44x wall, PCSX2 ~1.0).
Pairs land in `<OutDir>\pair\mNN_ours.png` and `mNN_pcsx2.png`. Measured walls: ours 86,92,107 and PCSX2 73,76,83.
GS dump (no PERF_LOG): `powershell -File tools\fight_compare.ps1 -OutDir logs\grok\fight-visuals-gs -SkipPcsx2 -GsDumpFrame 3691`
3691 = sum of `[tekken4] perf second 0..91` in logs/grok/fight-visuals-1/ours/stdout.txt (g_frames at wall ~92 on that fast run).

## pairs (fight-visuals-1/pair)

| pair | ours | clock / timer | pcsx2 | clock / timer |
| m00 | frame_86s | 00:06:60 / 53 | pcsx2_73s | 00:06:30 / 53 |
| m01 | frame_92s | 00:09:10 / 50 | pcsx2_76s | 00:09:26 / 50 |
| m02 | frame_107s | 00:15:91 / 44 | pcsx2_83s | 00:16:25 / 43 |

Select, before the confirming Cross: select_44s_ours (countdown 27, shirtless 1P portrait) and select_48s_pcsx2 (countdown 24, shirtless 1P portrait, grid badge 1P).
Do not cite pair/t03_pcsx2, t09_pcsx2, t15_pcsx2, t24_pcsx2, select_96s_pcsx2: those are the run-1 menu PrintWindows (about 45KB / 13KB).

Capture: ours is the runtime frame_NNs.bmp (TEKKEN4_FRAME_SHOTS), converted with `python -I tools/bmp_to_png.py`. PCSX2 is PrintWindow. F8 and Shift+F8 wrote no file under external/pcsx2/snaps or dumps. PINE has no screenshot opcode and cannot read TEX0.

## costume

Tekkenpedia: T4 Kazuya 1P is the white gi pants, purple belt, red fingerless gloves. 2P is the grey-blue dress shirt, dark pants, sunglasses, same red gloves.
https://eng.tekkenpedia.com/wiki/Kazuya_Mishima/Outfits
Both runs send Cross (our KEY_X -> Pad.cpp bit 14; PCSX2.ini Cross=Keyboard/X). Pre-press portrait is 1P on both. After Cross, PCSX2 is 2P and ours stays 1P.
Ours mc0 is 0 bytes (fight-visuals-gs2). PCSX2 memcard was not diffed.

## GS dump frame 3691

logs/grok/fight-visuals-gs2. stdout: frame_92s.bmp then `GS dump written for frame 3691`. That png is STAGE 1, clock 00:08:66, timer 51, Kazuya 1P gi vs Law (same stage as fight-visuals-1). First dump attempt (fight-visuals-gs) ran ~22 fps and was still on the select screen at wall 92, so 3691 was never reached.
History tail (gs_dump) is HUD sprites, not the body. Body is in gif_packets_frame3691.txt (A+D and packed).
Packed RGBAQ is R=lo[7:0], G=lo[39:32], B=hi[7:0], A=hi[39:32]. Packed XYZ2 Y is lo[47:32]. XYOFFSET_2 seen as OFX 0x6c00 (1728), OFY 0x7900 (1936).
Body-sized group tbp 15616: 4320 verts, screen x 167..303 y 48..283, mean RGBA 123,142,141,128, 1933 verts with R,G,B all > 128. TFX 0 MODULATE, psm 19 (PSMT8), cbp 15899, ABE 0, FGE 0.
Screen-sized group tbp 15296: 4900 verts, x 425..524 y 187..245, mean 116,130,129,128, 1482 verts > 128. Same modulate / PSMT8 / ABE 0. Pixel box 425,187 99x58 on that png is 187,210,210 with 2510/5742 px > 230.
Darker world group tbp 9664: mean 58,62,61. Not a global blowout.
tbp 12800: mean 255,255,255 alpha 0, x 1185..1649 (off the 640-wide view).
ALPHA last 0x42=0x44, 0x43=0x48. Body prims have ABE clear, so that ALPHA is not the gi.

## pixels (fight_sample.py, this session)

fight-visuals-1 ours frame_92s 640x448: pants 120,360 50x40 = 215,211,207 n 2000 white 466 speck 0. win 520,160 60x50 = 136,191,191 n 3000 white 504 speck 0.
pcsx2_76s 656x519: shirt 170,240 40x30 = 110,106,117 white 0 speck 0. pants 90,320 50x50 = 56,60,73 white 0 speck 0.
gs2 frame_92s screen 480,80 140x120 = 135,202,202 n 16800 white 2120. body 167,48 136x235 = 91,92,83 white 93 speck 14 (box includes background).
