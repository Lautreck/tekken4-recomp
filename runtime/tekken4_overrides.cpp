// Tekken 4 (SCES-50878, PAL v2.00) overrides for PS2Recomp's ps2xRuntime.
// tools\stage_generated.cmd copies this file into ps2xRuntime/src/runner next to the generated code.
// Addresses are for this ELF only (matched by name, entry point and CRC32 below).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include "Stubs/CD.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <string>

namespace tekken4
{
    // Namco's sound driver runs on the IOP and serves SIF RPC id 0x12345. Its EE library (0x2230d0-0x224140)
    // binds one client (0x1cccbc0) and only makes synchronous calls: 4 words in at 0x1ccc700 (the per-frame
    // call 0x100 sends a 0x80-byte command queue at 0x1ccc7c0 instead), 8 words back at 0x1ccc780.
    // FUN_002230d0 retries the bind every vsync until client.server != 0, so without a server boot stops there.
    // Until the real driver runs on ps2xIOP, every call is answered at once with a zeroed reply: no sound.
    constexpr uint32_t kSoundSid = 0x12345u;
    constexpr uint32_t kSceSifBindRpc = 0x00282808u;
    constexpr uint32_t kSceSifCallRpc = 0x002829d8u;
    constexpr uint32_t kSceSifCheckStatRpc = 0x00282bc8u;

    // t_SifRpcClientData: 16-byte header, then command, buf, cbuf, end_function, end_param, server.
    constexpr uint32_t kClientBodyOffset = 0x10u;
    constexpr uint32_t kClientBodySize = 0x18u;
    constexpr uint32_t kClientServerOffset = 0x24u;
    constexpr uint32_t kMaxReplySize = 0x1000u;
    constexpr uint32_t kSoundFrameCall = 0x100u; // FUN_002232a0, once per pass of the main loop (FUN_001969f8)

    std::mutex g_mutex;
    std::set<uint32_t> g_soundClients;
    std::set<uint32_t> g_loggedCalls;
    uint64_t g_frames = 0u;
    uint64_t g_nextFrameLog = 1u;
    std::chrono::steady_clock::time_point g_firstFrame;
    uint64_t g_gsDumpFrame = 0u; // TEKKEN4_GS_DUMP=<frame>, 0 = off

    // While the GS history records, every GIF packet the arbiter hands to the GS is also written out raw
    // (quadwords as "hi lo", the GIFtag first) to gif_packets_frame<N>.txt.
    std::mutex g_gifLogMutex;
    std::ofstream g_gifLog;

    void processGifPacketLogged(PS2Runtime &runtime, const uint8_t *data, uint32_t size)
    {
        {
            std::lock_guard<std::mutex> lock(g_gifLogMutex);
            if (g_gifLog.is_open())
            {
                g_gifLog << "packet " << std::dec << size << " bytes\n" << std::hex << std::setfill('0');
                for (uint32_t off = 0; off + 16u <= size && off < 0x800u; off += 16u)
                {
                    uint64_t lo = 0, hi = 0;
                    std::memcpy(&lo, data + off, 8);
                    std::memcpy(&hi, data + off + 8, 8);
                    g_gifLog << "  " << std::setw(4) << off << ": " << std::setw(16) << hi << " " << std::setw(16) << lo
                             << "\n";
                }
                g_gifLog << std::setfill(' ');
            }
        }
        runtime.gs().processGIFPacket(data, size);
    }

