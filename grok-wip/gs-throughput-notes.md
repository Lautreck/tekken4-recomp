# GS throughput

Baseline logs/fight_profile3 stdout.txt (6 workers). Menu sec 1-9: 60 fps. Fight:
- sec 85-89: 19-21 fps, GS 591-745 ms. drains sample 227-617 write 200-342 upload 230-264 clut 17-20 other 15-19 (stdout 310-319).
- sec 92-98 steady: 20 fps, GS 548-571 ms. sample 115-149 write 77-101 upload 303-450 clut 19-20 other 15-20 (stdout 324-337).
User window sample 227-788 / write 200-376 / upload 239-277 is sec 85-91, not the steady tail.

## Step 1a — tight pages + 48-slot CLUT that waits when full (logs/grok/gs-throughput-1, exe 02:17, DRAIN_STATS=2, PERF_LOG=120)

Fight sec 85-92 stdout 1354-1453:
- fps 20,19,20,19,16,17,16,17. GS ms 711,694,728,691,659,656,661,647.
- other 364-403 (was ~17). sample ~49-60 (was ~120-600). write ~33-40 (was ~80-340). upload 216-315 (was ~230-450). clut drains 0 (was ~18).
- saved sample 8k-23k, saved write 10k-17k, saved upload ~0-22. texfull 151k-448k, textight 79k-131k. clutload ~17k-21k, clutskip ~61k-85k.
- Menu sec 1-9 still 60 fps. texfull 0. uploads ~9180 still real (saved upload 60). sample drains fell to ~2.

Cause of the fps drop: clutload ~20k/s and the pool never recycled a slot until a full Wait, so Other ~= 20000/48. Tight rects did cut sample/write/upload waits; the Other regression hid it. Logged full=1 samples are tristrip/trifan with |Q|~7e-5 (below the old 1e-4 cutoff) and sane S/Q. A sibling write of fbp=475 over the same tbp=15200 buffer is a real RTT and stays.

Shots shot_40s.png and shot_95s.png are both 6185 bytes (PrintWindow white). Same size as logs/grok/game-thread-fixes-2/shot_40s.png. Not pixel evidence.

## Step 1b — CLUT slot recycle + Q ratio <=512 (exe 02:23:54). logs/grok/gs-throughput-2 is INVALID for fps.

That run (02:32:33-02:34, 95.2s, CAPTURE_EXIT 0) overlapped a full compile of out\grok-game-thread-fixes (134 files written 02:32:01-02:34:14, their exe linked 02:34:14). Menu sec 3-4: 43 and 37 fps, GS 1047 and 963 ms, prims 5798/5280, upload drains 5049/4590 (stdout 154-169). Same menu in gs-throughput-1 sec 2: 60 fps, GS 307 ms, prims 10560, upload 9180. GS ms is game-thread time inside processGIFPacket (tekken4_overrides.cpp:1186), so a busy machine stretches it.

Classification still readable, not a fps claim. Sec 85 (stdout 1355): 9 fps, GS 882, prims 75269, other 54, sample 21, write 10, upload 111, clut 0, texfull 68, textight 70426, clutload 5208, clutskip 15617. Versus gs-throughput-1 sec 85: 20 fps, other 364, sample 57, texfull 211529, clutload 19250. texfull collapsed (the |Q|~1e-4 cutoff was the whole-texture marks). other fell from ~380 to ~50 but the fight had not reached the 600k-prim steady state before the 95s cut (prims still climbing, sec 88 = 22 fps then close). Remeasure idle.

Block-wise UploadImage (WriteImageTransfer, same swizzle as Write*) was compiled by a grok-dir build that finished 02:42:45 (exe length 48436224). That exe is step 1b + block writes. Not captured idle yet: game-thread-fixes-4 and then sound-run-5 took the lock. Next idle run is logs/grok/gs-throughput-3 of THIS exe (do not rebuild first).

