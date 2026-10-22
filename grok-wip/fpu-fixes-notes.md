fpu-fixes 2026-10-22. generated/ not regenerated. fpu_translator.cpp not edited. SQRT.S stays ft (fpu_translator.cpp:60-62).

1 CVT.W.S
ps2_cvt_w_s macros.h:939. exp&0x7F800000 <= 0x4E800000 -> (int32_t) trunc toward 0; else +0x7FFFFFFF / -0x80000000. FPU_CVT_W_S calls it (macros.h:948). Emitter still FPU_CVT_W_S(ctx->f[fs]) memcpy to fd (fpu_translator.cpp:77-78). Kind: COP1_S_CVT_W behavior only; existing generated C++ picks it up on recompile. ROUND/TRUNC/CEIL/FLOOR unchanged.
Test run "CVT.W.S chops toward zero and saturates like PCSX2" passed (16 Equals: +-0.5, +-1.5, +-1.9, 2^31, -2^31, 0xCF000001, 0x4EFFFFFF, 0xCEFFFFFF, +-FLT_MAX, exp255 +-). Emitter IsTrue in "DIV.S and RSQRT.S translate..." passed. -2^31-1 is not a float32; it rounds to -2^31.

2 VCLIP
VU0_S2_VCLIPw emitter vu_translator.cpp:264-277 calls ps2_vu_clip (macros.h:382). Signed integer compare, w exp0 forced to 0x007fffff, bit0 x>+|w| 0x01, bit1 x<-|w| 0x02, y 0x04/0x08, z 0x10/0x20, (clip<<6)|flags & 0xFFFFFF. Field broadcast kept. Kind: VU0_S2_VCLIPw only. Needs regen. VU1 clipOp not touched.
Test run "VCLIP side bits and 24-bit history" passed (10 Equals + 3 IsTrue). (2,-3,0.5) vs |w|=1 -> 0x09; w negative same; x=-2 -> 0x02; y=4 z=-4 -> 0x24; history 0xFFFFFF -> 0xFFFFC0; denormal x vs denormal w stays 0; normal x vs w=0 sets bit0.

3 MAC
vu_fmac inserts ps2_vu_mac_apply on res before the closing brace (vu_translation_helpers.cpp:12-21). 42 call sites. VOPMSUB and VOPMULA use mask 0xE and clear_absent false (lines 413, 739). Every other wrap uses dest_mask and clear_absent true. Lane shifts x=3 y=2 z=1 w=0; Z=0x1 S=0x10 U=0x100 O=0x1000. status replaced by Z/S/U/O summary (macros.h:423-441), v2.8.2 VU_STAT_UPDATE, not sticky. CFC2 MAC emitter still reads vu0_mac_flags. fd==0 still stores vf[0].
Kinds whose text gains ps2_vu_mac_apply (72 opcodes, 42 translators):
S1 VADDx/y/z/w VSUBx/y/z/w VMADDx/y/z/w VMSUBx/y/z/w VMULx/y/z/w VMULq VMULi VADDq VMADDq VADDi VMADDi VSUBq VMSUBq VSUBi VMSUBi VADD VMADD VMUL VSUB VMSUB VOPMSUB
S2 VADDAx/y/z/w VSUBAx/y/z/w VMADDAx/y/z/w VMSUBAx/y/z/w VMULAx/y/z/w VMULAq VMULAi VADDAq VMADDAq VADDAi VMADDAi VSUBAq VMSUBAq VSUBAi VMSUBAi VADDA VMADDA VMULA VSUBA VMSUBA VOPMULA
Not wrapped: VMINI/VMAX and .x/.y/.z/.w/.i, VDIV VSQRT VRSQRT VITOF integer memory RNG ABS MOVE.
Needs regen. Test run "FMAC writes the MAC word CFC2 reads" passed (6 Equals + 6 IsTrue): VADD -1 -> mac 0x00F0 status 2; VOPMSUB-style xyz mask keeps prior W 0x1111 and sets Sx; X-only +0 from 0xFFFF -> 0x8; -0 -> 0x88; emitter strings for VADD, VOPMSUB, VADDx; VMINI has no apply; CFC2 MAC reads vu0_mac_flags.

4 VFTOI
ps2_vftoi macros.h:445. shift 0/4/12/15 scales by 1<<shift, exp>=0x4F000000 saturates +0x7FFFFFFF / -0x80000000, else cvttps. translateVU_VFTOI helpers.cpp:763. Kinds: VU0_S2_VFTOI0/4/12/15. Needs regen. VITOF unchanged.
Test run "VFTOI saturates at 2^31" passed (8 Equals + 5 IsTrue): 2^31 -> 0x7fffffff, -2^31 -> 0x80000000, 0.5 -> 0, -1.5 -> -1, 2147483520 stays, VFTOI4 134217728 and -134217728 saturate, 1.5 -> 24, -1.5 -> -24. Emitters pass shift 0/4/12/15. VFTOI0 string has no bare cvttps.

checks
cmd /c tools\build_ps2recomp_tools.cmd exit 0 after the final vu_translation_helpers.cpp edit. Relinked out/tools ps2xRecomp/ps2_recomp.exe.
cmd /c tools\build_tests.cmd: Total 476, Passed 475, Failed 1. The four runs above passed (log lines 259, 713-715, 749-751).
Failed run is unowned: ps2xTest/src/ps2_sif_dma_tests.cpp:183 "IOP heap DMA uses private backing instead of aliasing EE RDRAM", first IsTrue at :203 (iop heap address not in 0x00120000..0x00200000). Not edited.
First parallel tools+tests build: C2664 at macros.h:458 (_mm_blendv_ps result passed back through _mm_castsi128_ps). Fixed by returning the __m128. Same build also C1090 PDB on unowned ps2_audio_vag.cpp, ps2_gs_memory.cpp, ps2_audio.cpp, game_overrides.cpp, gs_cpu_backend.cpp, gs_frontend.cpp. Retry of tests alone compiled those files.

contra
MAC O/U/Z are taken from the value PS2_VADD/VSUB/VMUL already stored. Exp0 is flushed to signed 0 first, so a denormal lane becomes S|Z and not U. A value already clamped to 0x7FFFFFFF does not raise O. PCSX2 flags the pre-clamp result.
vu0_status is replaced, not OR'd. Audit cfc2.status n=3 at 0x293b8c, 0x293bec, 0x293c60; two follow CTC2 status plus VSUBs (audit json fact, not re-disassembled here). Those reads will see the VSUB summary instead of the CTC2 value. microVU status is sticky; this matches interpreter VU_STAT_UPDATE.
fd==0 still writes vf[0]. Audit .text fd==0 count is 0.
Exp255 stays a finite exponent in PS2_VADD (locked by ps2autotests). CVT.W.S still saturates exp 255, which is the PCSX2 CVT_W raw-exp test.
SQRT/RSQRT sign model not flipped.
Macro VCLIP is the integer _vuCLIP compare. VU1 clipOp stays the float fabs compare. They differ on denormals.
Flags are written immediately. Hardware FMAC flags have about a 4-cycle delay. The four CFC2.MAC sites are 1-5 insns later (audit), so the immediate write is what those reads need.

open
generated/ not regenerated. CVT changes on the next game compile. VCLIP, the 72 FMAC kinds, and VFTOI0/4/12/15 change only after Claude regenerates.
Unowned SIF DMA test failure above.
No game run.