    // Debug aid: write the GS's last 512 events (register writes, draws, image transfers) and the display
    // registers to gs_dump_frame<N>.txt in the working directory.
    void dumpGs(PS2Runtime &runtime, uint64_t frame)
    {
        std::ofstream f("gs_dump_frame" + std::to_string(frame) + ".txt");
        const GSRegisters &p = runtime.memory().gs();
        f << std::hex << "PMODE " << p.pmode << " SMODE1 " << p.smode1 << " SMODE2 " << p.smode2 << "\nDISPFB1 "
          << p.dispfb1 << " DISPLAY1 " << p.display1 << "\nDISPFB2 " << p.dispfb2 << " DISPLAY2 " << p.display2
          << "\nBGCOLOR " << p.bgcolor << "\n";
        const GSDebugSnapshot s = runtime.gs().getDebugSnapshot();
        for (int c = 0; c < 2; ++c)
        {
            const GSContext &x = s.ctx[c];
            f << "ctx" << c << " FRAME fbp " << x.frame.fbp << " fbw " << x.frame.fbw << " psm " << +x.frame.psm
              << " | TEX0 tbp0 " << x.tex0.tbp0 << " tbw " << +x.tex0.tbw << " psm " << +x.tex0.psm << " tw "
              << +x.tex0.tw << " th " << +x.tex0.th << " cbp " << x.tex0.cbp << " cpsm " << +x.tex0.cpsm
              << " | XYOFFSET/scissor see history | ALPHA " << x.alpha << " TEST " << x.test << "\n";
        }
        f << "TEXA ta0 " << +s.texa.ta0 << " aem " << s.texa.aem << " ta1 " << +s.texa.ta1 << "\n";
        const ps2_stubs::CdDebugSnapshot cd = ps2_stubs::getCdDebugSnapshot();
        f << std::dec << "CD initialized " << cd.initialized << " lastError " << cd.lastError << " nextPseudoLbn "
          << cd.nextPseudoLbn << " root " << cd.cdRoot.string() << " files " << cd.files.size() << "\n";
        for (const ps2_stubs::CdDebugFileEntry &file : cd.files)
            f << "  " << file.key << " lbn " << file.baseLbn << " sectors " << file.sectors << " size " << file.sizeBytes
              << "\n";
        f << "\n";
        for (const GSDebugHistoryEntry &e : runtime.gs().getDebugHistory())
        {
            f << std::dec << e.seq << " f" << e.frameIndex << std::hex << " ";
            switch (e.kind)
            {
            case GSDebugEventKind::GifTag:
                f << "GIFTAG size " << e.gifSizeBytes << " nloop " << e.gifNloop << " flg " << +e.gifFlg << " nreg "
                  << +e.gifNreg;
                break;
            case GSDebugEventKind::Register:
                f << "REG " << +e.reg << " = " << e.regValue;
                break;
            case GSDebugEventKind::Draw:
                f << "DRAW prim " << +e.prim.type << (e.prim.tme ? " TME" : "") << (e.prim.abe ? " ABE" : "")
                  << (e.prim.fst ? " FST" : "") << " ctx" << e.prim.ctxt << " verts " << std::dec << e.vertexCount
                  << " x " << e.xMin << ".." << e.xMax << " y " << e.yMin << ".." << e.yMax << " a " << +e.aMin
                  << ".." << +e.aMax << std::hex << " | fb " << e.frame.fbp << "/" << +e.frame.psm << " tex "
                  << e.tex0.tbp0 << " tbw " << +e.tex0.tbw << " psm " << +e.tex0.psm << " " << +e.tex0.tw << "x"
                  << +e.tex0.th << " tcc " << +e.tex0.tcc << " tfx " << +e.tex0.tfx << " cbp " << e.tex0.cbp
                  << " cpsm " << +e.tex0.cpsm << " cld " << +e.tex0.cld << " alpha " << e.alpha;
                break;
            case GSDebugEventKind::Transfer:
                f << "XFER dbp " << e.bitbltbuf.dbp << " dbw " << +e.bitbltbuf.dbw << " dpsm " << +e.bitbltbuf.dpsm
                  << " at " << std::dec << e.trxpos.dsax << "," << e.trxpos.dsay << " " << e.trxreg.rrw << "x"
                  << e.trxreg.rrh << " dir " << e.trxdir << " pixels " << e.transferPixels;
                break;
            case GSDebugEventKind::Present:
                f << "PRESENT display fbp " << e.displayFbp << " source " << e.sourceFbp << " " << std::dec << e.width
                  << "x" << e.height;
                break;
            }
            f << "\n";
        }
        RUNTIME_LOG("[tekken4] GS dump written for frame " << frame << "\n");
    }