## Idle capture of that exe: logs/grok/gs-throughput-3 (DRAIN_STATS=2, no compile overlap; menu sec 1-15 is 60 fps, GS 285-299 ms, stdout 132-328)

Fight, same prim ballpark as gs-throughput-1 (~600k):
- sec 90 stdout 1431: 18 fps, GS 695, prims 612225, other 164, sample 51, write 34, upload 237, clut 0, texfull 68, textight 590400, clutload 21433, clutskip 84828
- sec 91 stdout 1445: 17 fps, GS 679, prims 611481, other 146, sample 51, write 34, upload 241, texfull 70, clutload 21527, clutskip 85699
- sec 92 stdout 1459: 17 fps, GS 679, prims 588405, other 149, sample 50, write 33, upload 261, texfull 68, clutload 20235, clutskip 81766
Versus gs-throughput-1 sec 90-92: fps 17/16/17, GS 656/661/647, other 384/382/385, sample ~50, write 33, upload 234-315, texfull ~430k, clutload ~20k.
Versus fight_profile3 sec 92-98 (STATS=1): 20 fps, GS 548-571, other ~17, sample ~120, write ~80, upload 303-450, clut ~20.

What it bought: Q-ratio cut texfull ~440k to ~70 (the logged tristrips now say full=0). CLUT recycle cut other ~380 to ~150, not to the present-only ~17. 21k loads/s still overflow a 48-slot pool ~150 times/s. fps 16-17 to 17-18. Block writes did not move upload drain counts (they are real read hits: dbp=15200 dpsm 0x13/0x14 hit=read) and GS ms did not fall. Remaining sample/write are real: sample boxes on the display vs tbp 15200, and a sprite that draws the whole CT16 buffer at fbp 475.

Shots are PrintWindow 6185-byte white frames again, same as gs-throughput-1. Not pixel evidence.

## Step 3 bundle, idle fight (logs/grok/gs-throughput-4, exe 02:52:04, DRAIN_STATS=2). p15check test link finished 02:53:52, which is perf second ~70, so sec 85-93 are after it. Menu sec 1-15 still 60 fps, GS 248-271 (stdout 132-329), a bit under throughput-3's ~290.

Fight sec 90-92 stdout 1431-1459:
- 21/21/21 fps, GS 604/594/589, prims 714476/756877/733817
- other 16/17/15, sample 63/63/66, write 42/42/44, upload 368/410/460, clut 17/16/16, texfull 84/84/88
- clutload ~25-27k, clutskip ~101-105k

Versus throughput-3 sec 90-92 (17-18 fps, GS ~679-695, other ~150, sample ~51, write ~34, upload ~240, clut 0, texfull ~70).
other back to the present-only ~16. The 48-slot pool was the extra ~130 Other waits; 512 slots absorb the in-flight loads. clut drains returned to ~16/s (real queued write into the palette), which the pool-exhaustion waits had been covering. fps 17-18 to 21-22, GS 679 to ~590. Span/SamplePoint/spin are in the same binary, so the fps delta is the sum. Per frame, sample and write stayed ~3 and ~2 (tight pages); the fps came from dropping ~8 other waits per frame plus faster workers.

60 fps is not in reach on the GS path alone. At 22 fps the frame is ~45 ms. GS (processGIFPacket, includes WaitForWorkers) is ~27 ms, so the rest of the game thread is ~18 ms. A 16.7 ms frame cannot fit 18 ms of non-GS work (VIF, memcpy, heap) that this task does not own.

## Step 3 headline, same exe, DRAIN_STATS=1 (logs/grok/gs-throughput-5)

