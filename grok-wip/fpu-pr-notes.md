Match EE CVT.W.S and VU0 macro CLIP, MAC flags, and VFTOI to PCSX2

Problem

CVT.W.S
FPU_CVT_W_S was (int32_t)nearbyintf, so it rounded to nearest. PCSX2 v2.8.2 CVT_W (pcsx2/FPU.cpp:254-257) chops toward zero when (bits & 0x7F800000) <= 0x4E800000 and otherwise saturates: positive to 0x7fffffff, negative to 0x80000000. The EE comes out of reset with rounding mode chop. Tekken 4 SCES-50878 .text has 497 cvt.w.s and 0 ctc1, so the game never selects round-nearest. ROUND.W.S, TRUNC.W.S, CEIL.W.S, and FLOOR.W.S are unchanged. The COP1 emitter is still FPU_CVT_W_S(ctx->f[fs]) copied into fd. SQRT.S is not in this change (open PR #382).

VCLIP
VU0_S2_VCLIPw compared with SSE cmplt/cmpgt, packed the less-than mask into bit 0 and the greater-than mask into bit 1, and compared against the raw broadcast w. PCSX2 _vuCLIP (pcsx2/VUops.cpp:909-924) does a signed integer compare. A w whose exponent field is 0 is compared as the max denormal 0x007fffff. Bit 0 is x > +|w| (0x01), bit 1 is x < -|w| (0x02), then y (0x04/0x08) and z (0x10/0x20). The previous flags shift up by 6 and the register keeps 24 bits. Tekken has 35 vclipw. All 21 CFC2.CLIP reads are 5 to 15 instructions after a macro VCLIPw. The first pair is the op at 0x18ed08 and the read at 0x18ed1c. None of those reads follows VCALLMS. VU1 clipOp is not changed.

MAC flags
The macro FMAC emitters computed the vector and never wrote vu0_mac_flags. CFC2 of the MAC register already reads that word. PCSX2 applyBinaryMACOp, applyTernaryMACOp, _vuOPMULA, and _vuOPMSUB update the flags through VU_MAC_UPDATE and then VU_STAT_UPDATE (pcsx2/VUflags.cpp:15-96, pcsx2/VUops.cpp:530-538 and 841-869). A lane the op does not write is cleared (VU_MACx_CLEAR). OPMULA and OPMSUB never touch W, so those bits stay. VU_STAT_UPDATE replaces statusflag with the Z/S/U/O summary (0x1/0x2/0x4/0x8). Lane shifts are x=3, y=2, z=1, w=0, with Z=0x1, S=0x10, U=0x100, O=0x1000 at that shift. Tekken has 4 CFC2.MAC reads, all after a macro ALU and none after VCALLMS: 0x191a80 after VADD, 0x191b5c two instructions after VADD, 0x1938d0 five instructions after VOPMSUB, and 0x1d6e98 in the same VOPMSUB pattern.

VFTOI
VFTOI0/4/12/15 scaled and used _mm_cvttps_epi32 with no saturation. PCSX2 floatToInt<Offset> (pcsx2/VUops.cpp:877-893) multiplies by 2^Offset and, when the exponent is >= 0x4F000000, returns 0x7fffffff or 0x80000000. Otherwise it chops toward zero. Tekken has VFTOI0 x15 and VFTOI4 x25. VITOF is unchanged.

Per-file changes

ps2xRuntime/include/ps2_runtime_macros.h
ps2_cvt_w_s implements the CVT_W test. FPU_CVT_W_S calls it.
ps2_vu_clip implements _vuCLIP.
ps2_vu_mac_lane is one lane of VU_MAC_UPDATE. ps2_vu_mac_apply updates the written lanes, optionally clears the others, and replaces the status word with the Z/S/U/O summary.
ps2_vftoi implements floatToInt for shifts 0, 4, 12, and 15.

ps2xRecomp/src/lib/vu_translator.cpp
VU0_S2_VCLIPw stores the fs lanes and the broadcast ft lane and calls ps2_vu_clip. The field broadcast is unchanged.

ps2xRecomp/src/lib/vu_translation_helpers.cpp
vu_fmac inserts ps2_vu_mac_apply on the pre-blend res before the closing brace. 42 call sites. VOPMSUB and VOPMULA pass mask 0xE and clear_absent false. Every other wrap passes the instruction dest mask and clear_absent true. VMINI, VMAX, VDIV, VSQRT, VRSQRT, VITOF, integer, memory, and RNG ops are not wrapped. translateVU_VFTOI calls ps2_vftoi(ctx->vu0_vf[fs], shift) instead of a bare cvttps.

ps2xTest/src/ps2_vu_tests.cpp
Suite PS2VuFpuFixes: "VCLIP side bits and 24-bit history", "FMAC writes the MAC word CFC2 reads", "VFTOI saturates at 2^31". The emitter checks decode a COP2 word through R5900Decoder and CodeGenerator.

ps2xTest/src/ps2_float_semantics_tests.cpp
The existing "DIV.S and RSQRT.S translate to the PS2 semantics helpers" run also checks that CVT.W.S reads fs. New run "CVT.W.S chops toward zero and saturates like PCSX2" checks chop and saturation, including exponent 255 and the in-range extremes 0x4EFFFFFF and 0xCEFFFFFF. This branch does not contain the SQRT.S operand assertion from PR #382, so that assertion was not added.

Found while

Tekken 4 recomp audit after the SQRT.S operand bug (PR #382). Census of SCES-50878 .text 0x180080-0x2A91F8, logs/grok/fpu-audit_20261022_062059.json: cvt.w.s 497, ctc1 0, vclipw 35, cfc2.clip 21, cfc2.mac 4, vftoi0 15, vftoi4 25.

Verified with

cmd /c external\ps2recomp-pr10\build_pr_tests.cmd on branch ee-fpu-vu0-macro-semantics (eb8688b).
Rerun of that ps2x_tests.exe with no capture holding logs/run.lock: 449 tests, 449 passed, 0 failed (logs/grok/fpu-pr-tests2.txt).
CVT.W.S chops toward zero and saturates like PCSX2.
VCLIP side bits and 24-bit history.
FMAC writes the MAC word CFC2 reads.
VFTOI saturates at 2^31.
The first run of the same build (logs/grok/fpu-pr-tests.txt) was 449 tests, 448 passed, 1 failed: ps2xTest/src/ps2_runtime_interrupt_tests.cpp "EE Timer2 compare IRQ wakes a DelayThread-style semaphore wait", a 150 ms wall-clock wait. That file was not edited. That run passed the four new cases. The rerun passed the Timer2 case.

Made by Pablo and assisted with Claude Code and Grok.