    bool isSoundClient(uint32_t client)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_soundClients.count(client) != 0;
    }

    // Each replacement mirrors the generated stub wrappers: return to $ra, then handle or pass through.
    void sifBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = getRegU32(ctx, 31);
        if (getRegU32(ctx, 5) != kSoundSid)
        {
            ps2_stubs::sceSifBindRpc(rdram, ctx, runtime);
            return;
        }

        const uint32_t client = getRegU32(ctx, 4);
        uint8_t *cd = getMemPtr(rdram, client);
        if (!cd)
        {
            setReturnS32(ctx, -1);
            return;
        }
        // Any non-zero server ends the retry loop. Nothing dereferences it: calls on this client stop here.
        std::memset(cd + kClientBodyOffset, 0, kClientBodySize);
        std::memcpy(cd + kClientServerOffset, &client, sizeof(client));

        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_soundClients.insert(client).second)
        {
            RUNTIME_LOG("[tekken4] sound RPC 0x12345 bound to silent HLE server, client 0x" << std::hex << client
                                                                                            << std::dec << "\n");
        }
        setReturnS32(ctx, 0);
    }

    void sifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = getRegU32(ctx, 31);
        const uint32_t client = getRegU32(ctx, 4);
        if (!isSoundClient(client))
        {
            ps2_syscalls::sceSifCallRpc(rdram, ctx, runtime);
            return;
        }

        const uint32_t function = getRegU32(ctx, 5);
        const uint32_t reply = getRegU32(ctx, 9);
        const uint32_t replySize = getRegU32(ctx, 10);
        const uint32_t endFunction = getRegU32(ctx, 11);
        if (uint8_t *dst = replySize ? getMemPtr(rdram, reply) : nullptr)
        {
            std::memset(dst, 0, replySize < kMaxReplySize ? replySize : kMaxReplySize);
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_loggedCalls.insert(function).second)
        {
            RUNTIME_LOG("[tekken4] sound RPC call 0x" << std::hex << function << " (first time), reply 0x" << reply
                                                       << " size 0x" << replySize << std::dec << "\n");
        }
        // The per-frame sound update doubles as a frame counter: log at 1, 10, 100, ... frames.
        if (function == kSoundFrameCall && ++g_frames == g_nextFrameLog)
        {
            const auto now = std::chrono::steady_clock::now();
            if (g_frames == 1u)
                g_firstFrame = now;
            const double seconds = std::chrono::duration<double>(now - g_firstFrame).count();
            if (g_frames == 1u)
                RUNTIME_LOG("[tekken4] main loop frame 1\n");
            else
                RUNTIME_LOG("[tekken4] main loop frame " << g_frames << " after " << seconds << " s ("
                                                         << (g_frames - 1u) / seconds << " fps)\n");
            g_nextFrameLog *= 10u;
        }
        // The GS history is paused by default (the debug panel turns it on): record two frames, then dump.
        if (function == kSoundFrameCall && g_gsDumpFrame != 0u && g_frames + 2u == g_gsDumpFrame)
        {
            runtime->gs().clearDebugHistory();
            runtime->gs().setDebugHistoryPaused(false);
            std::lock_guard<std::mutex> gifLock(g_gifLogMutex);
            g_gifLog.open("gif_packets_frame" + std::to_string(g_gsDumpFrame) + ".txt");
        }
        if (function == kSoundFrameCall && g_frames == g_gsDumpFrame)
        {
            runtime->gs().setDebugHistoryPaused(true);
            {
                std::lock_guard<std::mutex> gifLock(g_gifLogMutex);
                g_gifLog.close();
            }
            dumpGs(*runtime, g_frames);
        }
        if (endFunction != 0u)
        {
            RUNTIME_LOG("[tekken4] sound RPC call 0x" << std::hex << function << " wants end function 0x"
                                                       << endFunction << ", not called" << std::dec << "\n");
        }
        setReturnS32(ctx, 0);
    }

    void sifCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = getRegU32(ctx, 31);
        if (!isSoundClient(getRegU32(ctx, 4)))
        {
            ps2_stubs::sceSifCheckStatRpc(rdram, ctx, runtime);
            return;
        }
        setReturnS32(ctx, 0); // never busy: every call completes inside sifCallRpc
    }

    void apply(PS2Runtime &runtime)
    {
        if (const char *dump = std::getenv("TEKKEN4_GS_DUMP"))
        {
            g_gsDumpFrame = std::strtoull(dump, nullptr, 10);
            // Same as the runtime's own packet function, plus the raw log while recording.
            PS2Runtime *rt = &runtime;
            runtime.gifArbiter().setProcessPacketFn([rt](const uint8_t *data, uint32_t size)
                                                    { processGifPacketLogged(*rt, data, size); });
        }
        runtime.registerFunction(kSceSifBindRpc, sifBindRpc);
        runtime.registerFunction(kSceSifCallRpc, sifCallRpc);
        runtime.registerFunction(kSceSifCheckStatRpc, sifCheckStatRpc);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Tekken 4 (SCES-50878 v2.00)", "SCES_508.78", 0x00180088u, 0xBEFFF97Au, tekken4::apply)
