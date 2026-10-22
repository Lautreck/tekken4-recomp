Store packet DMA QWC once, in terminate

Problem

The GIF and VIF packet stubs rewrote the open DMA tag's QWC on every pk->cur store. writePacketBuilderCurrent called refreshPacketBuilderPendingCount, which masked bits 0-15 to ((cur - tag) >> 4) - 1 and left bits 16-31 alone. libpkt does not do that. AddGsAD and CloseGifTag only advance cur and, for CloseGifTag, the GIF nloop. The DMA-tag close adds ((cur - tag) >> 4) - 1 to the whole tag word, once, then clears the open-tag pointer.

When a game's own close runs as recompiled code after those stubs, the QWC is counted twice. A one-register packet (QWC 2) becomes 4. A four-register packet (QWC 5) becomes 10. VIF1 then reads past the packet. Issue https://github.com/ran-j/PS2Recomp/issues/374.

Per-file changes

ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp
Deleted refreshPacketBuilderPendingCount. writePacketBuilderCurrent now only stores cur. terminatePacketBuilderState still pads while (cur & 0xC), then, when pk+8 != 0, adds ((cur - tag) >> 4) - 1 into the whole tag word with writeGuestBytes and clears pk+8. tag == 0 skips the add, so a second terminate does not change the word. closePacketGifTag's nloop add and sceVif1PkCloseDirectCode are unchanged.

ps2xTest/src/ps2_memory_tests.cpp
"VIF1 packet builders store chain qwc on terminate" (was "keep chain qwc live before terminate") expects QWC 0 after Cnt + OpenDirect + Reserve + CloseDirect, then 0x10000001 with the CNT id kept after sceVif1PkTerminate, then the same one-qword GIF callback.
"stub AddGsAD plus a game-side ADD close stores qwc once": VIF AddGsAD + CloseGifTag leave 0x10000000, and a game-side ADD close stores 0x10000002 (GIF nloop 1). Four GIF AddGsAD + CloseGifTag leave 0x10000000, and the game-side close stores 0x10000005 (GIF nloop 4). The same VIF stubs closed with sceVif1PkTerminate store 0x10000002, return cur, clear pk+8, and a second terminate stays at 0x10000002.
"VIF1 Cnt closed before data keeps qwc at zero": sceVif1PkCnt leaves cur at tag+8 and word 0x10000000. Terminate pads two words to tag+16 before the add, so the add is 0 and the word stays 0x10000000. It does not become 0x0FFFFFFF. A second terminate is unchanged.
"terminate addu wraps an empty open tag to 0x0FFFFFFF": cur still on the tag (no pad) adds -1 through the whole word, 0x10000000 becomes 0x0FFFFFFF, and a second terminate does not add again. That is the ELF addu. The old masked SET left this word at 0x10000000 because cur == tag returned early.
"terminate addu adds to a preloaded qwc": a tag opened as 0x10000003 with one qword past it becomes 0x10000004. The old SET would have replaced the low half with 1.

Found while

Tekken 4 recomp. The game's own terminate runs after stubbed AddGsAD and CloseGifTag and adds a QWC the stubs had already set.

Verified with

cmd /c external\ps2recomp-pr6\build_pr_tests.cmd on branch qwc-close-once (eb8688b). 449 tests, 449 passed, 0 failed.
VIF1 packet builders store chain qwc on terminate.
stub AddGsAD plus a game-side ADD close stores qwc once.
VIF1 Cnt closed before data keeps qwc at zero.
terminate addu wraps an empty open tag to 0x0FFFFFFF.
terminate addu adds to a preloaded qwc.
sceGifPkRefLoadImage seeds A+D GIFtag nloop once (no double-count).
sceVif1PkCloseDirectCode encodes DIRECT length in qwords.

Made by Pablo and assisted with Claude Code.
