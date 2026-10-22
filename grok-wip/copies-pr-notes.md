Pass GIF packets through on an empty arbiter queue and memcpy 32-bit UNPACK

Problem

GifArbiter::submit copies every packet into a heap vector before drain() sorts it. An empty queue has nothing to arbitrate, so that copy is pure overhead. submitGifPacket already drains before another path can queue, and a fight profile spends real time in that copy.

VIF1 UNPACK of 32-bit data with the mask off, MODE 0 or 3, and CL >= WL writes one source vector per destination and adds no ROW. V4-32 with CL == WL is one contiguous VU span (wrapping once at vector 1024). Narrower 32-bit formats write 16, 12, 8, or a broadcast 4-byte scalar and leave the unread lanes. The per-lane loop is the other hot copy in the same profile.

Per-file changes

ps2xRuntime/include/runtime/gs/ps2_gif_arbiter.h
ps2xRuntime/src/lib/gs/ps2_gif_arbiter.cpp
submit takes drainImmediately, default false. When it is true and the queue is empty, the GS callback is invoked on the caller's pointer and the packet is not stored. Any other submit still copies, so a later drain() sorts the batch as before.

ps2xRuntime/src/lib/ps2_memory.cpp
submitGifPacket forwards the drainImmediately flag it already had. flushMaskedPath3Packets still submits with the default (queue, do not pass the flag) and drains the masked PATH3 batch once.

ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp
Before the UNPACK loop: if VL is 32-bit, the mask bit is off, MODE is 0 or 3, and CL >= WL, write the vectors directly. V4 with CL == WL is memcpy, split once on the 1024-vector wrap. S-32 broadcasts the scalar. V3-32 and V2-32 memcpy 12 or 8 bytes and keep the unread lanes. Masks, MODE 1 and 2, VL other than 32-bit, and WL > CL stay on the per-lane loop.

ps2xTest/src/ps2_memory_tests.cpp
"UNPACK 32-bit fast path matches the slow per-lane write": V4-32 contiguous, VU wrap, NUM=0 (256 vectors), CL > WL skip, TOPS, V3-32, V2-32, S-32, and MODE 3. Whole VU memory and ROW match a per-lane reference, with literal words for the wrap, the preserved lanes, and the missing ROW add.
"UNPACK fast path stays off for masks STMOD narrow formats and WL greater than CL": masked V4-32 (data/row/col/protect), MODE 1 offset, MODE 2 difference (ROW updates), WL > CL row fill, V4-16 sign-extend, masked S-32. A memcpy would fail the protected lane, the ROW add, and the sign-extend.
"GIF immediate drain delivers the same bytes as a queued drain": empty-queue submit passes the caller pointer; a queued submit still copies, so editing the caller buffer before drain does not change the GS bytes; a non-empty queue is not bypassed (PATH1 stays ahead of a later PATH3); submitGifPacket and a masked PATH3 flush deliver the same bytes in order.

Not ported

MFIFO fromSPR span memcpy, mfifoContigQwords, and the reused drainMfifo buffer. eb8688b has no MFIFO ring (that is the other patch stack).
EE timer batching. It broke the Timer2 compare and DelayThread tests and was reverted to an inert scaffold. The scaffold is omitted.
TEKKEN4_GT_STATS counters. Omitted.

Found while

Tekken 4 recomp fight profile. On the patched tree, 8379124 of 8388608 GIF submits had an empty queue, and processVIF1Data dropped from 6.0% to 2.2% of samples once the 32-bit UNPACK memcpy was in (logs/grok/game-thread-fixes-notes.md).

Verified with

cmd /c external\ps2recomp-pr7\build_pr_tests.cmd on branch game-thread-copies (eb8688b). 448 tests, 448 passed, 0 failed.
UNPACK 32-bit fast path matches the slow per-lane write.
UNPACK fast path stays off for masks STMOD narrow formats and WL greater than CL.
GIF immediate drain delivers the same bytes as a queued drain.
VIF DIRECTHL stalls behind queued PATH3 IMAGE packets.
GIF arbiter prioritizes PATH1 then PATH2 then PATH3.

Made by Pablo and assisted with Claude Code and Grok.