params.txt: started 2026-10-22 02:55:21, commit 3d16de9, PS2X_GS_DRAIN_STATS=1, TEKKEN4_BUILD_DIR=grok, TEKKEN4_PERF_LOG=120. No TEKKEN4_PROFILE. CAPTURE_EXIT 0, ran 95.2 s. Exe 02:52:04 length 48935936 (cache CMAKE_CXX_FLAGS_RELWITHDEBINFO=/Zi /O2 /Ob2 /DNDEBUG). No *.obj/*.exe/*.pdb/*.ninja_log under external/PS2Recomp/out or external/ps2recomp-p15check/out written 02:56:20-02:57:20 (fight sec ~90).

Menu stdout 95-113: sec 1-10 are 60 fps, GS 252,261,257,266,257,255,255,259,253,255. Sec 15 stdout 123: 60 fps, GS 254.

Fight stdout 326-333 (steady, prims ~760k):
- sec 90: 22 fps, GS 586, prims 759562, other 15, sample 65, write 43, upload 454, clut 20, full 0
- sec 91: 22 fps, GS 586, prims 760320, other 15, sample 65, write 43, upload 431, clut 18
- sec 92: 22 fps, GS 585, prims 761867, other 16, sample 65, write 43, upload 413, clut 19
- sec 93: 22 fps, GS 586, prims 760920, other 17, sample 66, write 44, upload 352, clut 16
Sec 86-87 (stdout 318-321) already 22 fps once prims hit ~756k-769k, GS 596/588.

Versus fight_profile3 sec 92-98 (stdout 324-337, STATS=1, re-read): 20 fps, GS 548-571, sample 115-149, write 77-101, upload 303-450, clut 19-20, other 15-20, prims ~669k-701k.
Versus the user window (stdout 310-321, sec 85-91): sample 227-1009, write 200-376, upload 230-338, fps 15-21. Sec 90 there is 15 fps, sample 788, write 376.
Versus throughput-3 (48 slots, STATS=2): 17-18 fps, GS ~679-695, other ~150. Versus throughput-4 (same binary, STATS=2): 21 fps, GS ~589-604, other ~16, sample ~63, write ~42. STATS=1 and STATS=2 land on the same 21-22 fps, so the level-2 wide-page mark is not the limiter.

Per frame at 22 fps: sample ~3, write ~2, upload ~16-20, clut ~0.9, other ~0.7. Sample and write are about half the steady baseline. Upload, clut and other are the real render-to-texture / palette / present waits and did not fall. The +4 fps from throughput-3 to here is mostly the 512-slot CLUT pool (other ~150 to ~16). Span, SamplePoint and idle-spin 512 are in the same binary; the drain counts do not separate them from the slot bump.

Shots: gs-throughput-1/3/4/5 shot_40s.png and shot_95s.png are each 6185 bytes. shot_95s.png of throughput-5 is a white PrintWindow client (title bar "PS2-Recomp | SCES_508.78" only). Not pixel evidence. Menu stayed 60 fps on every measured build.

Tests: out/grok/ps2xTest/ps2x_tests.exe, cwd external/PS2Recomp, built by configuring that grok tree with PS2X_BUILD_TEST/RECOMP/ANALYZER=ON (build_tests.cmd not used; it writes out/test). logs/grok/gs-tests-run4.txt: Passed 470, Failed 0, exit 0. Includes "drawing threads leave the same GS memory", "drawing threads keep queued primitives on their texture and CLUT", "UV-tight page marks" (sentinel moved from (900,400) to (900,420): with TBW 10, (x,y) aliases (x-640,y+32), and (260,432) is inside the test's own sprites so the old expected blue was already red before the upload), "a draw into the CLUT buffer", "triangle coverage matches the per-pixel barycentric loop". Runner exe was not relinked (still 02:52:04, length 48935936).

## Step 4 — swizzle address + texel reuse + texture-page cache kept (logs/grok/gs-throughput-6)

Address math is shifts into the small block/column tables (SwizzlePixel) instead of the page-sized LUT. Frame/Z reads and writes call that directly. Texture samples go through ReadThroughCache (8 KiB page copy until TEXFLUSH). A first test build that skipped the cache failed "GS texture page buffer hides local-memory writes until TEXFLUSH"; the cache was put back on the sample path only. Point and bilinear samples reuse the last texel when the integer UV (or the bilinear cell, including a +1 U shift) matches. Present dump: PS2X_GS_FRAME_DUMP writes frame_40.bmp and frame_95.bmp (24-bit).

Tests: cmd /c logs/grok/build_gs_tests.cmd then logs/grok/run_gs_tests.cmd (cwd external/PS2Recomp, exe out/grok/ps2xTest/ps2x_tests.exe). logs/grok/gs-tests-run5.txt: Total 472, Passed 471, Failed 1. Passed: "swizzle pixel index matches the page lookup table", "magnified sprite samples match per-texel colours", "GS texture page buffer hides local-memory writes until TEXFLUSH", "drawing threads leave the same GS memory", "drawing threads keep queued primitives on their texture and CLUT", "triangle coverage matches the per-pixel barycentric loop". The one failure is "IOP heap DMA uses private backing" (ps2xIOP sources are dirty from the other task; not a GS file).

Capture: logs/grok/gs-throughput-6, params started 2026-10-22 05:58:25, commit 9ba7cfa, DRAIN_STATS=1, PERF_LOG=120, FRAME_DUMP=., shots 40,95,105 (105 only so the process is still alive at present-time 95). Exe 05:57:28 length 49015296. CAPTURE ran 105.2 s. No compile during the run.

Menu stdout 95-113: sec 1-10 are 60 fps, GS 267,272,271,272,266,269,268,266,272,268.

Fight stdout 326-333 (prims ~829k, same ~34540 prims/frame as throughput-5's ~760k/22):
- sec 90: 24 fps, GS 554, prims 829094, other 24, sample 72, write 48, upload 320, clut 24
- sec 91: 24 fps, GS 558, prims 828930, other 20, sample 72, write 48, upload 299, clut 24
- sec 92: 24 fps, GS 564, prims 828039, other 22, sample 72, write 48, upload 289, clut 24
- sec 93: 24 fps, GS 564, prims 828987, other 23, sample 72, write 48, upload 288, clut 24
Sec 85-89 already 23-24 fps, GS 562-546. Sec 94-99 stay 23-24 fps, GS 552-561.

Versus throughput-5 sec 90-93 (stdout 326-333, re-read): 22 fps, GS 586/586/585/586, sample 65/65/65/66, write 43/43/43/44, upload 454/431/413/352, clut 20/18/19/16, other 15/15/16/17, prims ~760k.
What it bought: +2 fps (22 to 24), GS ~586 to ~554-564. Sample and write per frame stayed ~3 and ~2 (72/24 and 48/24 vs 65/22 and 43/22). Upload drains fell ~350-450 to ~290-320. The fps is faster workers, not fewer waits. Swizzle and texel reuse are one binary, so the +2 is the sum. The cache skip was rejected by the TEXFLUSH test and is not in this exe.

At 24 fps the frame is 41.7 ms. GS ~556/24 = 23.2 ms, so the rest of the game thread is ~18.5 ms, the same ~18 ms as throughput-5 (586/22 = 26.6 GS, 45.5-26.6 = 18.9). A 16.7 ms frame cannot fit 18.5 ms of non-GS work. Ceiling if GS wait were zero is ~54 fps (1000/18.5).

Contra: this exe also contains runtime/tekken4_overrides.cpp (mtime 04:38:49) and runtime/tekken4_camera_overrides.cpp (mtime 04:02:18). throughput-5's exe was 02:52. Prims/frame and non-GS ms/frame match, which is why the GS drop is attributed to the GS edit, but it is not a one-variable rebuild.

Shots shot_40/95/105.png are 6185-byte PrintWindow whites. frame_40.bmp and frame_95.bmp are 860214 bytes (640x448 24-bit). frame_40 is the MODE SELECT menu (ARCADE, Kazuya/Paul portraits, readable text). frame_95 is the Aqua Garden fight (Kazuya vs Law, timer 48, HUD readable). No earlier present dump exists, so there is no byte compare. The fight dump moves with fps. Camera is still too close (arm fills the left). Not fixed here.

## Step 5 — snapshot read-only upload pages (no wait)

Upload drains (~300/s at 24 fps) are read hits: queued primitives sample the destination, they do not draw into it. Those uploads now copy the 8 KiB pages onto a chain tagged with the job epoch and write live VRAM under a per-page seqlock. Write hits still wait. Snapshots are dropped on the next drain (present Sync is one). Stats line gains `shadow`. overrides.cpp mtime 07:06:17 is newer than the step-4 exe, so fight numbers are not a one-variable compare against throughput-6.

### throughput-7 (same keys, shots 40,95; exe 07:26:43 len 49292288; DRAIN_STATS=1 PERF_LOG=120; ran 95.5s)
Killed at perf 88, before frameshot 95 (only frame_40s.bmp). Menu sec 1-10: 60 fps, GS 226-305. Select sec 37-40 (stdout 195-205): ~40 fps, GS ~700, prims ~16-18k, upload ~2200, shadow 0. From sec 47 the prims jump to ~200k and stay ~240-270k through sec 88 at 24-27 fps, GS 595-637, upload ~185-226, shadow ~1100-1300, sample ~70-80, write ~66-78, clut ~0, other ~25. Not the throughput-6 829k window (that ramp is sec 80-83 of throughput-6). frame_40s = character select, Kazuya, timer 27.

### throughput-8 (shots 40,95,130; same exe; params 07:37:09 commit 5082506; ran 139s)
Menu sec 1-10 stdout 95-113: 60 fps, GS 231,259,266,277,272,227,224,296,272,244.
Character select sec 39-46 stdout 202-218: 33-38 fps, GS 719-752, prims ~14.5-16.5k, upload ~2100-2360, shadow 0, sample 0, write ~34-39.
Did not leave that screen until sec 92-93 (stdout 330-333): sec 92 is 18 fps GS 217 prims 19182; sec 93 is 30 fps GS 569 prims 290408 shadow 9564. Then sec 100-119 (stdout 347-386): 31-36 fps, GS 560-620, prims 268k-333k, upload 174-216, shadow 9164-11376, sample ~30-36, write ~30-36, other ~43-61, clut 0.
frame_40s.bmp = character select Kazuya timer 28. Diff vs throughput-7 frame_40s: pct_diff 25.597, mean_abs 9.327, psnr 22.24 (timer digit + the select backdrop animates; portrait, grid, KAZUYA plate match).
frame_95s.bmp = COMMAND LIST tutorial ("Press START to close this instruction"), not the fight. throughput-6 frame_95.bmp (old FRAME_DUMP, no s) is Aqua Garden Kazuya vs Law timer 48. The 270-330k window is that tutorial's 3D backdrop. Same keys never pressed Start again, so the fight did not start. throughput-9 adds 100:Start and PERF_LOG=180.

### throughput-9 (same exe as throughput-8, keys plus 100:Start, shots 40,95,160, PERF_LOG=180, ran 160.2s)
Menu sec 1-5 stdout 95-103: 60 fps, GS 222,228,240,232,242. shadow ~9240, upload 240 (same menu shape as throughput-7/8).
Fight DID start. frame_95s.bmp is Kazuya vs Kazuya, timer 00, night rooftop (not Aqua Garden, not the command list). HUD and both fighters readable. frame_160s.bmp was not written: script killed at 160.2s, frameshot clock had not reached 160 (stdout has only frame_40s and frame_95s).
Round start before the extra Start, stdout 335-345:
- sec 94: 21 fps, GS 643, prims 283338, other 20, sample 36, write 42, upload 173, clut 11, shadow 1262
- sec 95: 18 fps, GS 666, prims 278189, upload 180, shadow 666
- sec 96: 18 fps, GS 654, prims 280214, upload 190, shadow 702
- sec 97: 19 fps, GS 654, prims 286935, upload 180, shadow 667
- sec 98: 17 fps, GS 686, prims 271923, upload 180, shadow 666
- sec 99: 19 fps, GS 647, prims 267762, upload 185, shadow 702
Start landed at 100.7s, after the round had already begun, so it paused. Sec 127-146 stay in the same band (19-22 fps, GS 619-675, prims 275k-323k, upload 190-220, shadow 667-813, sample ~40, write ~50, clut ~7-14, other ~20). Not throughput-6's 829k Aqua Garden.
Versus throughput-6 at ~260-320k prims (stdout 296-310): sec 75 is 23 fps GS 713 upload 308 prims 258700; sec 81 is 25 fps GS 640 upload 287 prims 317028. Step 5 here: upload waits ~300 to ~180, shadow ~700, GS ~640-713 to ~643-686, fps 23-25 to 17-21. fps did not rise. Contra: different stage and characters, timer still 00, Start then paused, not a matched Aqua Garden second.

## Step 6 — drop finished jobs from the page counts; skip a write-hit upload whose pixel box misses

Page masks were bits set until the next drain, so a finished draw still forced later uploads to wait. Counts increment per queued job and decrement once every worker has drawn it. A remaining write hit whose pixel box misses the upload (same DBW/PSM, X inside the buffer width) does not wait. X past DBW*64 aliases to the next page row, so that case keeps the page wait. A fake write hit that is still a queued sample snapshots, it does not skip the snapshot. Stats gain `box`.
Tests: logs/grok/gs-tests-run7.txt after the alias fix: Total 478, Passed 478, Failed 0. Includes the new "upload beside queued sprites" test and "UV-tight page marks match serial when the whole texture covers the frame" (that one failed on the first cut, which skipped the snapshot; the second cut passes).

Game exe: external/PS2Recomp/out/grok-gs-throughput/ps2xRuntime/ps2EntryRunner.exe 2026-10-22 07:59:53 length 49303040. Capture logs/grok/gs-throughput-10 (no extra Start, shots 40,95,175, DRAIN_STATS=1, PERF_LOG=200, ran 175.2s, exit 0). frame_40s.bmp is MODE SELECT (not character select). frame_95s.bmp is Aqua Garden, Kazuya vs Law, timer 48, Law on the ground (same stage and timer as gs-throughput-6 frame_95.bmp, different pose). frame_175s.bmp was not written (last perf second 172; wall 175 closed the process first).

Menu sec 1-9 stdout 96-113: 60 fps, GS 207-218. Steady fight sec 120-160 stdout 389-469: 25-27 fps, GS 627-658. Example sec 150: 27 fps, GS 650, prims 505876, other 27, sample 70, write 54, upload 252, clut 0, shadow 165, box 0. Sec 90-93 (around the frameshot): 26-27 fps, GS 728-736, prims 258k-299k, upload 233-250, shadow ~1030, box 0. box is 0 on every second. bq/ba/bb/bo were not printed yet.

Versus gs-throughput-6 sec 90-93 (24 fps, GS 554-564, prims ~829k, upload 288-320): this run's steady window is ~480k prims not ~829k, and the pose at timer 48 is not the same frame, so the fps delta is not a GS win. Per frame at sec 150: GS 650/27 = 24.1 ms, upload 252/27 = 9.3. t6 sec 90: GS 554/24 = 23.1 ms, upload 320/24 = 13.3, at 829094/24 = 34k prims/frame versus 505876/27 = 19k here. box never fired: UploadMissesInFlightPixels returned false when in-flight jobs exceeded 256, and drains imply ~1200 jobs between waits (505876 prims / (27+70+54+252) drains).

## Step 6b — scan up to 8192 in-flight jobs with a page bitset

Same pixel test (buffer identity, disjoint box, X inside DBW*64, 4-bit pad). The 256 cap is 8192. Each job stores the write pages MarkPages set, as two 512-bit sets, so the scan does not walk rectangles. A wrapped box rejects the skip only for a job that shares a page. Stats add bq (queue over the cap), ba (X alias), bb (different buffer), bo (same buffer, boxes meet). Tests logs/grok/gs-tests-run8.txt: Total 478 Passed 478 Failed 0. Exe 2026-10-22 08:15:34 length 49304064.

logs/grok/gs-throughput-11 (shots 40,95,140, ran ~140s). Menu sec 1-9: 60 fps, GS 205-220. Steady fight sec 120-137: 26-27 fps, GS 637-654, prims ~470-488k, upload 234-247, sample ~50-70, write ~52, shadow 135-1120, box 0, bq 0, ba 0, bb 0, bo 0 (stdout 389-424). Same band as throughput-10. The scan never ran: those upload waits are write-page hits on calls that store no pixels (canWrite is false, so UploadMisses is not called and no fail counter increments). Bought nothing.

## Step 6c — do not drain on an UploadImage that stores no pixels

Early return before the page check. Counter nw. Tests logs/grok/gs-tests-run9.txt: 478/478.

logs/grok/gs-throughput-12 (shots 40,95,140). frame_95s.bmp is Aqua Garden Kazuya vs Law, timer 48, Law down (same scene as throughput-10/11). Menu sec 1-9 stdout 96-112: 60 fps, GS 163-171 (was 205-220), upload 0 (was 240), shadow ~9480, nw ~900. Steady fight sec 110-138 stdout 369-425: 26-27 fps, GS 634-656, prims ~450-494k, upload 0 (was ~234), sample ~35-70, write ~52-54, clut ~26-54, shadow ~350-1400, nw ~700-750, box 0. Upload waits are gone. Fight fps and GS ms did not move: those waits were not the raster. Menu GS fell ~40 ms/s under an already-60 cap.

## Step 7 — skip the texture sample when the depth test already rejects the pixel

DepthRejects uses the same ZTST comparison as WritePixel (never / always / gequal / greater). A reject stores nothing, which is what WritePixel does on a failed depth test. Passing pixels still go through WritePixel.

logs/grok/gs-throughput-13 (reject, second depth read still inside WritePixel). Menu sec 1-9: 60 fps, GS 172-182. Steady fight sec 110-129 stdout 369-407: 28-30 fps, GS 606-628, prims ~482-534k, upload 0. frame_95s.bmp Aqua Garden, Kazuya vs Law, timer 47, both fighters readable.

## Step 7b — do not read depth again on a pixel that already passed

WritePixel(zAlreadyPassed=true) skips the second Z read and still writes Z when the mask says so. Tests logs/grok/gs-tests-run10.txt and gs-tests-run11.txt: 478/478.

logs/grok/gs-throughput-14. Menu sec 1-9 stdout 96-112: 60-61 fps, GS 165-177. frame_40s.bmp is MODE SELECT, text and portraits readable. Steady fight sec 110-129 stdout 369-407: 28-30 fps, GS 604-627. Sec 120: 29 fps, GS 609, prims 545479, other 30, sample 66, write 57, upload 0, clut 30, shadow 358, nw 810. frame_95s.bmp Aqua Garden, Kazuya vs Law, timer 46, both fighters and the HUD readable. Versus throughput-12 sec 120 (27 fps, GS 636, prims 487234): about +2 to +3 fps and GS ~640 -> ~610 at the same ~18k prims/frame. The second-read removal did not move past throughput-13.

60 fps is not reached. At 30 fps, GS is ~20 ms/frame and the rest of the frame is ~13 ms, so the ceiling if the GS wait were free is ~75 fps on this pose. The remaining GS time is pixels that pass the depth test (SampleTexture + WritePixel). The 829k-prim close-up in gs-throughput-6 (24 fps, GS 554) is a heavier pose than this ~500k window and is not the same comparison.
