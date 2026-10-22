stack profiler TEKKEN4_STACKPROF=88:12 logs/grok/stack-profiler-3/stdout.txt
6129 samples tid 106376 source hook suspendFail 0 empty 0 depth 8. self-test 6 frames. applied with the other SCES_508.78 overrides.

names: dbghelp export Size is the gap to the next export (SYMFLAG_EXPORT 0x200). ntdll memcpy/memmove/RtlMoveMemory are one address, Size 30816, pdata len 682 begin RVA 0xa7980 (ends 0xa7c2a). NtWait and ZwWriteFile/NtWriteFile pdata len 24, Size 32, names trustworthy. RtlAllocateHeap Size 22080, first pdata len 122. Resolver keeps an export name only when the export starts inside the sampled pdata function; a PDB name is kept when displacement < Size. Leaf ntdll.dll+0xaec40 (13.7% 842/6129) sits inside the fat memcpy range and outside the 682-byte function. Every one of those samples is SleepConditionVariableSRW <- Cnd_wait <- WaitForWorkersUnlocked. Not a copy.

memcpy symbol: 2/6129, module ps2entryrunner.exe, caller PS2Memory::processVIF1Data <- drainMfifo (ps2_memory.cpp:2152) <- tryProcessScratchpadDma (ps2_memory.cpp:1795).
memmove (vcruntime140): 46/6129 (0.8%). 21 writeImageTransfer<19> <- WriteImageTransfer <- UploadImage; 11 processVIF1Data <- drainMfifo <- tryProcessScratchpadDma; 5 PixelStorageTraits<0>::ReadThroughCache <- LoadClutUnlocked <- LoadClut; 5 drainMfifo lambda (ps2_memory.cpp:2043/2081); 1 tryProcessScratchpadDma.
WriteFile/ZwWriteFile/NtWriteFile: absent this window.
RtlAllocateHeap: 1/6129. operator new <- vector<EeIrqHandler>::_Emplace_reallocate <- EeScheduler::dispatchIrq (EeScheduler.cpp:1268).

NtWaitForAlertByThreadId 32.7% 2006/6129. Raw: 27.0% 1656 RtlSleepConditionVariableSRW <- SleepConditionVariableSRW <- Cnd_wait; 5.2% 317 ntdll+0x37e8b <- Mtx_lock <- LoadClut; 0.2% 14 same into BeginTransfer; 0.2% 10 into Submit. User: 22.7% 1390 WaitForWorkersUnlocked <- Submit <- vertexKick; 5.2% 317 LoadClut <- writeRegisterUnlocked <- processGIFPacket; 3.4% 206 WaitForWorkersUnlocked <- UploadImage <- processGIFPacket; 1.1% 69 WaitForWorkersUnlocked <- LoadClut; 0.2% 14 BeginTransfer.
ntdll+0xaec40 user: 12.0% 737 WaitForWorkers <- Submit <- vertexKick; 0.9% 53 UploadImage; 0.8% 52 LoadClut.
WaitForWorkersUnlocked own leaf 2.4% 150, hottest IP gs_cpu_backend.cpp:1335 (m_idleCv.wait). Of that: 1.6% 98 UploadImage, 0.6% 35 Submit<-vertexKick<-processGIFPacket, 0.2% 15 Submit<-vertexKick<-writeRegisterUnlocked.
Exclusive Submit drain (NtWait 1390 + ntdll helper 737 + wait leaf 35+15) = 2177/6129 = 35.5%. Upload drain (206+53+98) = 357/6129 = 5.8%. LoadClut worker-wait (69+52+2) = 123/6129 = 2.0%. Do not add these to the 32.7% leaf.

writeImageTransfer<19> = P8 (PixelStorageMode 0x13). 7.4% 454/6129, all WriteImageTransfer <- UploadImage <- processGIFPacket. Hottest IP ps2_gs_memory.cpp:268 (per-pixel memcpy, bytesPerPixel 1). WriteImageTransfer hottest IP line 308 (P8 case).
PixelStorageTraits<20>::Write = P4 (0x14). 1.7% 107/6129, caller writeImageTransfer<20> at ps2_gs_memory.cpp:250 <- WriteImageTransfer <- UploadImage. Site ps2_gs_memory.h:700.
beginNextReady 5.1% 314/6129 (iop_kernel.cpp:674, two thread-map scans) + nextWakeCycle 2.4% 149 (line 702). Both from runCycles iop_emulator.cpp:984 via checkpointDue EeScheduler.cpp:359, mostly FUN_0022aac0 eeCheckpointDue (FUN_0022aac0.cpp:62). Combined 463/6129 = 7.6%.
processVIF1Data 0.9% + runNative 0.9%. Submit own leaf 1.8% hottest IP gs_cpu_backend.cpp:1119 (MarkPages, not the wait). vertexKick site gs_frontend.cpp:1582 is the Submit call.

fixes (not implemented):
1. gs_cpu_backend.cpp:1335 do not block the EE thread in m_idleCv.wait. Submit from vertexKick is 35.5%; UploadImage drain at 2691 is 5.8%. Submit holds m_mutex from line 1044 across the wait; LoadClut then waits 5.2% (317/6129) at line 1415.
2. ps2_gs_memory.cpp:268 widen the PSMT8 store. 7.4% plus 0.3% vcruntime memmove from the same function. Caller WriteImageTransfer P8 case line 307-309, UploadImage:2714.
3. iop_kernel.cpp:674 and :702 stop scanning every IOP thread on each slice (ready queue + cached next wake). 5.1% + 2.4% = 7.6%, called from iop_emulator.cpp:984.

contra: game-thread-fixes-1 ZwWriteFile 3.4% and fight_profile3 RtlAllocateHeap 3.3% / memcpy 5.9% are other builds. Flat memcpy was the fat export. This window has no WriteFile. One 12s fight slice on the current tree (4 overrides applied).
