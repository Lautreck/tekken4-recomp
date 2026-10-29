// Tekken 4 (SLUS-20328, NTSC-U v1.00): rollback netcode phases 0-2 (docs/ROLLBACK_PLAN.md, docs/ROLLBACK_PHASE01.md,
// docs/ROLLBACK_PHASE2.md). Nothing here runs unless TEKKEN4_RB_MODE is set: normal play keeps the default runtime
// path (no hook, no cut, no IOP gate installed).
//
// TEKKEN4_RB_MODE=measure  per frame boundary: frame number, vsync tick, EE cycle, full state hash (RDRAM with masks,
//                          scratchpad, VU memories, scheduler, device registers), per-1-MB region hashes, EE time.
//                          Written to rb_frames.log in the working directory (the run dir).
// TEKKEN4_RB_MODE=test     measure + rollback self-test (T5DR's T5_RB_MODE=test): every N frames save the machine,
//                          run K frames, restore, check the restored hash, run the same K frames again with the same
//                          scripted input and compare every boundary's hash with the first pass (K = 1..maxdepth).
//                          TEKKEN4_RB_TEST="start=<frame>,every=<N>,depth=<maxK>[,words=1][,mispredict=1]"
//                          (defaults 600,30,8). mispredict=1: like a real rollback, a second pass runs the K frames
//                          with WRONG input (pad 1 replaced), is rolled back again, and the third pass with the
//                          true input must match the first pass frame by frame.
// TEKKEN4_RB_MODE=selfnet  one-process netcode harness: pad 2 is the "remote" player. Its scripted input is revealed
//                          to the simulation late (TEKKEN4_RB_NET="start=<frame>,delay=<D>,jitter=<J>,max=<M>",
//                          defaults 2100,4,0,8); until then the input of the last revealed frame is used (prediction
//                          = repeat), and a misprediction rolls back to the mispredicted frame and re-simulates up to
//                          the live frame (snapshot every boundary, ring of M+2). Compare the LAST line of each frame
//                          (rb_compare.py --last) with a measure run fed the true input on time (TEKKEN4_RB_PAD2=1).
// Re-simulated frames (W/R passes of the test, rollbacks of selfnet) run with the cuts of TEKKEN4_RB_CUT
//                          ("none" or a list of draw, upload, iop, present, vu1; default draw,iop,present,vu1):
//                          draw = no GS primitive submission, upload = no host->local image data, iop = IOP frozen
//                          (sound RPCs answered from the live frames' log, async IOP->EE writes replayed by EE cycle),
//                          present = the presenter keeps the last live frame, vu1 = no VU1 microprogram runs (VU1
//                          data memory is then output state and left out of the hash unless TEKKEN4_RB_HASHVU1=1).
//                          TEKKEN4_RB_CUT_TAIL=<n> (default 1):
//                          the last n re-simulated frames draw again (the picture the next vblank shows).
// TEKKEN4_RB_SND           what a frozen-IOP RPC does when the re-simulated call differs from the logged one:
//                          "replay" (default: the logged reply is written, nothing reaches the IOP) or "forward"
//                          (the call goes to the IOP: a sound of the corrected timeline starts late).
// TEKKEN4_RB_IDLE=0       the vsync wait loop (0x22a150) iterates as the game does (default: its iterations up to the
//                          next event are accounted in one step; same EE cycles and events, much less host time).
// TEKKEN4_RB_SNDTRACE=1    rb_sound.log: every IOP RPC (frame, sid, function, send/reply bytes) and every replay.
// TEKKEN4_RB_INPUT         frame-exact scripted pad 1 input instead of the keyboard (both runs see the same input):
//                          "<frame>:<Btn>[+<Btn>...][:<hold frames>],..." e.g. "500:Start,800:Cross:4"
//                          (Up Down Left Right Start Select Cross Circle Square Triangle L1 R1 L2 R2; hold 6).
// TEKKEN4_RB_FUZZ          "<from>[:<to>]": from that frame on, pad 1 plays a pseudo-random fight input (directions,
//                          punches, kicks; never Start/Select) that is a pure function of the frame number.
// TEKKEN4_RB_PAD2=1        pad 2 connected (always on in selfnet), driven by TEKKEN4_RB_INPUT2 / TEKKEN4_RB_FUZZ2
//                          (same syntax; pad 2's fuzz is another sequence than pad 1's).
// TEKKEN4_RB_BOUNDARY      the frame boundary: "vblank" (default: every VBlankStart, at the next dispatcher
//                          checkpoint), or a main-thread wake: "vsync", "sema:<id>", "evf:<id>", "any".
// TEKKEN4_RB_MASK          extra hash masks "<addr>:<size>,..." (physical RDRAM, hex ok) on top of the built-in list.
// TEKKEN4_RB_DUMP          "<frame>,..." writes rb_dump_f<frame>.bin (RDRAM 32 MB) and rb_sched_f<frame>.bin.
// TEKKEN4_RB_PAGES         "<from>:<to>": also log the per-4-KB page hashes of RDRAM for those frames (rb_pages.log).
#include "game_overrides.h"
#include "ps2_log.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_rollback.h"
#include "runtime/ps2_rollback_cuts.h"
#include "Stubs/Pad.h"
#include "Stubs/SIF.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace tekken4
{
    extern uint64_t g_frames; // tekken4_overrides.cpp: passes of the main loop (per-frame sound RPC 0x100)
}

namespace tekken4_rollback
{
    enum class Mode
    {
        Off,
        Measure,
        Test,
        SelfNet,
    };

    // Hash masks: words that differ between two runs with the same input for reasons outside the simulation, or
    // that the restore cannot reproduce. Each entry needs its evidence in docs/ROLLBACK_PHASE01.md.
    const std::vector<ps2_rollback::MaskRange> kBuiltinMasks = {
    };

    constexpr uint16_t kBtnSelect = 1u << 0, kBtnStart = 1u << 3, kBtnUp = 1u << 4, kBtnRight = 1u << 5,
                       kBtnDown = 1u << 6, kBtnLeft = 1u << 7, kBtnL2 = 1u << 8, kBtnR2 = 1u << 9, kBtnL1 = 1u << 10,
                       kBtnR1 = 1u << 11, kBtnTriangle = 1u << 12, kBtnCircle = 1u << 13, kBtnCross = 1u << 14,
                       kBtnSquare = 1u << 15;

    struct ScriptPress
    {
        uint64_t frame = 0;
        uint64_t hold = 6;
        uint16_t buttons = 0; // active-high here
    };

    struct PadScript
    {
        std::vector<ScriptPress> presses;
        uint64_t fuzzFrom = ~0ull, fuzzTo = ~0ull;
        uint64_t salt = 0;
    };

    struct Config
    {
        Mode mode = Mode::Off;
        bool scripted = false;
        bool pad2 = false;
        PadScript pads[2];
        EeWaitReason boundaryReason = EeWaitReason::VSync;
        int boundaryId = -1; // -1: any id
        bool boundaryAny = false;
        bool boundaryVblank = true;
        std::vector<ps2_rollback::MaskRange> masks;
        std::vector<uint64_t> dumpFrames;
        uint64_t pagesFrom = ~0ull, pagesTo = 0;
        uint64_t testStart = 600, testEvery = 30, testDepth = 8;
        bool testWords = false;
        bool testMispredict = false;
        uint32_t cuts = ps2_rollback::CutDraw | ps2_rollback::CutIop | ps2_rollback::CutPresent | ps2_rollback::CutVu1;
        uint64_t cutTail = 1;
        bool sndForward = false;
        bool sndTrace = false;
        uint64_t netStart = 2100, netDelay = 4, netJitter = 0, netMax = 8;
    };

    struct TestState
    {
        bool active = false; // a save exists and the first pass is running
        bool resim = false;  // second pass running
        bool wrong = false;  // mispredict=1: the wrong-input pass is running
        uint64_t wrongDiverged = 0, wrongPasses = 0;
        bool wrongSawDiff = false;
        uint64_t saveFrame = 0;
        uint64_t targetFrame = 0;
        uint64_t depth = 0;
        uint64_t cycle = 0;
        std::map<uint64_t, ps2_rollback::StateHash> firstPass;
        std::map<uint64_t, std::vector<uint64_t>> firstPages;
        std::map<uint64_t, std::vector<uint8_t>> firstRam; // words=1 only
        // totals
        uint64_t rollbacks = 0, restoredOk = 0, restoredBad = 0, compared = 0, mismatches = 0;
        double saveMs = 0, restoreMs = 0, maxSaveMs = 0, maxRestoreMs = 0;
        // cost of the current re-simulation pass (R): restore + EE time of its frames
        double passRestoreMs = 0, passEeMs = 0;
        uint64_t passFrames = 0;
    };

    struct NetState
    {
        std::vector<ps2_rollback::Snapshot> ring; // snapshot of boundary b in ring[b % size]
        std::vector<uint64_t> ringFrame;
        std::map<uint64_t, uint16_t> usedRemote; // pad 2 buttons (active-high) the latest run of frame k used
        uint64_t revealed = 0;                   // remote input known for frames <= revealed
        bool resim = false;
        uint64_t liveFrame = 0;                  // the boundary a running re-simulation returns to
        uint64_t rollbacks = 0, resimFrames = 0, maxDepth = 0, mispredicted = 0, revealedFrames = 0;
        double restoreMs = 0, captureMs = 0, passEeMs = 0;
        uint64_t captures = 0;
        uint64_t depthHist[17]{};
        std::vector<double> passCost; // restore + EE time of each rollback's re-simulated frames
    };

    // The IOP outside the rollback state: per frame, what the IOP did to the EE on the live run, replayed while
    // re-simulating (CutIop). Frame = the logical frame executing (g_frame after its boundary).
    struct IopEffect
    {
        uint8_t kind = 0; // 0 write, 1 zero, 2 SIF command
        uint32_t address = 0;
        uint8_t *host = nullptr; // RDRAM/scratchpad pointer of address (writes)
        uint64_t cycle = 0;
        std::vector<uint8_t> data;
        size_t size = 0;
    };

    struct RpcRecord
    {
        uint32_t sid = 0, function = 0;
        std::vector<uint8_t> send;
        ps2x::iop::RpcResult result{};
        std::vector<IopEffect> effects;
    };

    struct FrameIop
    {
        std::vector<RpcRecord> rpcs;
        std::vector<IopEffect> async;
        size_t asyncCursor = 0;
        size_t rpcCursor = 0;
    };

    struct IopStats
    {
        uint64_t liveRpcs = 0, liveAsync = 0, liveSifCmds = 0;
        uint64_t replayedSame = 0, replayedDiff = 0, forwardedDiff = 0, forwardedUnlogged = 0, unloggedReplayEmpty = 0;
        uint64_t asyncReplayed = 0, asyncLeft = 0, eeToIopSuppressed = 0, frozenWritesIgnored = 0;
    };

    Config g_cfg;
    PS2Runtime *g_runtime = nullptr;
    uint64_t g_frame = 0;  // logical frame = boundary count since boot; restored with the snapshot
    uint64_t g_wallFrames = 0;
    FILE *g_log = nullptr;
    FILE *g_pagesLog = nullptr;
    FILE *g_sndLog = nullptr;
    ps2_rollback::Snapshot g_snapshot;
    uint64_t g_snapshotFrame = 0;
    TestState g_test;
    NetState g_net;
    std::map<uint64_t, FrameIop> g_iop;
    FrameIop *g_iopFrame = nullptr;
    RpcRecord *g_iopRpc = nullptr; // live RPC being recorded
    IopStats g_iopStats;
    uint32_t g_frameCuts = 0;       // cuts active while the current frame executes
    std::chrono::steady_clock::time_point g_lastBoundary{};
    uint64_t g_lastWaitNs = 0;
    uint64_t g_lastCycle = 0;
    uint64_t g_lastTick = 0;
    uint64_t g_lastLoops = 0;
    uint64_t g_lastIdle = 0;
    double g_hookMs = 0;
    std::map<std::string, uint64_t> g_wakeCounts;
    std::vector<uint64_t> g_prevPages; // previous boundary's page hashes: dirty-page count per frame
    uint64_t g_gsNs = 0, g_lastGsNs = 0; // host time in GS packet processing on the EE thread
    std::chrono::steady_clock::time_point g_start{};
    std::map<std::string, std::vector<double>> g_eeByKind; // EE ms per executed frame, by pass kind
    ps2_rollback::HostProfile g_lastProfile;               // host profile counters at the previous boundary

    uint64_t parseNumber(const std::string &text)
    {
        return std::strtoull(text.c_str(), nullptr, 0);
    }

    std::vector<std::string> split(const std::string &text, char by)
    {
        std::vector<std::string> out;
        std::stringstream stream(text);
        for (std::string item; std::getline(stream, item, by);)
            if (!item.empty())
                out.push_back(item);
        return out;
    }

    uint16_t buttonByName(const std::string &name)
    {
        static const std::pair<const char *, uint16_t> kNames[] = {
            {"Up", kBtnUp}, {"Down", kBtnDown}, {"Left", kBtnLeft}, {"Right", kBtnRight}, {"Start", kBtnStart},
            {"Select", kBtnSelect}, {"Cross", kBtnCross}, {"Circle", kBtnCircle}, {"Square", kBtnSquare},
            {"Triangle", kBtnTriangle}, {"L1", kBtnL1}, {"R1", kBtnR1}, {"L2", kBtnL2}, {"R2", kBtnR2}};
        for (const auto &[text, bit] : kNames)
            if (name == text)
                return bit;
        RUNTIME_LOG("[rollback] unknown button " << name << "\n");
        return 0;
    }

    // Pseudo-random fight input as a pure function of the frame: a new choice every 6 frames.
    uint16_t fuzzButtons(uint64_t frame, uint64_t salt = 0)
    {
        uint64_t x = (frame / 6u) * 0x9E3779B97F4A7C15ull + 0x1234567u + salt * 0xD1B54A32D192ED03ull;
        x ^= x >> 31;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 29;
        static const uint16_t kDirs[] = {0, 0, kBtnLeft, kBtnRight, kBtnDown, kBtnUp, kBtnRight | kBtnDown,
                                         kBtnLeft | kBtnDown, kBtnRight, kBtnRight};
        static const uint16_t kAttacks[] = {0, kBtnSquare, kBtnTriangle, kBtnCross, kBtnCircle, kBtnSquare | kBtnTriangle,
                                            kBtnCross | kBtnCircle, 0, kBtnSquare, kBtnCross};
        const uint16_t dir = kDirs[x % 10u];
        const uint16_t attack = ((frame % 6u) < 3u) ? kAttacks[(x >> 8) % 10u] : 0u; // press then release
        return static_cast<uint16_t>(dir | attack);
    }

    uint16_t scriptedButtons(int pad, uint64_t frame)
    {
        const PadScript &script = g_cfg.pads[pad];
        uint16_t buttons = 0;
        for (const ScriptPress &press : script.presses)
            if (frame >= press.frame && frame < press.frame + press.hold)
                buttons |= press.buttons;
        if (frame >= script.fuzzFrom && frame < script.fuzzTo)
            buttons |= fuzzButtons(frame, script.salt);
        return buttons;
    }

    // selfnet: the remote (pad 2) input a run of frame k uses: the true one once revealed, else the last revealed.
    uint16_t remoteInputFor(uint64_t k)
    {
        if (k < g_cfg.netStart || k <= g_net.revealed)
            return scriptedButtons(1, k);
        return scriptedButtons(1, g_net.revealed);
    }

    bool padProvider(int port, uint16_t &buttons, uint8_t (&axes)[4])
    {
        (void)axes;
        uint16_t pressed = 0;
        if (port == 0)
        {
            pressed = scriptedButtons(0, g_frame);
            if (g_test.wrong)
                pressed = static_cast<uint16_t>((fuzzButtons(g_frame * 7u + 3u) | kBtnLeft) & ~(kBtnStart | kBtnSelect));
        }
        else if (port == 1 && g_cfg.pad2)
        {
            if (g_cfg.mode == Mode::SelfNet)
            {
                const auto it = g_net.usedRemote.find(g_frame);
                pressed = it != g_net.usedRemote.end() ? it->second : remoteInputFor(g_frame);
            }
            else
                pressed = scriptedButtons(1, g_frame);
        }
        else
            return false; // not connected (as with no host gamepad)
        buttons = static_cast<uint16_t>(~pressed);
        return true;
    }

    // ---------------------------------------------------------------- IOP gate
    std::string hexBytes(const uint8_t *data, size_t size, size_t limit = 0x80)
    {
        std::string text;
        char buffer[4];
        for (size_t i = 0; i < size && i < limit; ++i)
        {
            std::snprintf(buffer, sizeof(buffer), "%02x", data[i]);
            text += buffer;
            if ((i & 3u) == 3u)
                text += ' ';
        }
        return text;
    }

    std::vector<uint8_t> guestBytes(uint32_t address, uint32_t size)
    {
        std::vector<uint8_t> out;
        if (size == 0 || size > 0x10000u)
            return out;
        uint8_t *rdram = g_runtime->memory().getRDRAM();
        if (const uint8_t *src = getMemPtr(rdram, address))
            out.assign(src, src + size);
        return out;
    }

    void applyEffect(const IopEffect &effect)
    {
        if (effect.kind == 2)
        {
            ps2_stubs::dispatchSifCommand(g_runtime->memory().getRDRAM(), g_runtime, effect.address,
                                          effect.data.data(), effect.data.size());
            return;
        }
        if (!effect.host)
            return;
        if (effect.kind == 1)
            std::memset(effect.host, 0, effect.size);
        else
            std::memcpy(effect.host, effect.data.data(), effect.data.size());
    }

    class Gate final : public ps2_rollback::IopGate
    {
    public:
        void guestWrite(uint32_t address, const uint8_t *data, size_t size) override
        {
            if (ps2_rollback::cutActive(ps2_rollback::CutIop))
            {
                ++g_iopStats.frozenWritesIgnored; // a forwarded call while frozen: not part of the log
                return;
            }
            IopEffect effect;
            effect.kind = data ? 0 : 1;
            effect.address = address;
            effect.host = getMemPtr(g_runtime->memory().getRDRAM(), address);
            effect.cycle = g_runtime->eeScheduler().eeCycle();
            effect.size = size;
            if (data)
                effect.data.assign(data, data + size);
            record(std::move(effect));
        }

        void sifCommand(uint32_t commandId, const uint8_t *packet, size_t size) override
        {
            if (ps2_rollback::cutActive(ps2_rollback::CutIop))
            {
                ++g_iopStats.frozenWritesIgnored;
                return;
            }
            IopEffect effect;
            effect.kind = 2;
            effect.address = commandId;
            effect.cycle = g_runtime->eeScheduler().eeCycle();
            effect.size = size;
            if (packet)
                effect.data.assign(packet, packet + size);
            ++g_iopStats.liveSifCmds;
            record(std::move(effect));
        }

        void rpcBegin(const ps2x::iop::RpcRequest &request) override
        {
            FrameIop &frame = currentFrame();
            frame.rpcs.emplace_back();
            RpcRecord &rec = frame.rpcs.back();
            rec.sid = request.sid;
            rec.function = request.function;
            rec.send = guestBytes(request.send.address, request.send.size);
            g_iopRpc = &rec;
            ++g_iopStats.liveRpcs;
        }

        void rpcEnd(const ps2x::iop::RpcRequest &request, const ps2x::iop::RpcResult &result) override
        {
            if (g_iopRpc)
            {
                g_iopRpc->result = result;
                if (g_sndLog)
                {
                    const std::vector<uint8_t> reply = guestBytes(request.receive.address, request.receive.size);
                    std::fprintf(g_sndLog, "L f=%" PRIu64 " cyc=%" PRIu64 " sid=%x fn=%x send=%u recv=%x/%u eff=%zu\n  S %s\n  Q %s\n",
                                 g_frame, g_runtime->eeScheduler().eeCycle(), request.sid, request.function,
                                 request.send.size, request.receive.address, request.receive.size,
                                 g_iopRpc->effects.size(), hexBytes(g_iopRpc->send.data(), g_iopRpc->send.size()).c_str(),
                                 hexBytes(reply.data(), reply.size(), 0x40).c_str());
                }
            }
            g_iopRpc = nullptr;
        }

        void frozenAdvance(uint64_t eeCycle) override
        {
            FrameIop *frame = g_iopFrame;
            if (!frame)
                return;
            while (frame->asyncCursor < frame->async.size() && frame->async[frame->asyncCursor].cycle <= eeCycle)
            {
                applyEffect(frame->async[frame->asyncCursor++]);
                ++g_iopStats.asyncReplayed;
            }
        }

        ps2x::iop::RpcResult frozenRpc(const ps2x::iop::RpcRequest &request,
                                       const std::function<ps2x::iop::RpcResult()> &forward) override
        {
            FrameIop &frame = currentFrame();
            const size_t index = frame.rpcCursor++;
            RpcRecord *rec = index < frame.rpcs.size() ? &frame.rpcs[index] : nullptr;
            if (rec && (rec->sid != request.sid || rec->function != request.function))
                rec = nullptr;
            if (!rec)
            {
                ++g_iopStats.forwardedUnlogged;
                if (g_sndLog)
                    std::fprintf(g_sndLog, "F f=%" PRIu64 " sid=%x fn=%x idx=%zu: no log, forwarded\n", g_frame,
                                 request.sid, request.function, index);
                return forward();
            }
            const std::vector<uint8_t> send = guestBytes(request.send.address, request.send.size);
            const bool same = send == rec->send;
            if (!same && g_cfg.sndForward)
            {
                ++g_iopStats.forwardedDiff;
                if (g_sndLog)
                    std::fprintf(g_sndLog, "F f=%" PRIu64 " sid=%x fn=%x idx=%zu: differs, forwarded\n  S %s\n", g_frame,
                                 request.sid, request.function, index, hexBytes(send.data(), send.size()).c_str());
                return forward();
            }
            ++(same ? g_iopStats.replayedSame : g_iopStats.replayedDiff);
            for (const IopEffect &effect : rec->effects)
                applyEffect(effect);
            if (g_sndLog && !same)
                std::fprintf(g_sndLog, "D f=%" PRIu64 " sid=%x fn=%x idx=%zu: differs, logged reply written\n  S %s\n",
                             g_frame, request.sid, request.function, index, hexBytes(send.data(), send.size()).c_str());
            return rec->result;
        }

        bool frozenIopWrite(uint32_t address, size_t size) override
        {
            (void)address;
            (void)size;
            ++g_iopStats.eeToIopSuppressed;
            return false;
        }

    private:
        static FrameIop &currentFrame()
        {
            if (!g_iopFrame)
                g_iopFrame = &g_iop[g_frame];
            return *g_iopFrame;
        }

        static void record(IopEffect &&effect)
        {
            if (g_iopRpc)
            {
                g_iopRpc->effects.push_back(std::move(effect));
                return;
            }
            ++g_iopStats.liveAsync;
            if (g_sndLog)
                std::fprintf(g_sndLog, "A f=%" PRIu64 " cyc=%" PRIu64 " kind=%u addr=%x size=%zu\n", g_frame, effect.cycle,
                             effect.kind, effect.address, effect.size);
            currentFrame().async.push_back(std::move(effect));
        }
    };

    Gate g_gate;

    // A frame starts executing: live frames get a fresh IOP log, re-simulated ones replay theirs from the start.
    void startFrameIop(bool live)
    {
        if (g_iopFrame && !live && g_iopFrame->asyncCursor < g_iopFrame->async.size())
            g_iopStats.asyncLeft += g_iopFrame->async.size() - g_iopFrame->asyncCursor;
        FrameIop &frame = g_iop[g_frame];
        if (live)
            frame = FrameIop{};
        frame.asyncCursor = 0;
        frame.rpcCursor = 0;
        g_iopFrame = &frame;
        g_iopRpc = nullptr;
        while (!g_iop.empty() && g_iop.begin()->first + 256u < g_frame)
            g_iop.erase(g_iop.begin());
    }

    // ---------------------------------------------------------------- logging helpers
    const char *reasonName(EeWaitReason reason)
    {
        switch (reason)
        {
        case EeWaitReason::None: return "none";
        case EeWaitReason::Sleep: return "sleep";
        case EeWaitReason::Semaphore: return "sema";
        case EeWaitReason::EventFlag: return "evf";
        case EeWaitReason::VSync: return "vsync";
        case EeWaitReason::External: return "ext";
        case EeWaitReason::Mpeg: return "mpeg";
        }
        return "?";
    }

    bool isBoundary(const EeBoundaryInfo &info)
    {
        if (g_cfg.boundaryVblank)
            return info.vblank;
        if (g_cfg.boundaryAny)
            return true;
        if (info.reason != g_cfg.boundaryReason)
            return false;
        return g_cfg.boundaryId < 0 || info.waitId == g_cfg.boundaryId;
    }

    void dumpFrame(uint64_t frame)
    {
        PS2Memory &memory = g_runtime->memory();
        {
            std::ofstream out("rb_dump_f" + std::to_string(frame) + ".bin", std::ios::binary);
            out.write(reinterpret_cast<const char *>(memory.getRDRAM()), PS2_RAM_SIZE);
            out.write(reinterpret_cast<const char *>(memory.getScratchpad()), PS2_SCRATCHPAD_SIZE);
        }
        std::vector<uint8_t> digest;
        g_runtime->eeScheduler().appendStateDigest(digest);
        std::ofstream out("rb_sched_f" + std::to_string(frame) + ".bin", std::ios::binary);
        out.write(reinterpret_cast<const char *>(digest.data()), static_cast<std::streamsize>(digest.size()));
        g_runtime->eeScheduler().publishSnapshot();
        const EeKernelSnapshot kernel = g_runtime->eeScheduler().snapshot();
        for (const EeThreadSnapshot &thread : kernel.threads)
            std::fprintf(g_log, "THREAD f=%" PRIu64 " id=%d status=%d wait=%s:%d prio=%d pc=0x%08x entry=0x%08x"
                                " invocations=%u\n",
                         frame, thread.id, static_cast<int>(thread.status), reasonName(thread.waitReason), thread.waitId,
                         thread.currentPriority, thread.pc, thread.entry, thread.invocationDepth);
        RUNTIME_LOG("[rollback] dumped frame " << frame << "\n");
    }

    std::string regionsText(const ps2_rollback::StateHash &hash)
    {
        std::string text;
        char buffer[16];
        for (int r = 0; r < 32; ++r)
        {
            std::snprintf(buffer, sizeof(buffer), "%04x", static_cast<unsigned>(hash.regions[r] & 0xFFFFu));
            text += buffer;
        }
        return text;
    }

    void logPageDiff(uint64_t frame, const std::vector<uint64_t> &now)
    {
        const auto it = g_test.firstPages.find(frame);
        if (it == g_test.firstPages.end())
            return;
        int shown = 0;
        size_t total = 0;
        std::string pages;
        for (size_t p = 0; p < now.size(); ++p)
        {
            if (now[p] == it->second[p])
                continue;
            ++total;
            if (shown++ < 24)
            {
                char buffer[24];
                std::snprintf(buffer, sizeof(buffer), " %07zx", p * 4096u);
                pages += buffer;
            }
        }
        std::fprintf(g_log, "  pages differing: %zu:%s\n", total, pages.c_str());
        const auto ram = g_test.firstRam.find(frame);
        if (ram == g_test.firstRam.end())
            return;
        const uint8_t *current = g_runtime->memory().getRDRAM();
        int words = 0;
        for (uint32_t a = 0; a < PS2_RAM_SIZE && words < 64; a += 4)
        {
            uint32_t before = 0, after = 0;
            std::memcpy(&before, ram->second.data() + a, 4);
            std::memcpy(&after, current + a, 4);
            if (before != after)
            {
                std::fprintf(g_log, "  word %07x: first %08x resim %08x\n", a, before, after);
                ++words;
            }
        }
    }

    double percentile(std::vector<double> values, double p)
    {
        if (values.empty())
            return 0.0;
        std::sort(values.begin(), values.end());
        const size_t at = std::min(values.size() - 1u, static_cast<size_t>(p * static_cast<double>(values.size() - 1u) + 0.5));
        return values[at];
    }

    std::string costText()
    {
        std::string text;
        char buffer[160];
        for (const auto &[kind, values] : g_eeByKind)
        {
            std::snprintf(buffer, sizeof(buffer), " %s:n=%zu,p50=%.2f,p90=%.2f,max=%.2f", kind.c_str(), values.size(),
                          percentile(values, 0.5), percentile(values, 0.9), percentile(values, 1.0));
            text += buffer;
        }
        return text;
    }

    std::string iopText()
    {
        char buffer[400];
        std::snprintf(buffer, sizeof(buffer),
                      "live rpcs %" PRIu64 " async %" PRIu64 " sifcmds %" PRIu64 "; frozen: replayed same %" PRIu64
                      " diff %" PRIu64 ", forwarded diff %" PRIu64 " unlogged %" PRIu64 ", async replayed %" PRIu64
                      " left %" PRIu64 ", EE->IOP writes suppressed %" PRIu64 ", writes of forwarded calls %" PRIu64,
                      g_iopStats.liveRpcs, g_iopStats.liveAsync, g_iopStats.liveSifCmds, g_iopStats.replayedSame,
                      g_iopStats.replayedDiff, g_iopStats.forwardedDiff, g_iopStats.forwardedUnlogged,
                      g_iopStats.asyncReplayed, g_iopStats.asyncLeft, g_iopStats.eeToIopSuppressed,
                      g_iopStats.frozenWritesIgnored);
        return buffer;
    }

    // ---------------------------------------------------------------- test mode
    // Restores the snapshot and checks the restored state against the first pass's hash of that frame.
    void rollBack(const char *from)
    {
        const auto restoreStart = std::chrono::steady_clock::now();
        const uint64_t fromFrame = g_frame;
        ps2_rollback::restore(*g_runtime, g_snapshot);
        g_frame = g_snapshotFrame;
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - restoreStart).count();
        g_test.restoreMs += ms;
        g_test.maxRestoreMs = std::max(g_test.maxRestoreMs, ms);
        g_test.passRestoreMs = ms;
        g_test.passEeMs = 0;
        g_test.passFrames = 0;
        ++g_test.rollbacks;
        std::vector<uint64_t> restoredPages;
        const ps2_rollback::StateHash restored = ps2_rollback::hashState(*g_runtime, g_cfg.masks, &restoredPages);
        const auto saved = g_test.firstPass.find(g_frame);
        const bool ok = saved != g_test.firstPass.end() && saved->second.total == restored.total;
        if (ok)
            ++g_test.restoredOk;
        else
        {
            ++g_test.restoredBad;
            std::fprintf(g_log, "RESTORE MISMATCH at frame %" PRIu64 ": saved h=%016" PRIx64 " restored h=%016" PRIx64
                                " ram %016" PRIx64 "/%016" PRIx64 " oth %016" PRIx64 "/%016" PRIx64 " sch %016" PRIx64
                                "/%016" PRIx64 "\n",
                         g_frame, saved != g_test.firstPass.end() ? saved->second.total : 0, restored.total,
                         saved != g_test.firstPass.end() ? saved->second.rdram : 0, restored.rdram,
                         saved != g_test.firstPass.end() ? saved->second.other : 0, restored.other,
                         saved != g_test.firstPass.end() ? saved->second.scheduler : 0, restored.scheduler);
            logPageDiff(g_frame, restoredPages);
        }
        std::fprintf(g_log, "ROLLBACK cycle %" PRIu64 " depth %" PRIu64 " after %s: %" PRIu64 " -> %" PRIu64
                            " restore %.2f ms, restored state %s\n",
                     g_test.cycle, g_test.depth, from, fromFrame, g_frame, ms, ok ? "OK" : "BAD");
    }

    // Which cuts the frame about to execute (g_frame) runs with.
    uint32_t cutsForNextFrame()
    {
        bool resim = false;
        uint64_t target = 0;
        bool tailAllowed = false;
        if (g_cfg.mode == Mode::Test)
        {
            resim = g_test.resim || g_test.wrong;
            target = g_test.targetFrame;
            tailAllowed = g_test.resim;
        }
        else if (g_cfg.mode == Mode::SelfNet)
        {
            resim = g_net.resim;
            target = g_net.liveFrame;
            tailAllowed = true;
        }
        if (!resim)
            return 0;
        uint32_t cuts = g_cfg.cuts;
        // frame g_frame ends at boundary g_frame+1; the last cutTail frames before the target draw again
        if (tailAllowed && g_frame + g_cfg.cutTail >= target)
            cuts &= ~static_cast<uint32_t>(ps2_rollback::CutDraw | ps2_rollback::CutUpload | ps2_rollback::CutVu1);
        return cuts;
    }

    void testBoundary(const ps2_rollback::StateHash &hash, const std::vector<uint64_t> &pages, double eeMs)
    {
        if (g_test.resim)
        {
            g_test.passEeMs += eeMs;
            ++g_test.passFrames;
            const auto it = g_test.firstPass.find(g_frame);
            ++g_test.compared;
            const bool same = it != g_test.firstPass.end() && it->second.total == hash.total;
            if (!same)
            {
                ++g_test.mismatches;
                std::fprintf(g_log, "MISMATCH resim frame %" PRIu64 " (cycle %" PRIu64 ", depth %" PRIu64
                                    "): first h=%016" PRIx64 " ram=%016" PRIx64 " oth=%016" PRIx64 " sch=%016" PRIx64
                                    " reg=%s\n",
                             g_frame, g_test.cycle, g_test.depth, it != g_test.firstPass.end() ? it->second.total : 0,
                             it != g_test.firstPass.end() ? it->second.rdram : 0,
                             it != g_test.firstPass.end() ? it->second.other : 0,
                             it != g_test.firstPass.end() ? it->second.scheduler : 0,
                             it != g_test.firstPass.end() ? regionsText(it->second).c_str() : "");
                logPageDiff(g_frame, pages);
            }
            if (g_frame >= g_test.targetFrame)
            {
                g_test.resim = false;
                g_test.firstPass.clear();
                g_test.firstPages.clear();
                g_test.firstRam.clear();
                std::fprintf(g_log, "PASSCOST cycle %" PRIu64 " depth %" PRIu64 " restore %.2f resim %.2f ms (%.2f per frame)\n",
                             g_test.cycle, g_test.depth, g_test.passRestoreMs, g_test.passEeMs,
                             g_test.passFrames ? g_test.passEeMs / static_cast<double>(g_test.passFrames) : 0.0);
                g_eeByKind["pass" + std::to_string(g_test.depth)].push_back(g_test.passRestoreMs + g_test.passEeMs);
            }
        }
        else if (g_test.active)
        {
            g_test.firstPass[g_frame] = hash;
            g_test.firstPages[g_frame] = pages;
            if (g_cfg.testWords)
            {
                const uint8_t *ram = g_runtime->memory().getRDRAM();
                g_test.firstRam[g_frame].assign(ram, ram + PS2_RAM_SIZE);
            }
            if (g_frame >= g_test.targetFrame)
            {
                rollBack("first pass");
                g_test.active = false;
                if (g_cfg.testMispredict)
                {
                    g_test.wrong = true;
                    g_test.wrongSawDiff = false;
                    ++g_test.wrongPasses;
                }
                else
                    g_test.resim = true;
                // The re-run starts here: the next boundaries are frames saveFrame+1 .. targetFrame again.
            }
        }
        else if (g_test.wrong)
        {
            // Wrong-input pass (a mispredicted timeline): only check that it really diverged.
            const auto it = g_test.firstPass.find(g_frame);
            if (it != g_test.firstPass.end() && it->second.total != hash.total)
                g_test.wrongSawDiff = true;
            if (g_frame >= g_test.targetFrame)
            {
                if (g_test.wrongSawDiff)
                    ++g_test.wrongDiverged;
                g_test.wrong = false;
                rollBack("wrong-input pass");
                g_test.resim = true;
            }
        }
        else if (g_frame >= g_cfg.testStart && (g_frame - g_cfg.testStart) % g_cfg.testEvery == 0)
        {
            const auto saveStart = std::chrono::steady_clock::now();
            ps2_rollback::capture(*g_runtime, g_snapshot);
            g_snapshotFrame = g_frame;
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - saveStart).count();
            g_test.saveMs += ms;
            g_test.maxSaveMs = std::max(g_test.maxSaveMs, ms);
            ++g_test.cycle;
            g_test.depth = 1u + (g_test.cycle - 1u) % g_cfg.testDepth;
            g_test.saveFrame = g_frame;
            g_test.targetFrame = g_frame + g_test.depth;
            g_test.active = true;
            g_test.firstPass.clear();
            g_test.firstPages.clear();
            g_test.firstRam.clear();
            g_test.firstPass[g_frame] = hash;
            g_test.firstPages[g_frame] = pages;
        }
        if (g_wallFrames % 600u == 0u)
        {
            std::fprintf(g_log,
                         "SUMMARY wall frames %" PRIu64 " logical %" PRIu64 ": rollbacks %" PRIu64 " restore ok %" PRIu64
                         " bad %" PRIu64 ", resim frames compared %" PRIu64 " mismatches %" PRIu64
                         ", wrong-input passes %" PRIu64 " diverged %" PRIu64
                         ", save avg %.2f max %.2f ms, restore avg %.2f max %.2f ms\n",
                         g_wallFrames, g_frame, g_test.rollbacks, g_test.restoredOk, g_test.restoredBad,
                         g_test.compared, g_test.mismatches, g_test.wrongPasses, g_test.wrongDiverged,
                         g_test.cycle ? g_test.saveMs / g_test.cycle : 0.0,
                         g_test.maxSaveMs, g_test.rollbacks ? g_test.restoreMs / g_test.rollbacks : 0.0,
                         g_test.maxRestoreMs);
        }
    }

    // ---------------------------------------------------------------- selfnet mode
    void netCapture()
    {
        const size_t slot = g_frame % g_net.ring.size();
        const auto t0 = std::chrono::steady_clock::now();
        ps2_rollback::capture(*g_runtime, g_net.ring[slot]);
        g_net.ringFrame[slot] = g_frame;
        g_net.captureMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++g_net.captures;
    }

    uint64_t jitterFor(uint64_t frame)
    {
        if (g_cfg.netJitter == 0)
            return 0;
        uint64_t x = frame * 0x9E3779B97F4A7C15ull + 0x51u;
        x ^= x >> 29;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 32;
        return x % (g_cfg.netJitter + 1u);
    }

    void netBoundary(double eeMs)
    {
        if (g_frame < g_cfg.netStart)
        {
            g_net.revealed = g_frame; // before the start everything is known (menus, lockstep-like)
            return;
        }
        if (g_net.resim)
        {
            g_net.passEeMs += eeMs;
            ++g_net.resimFrames;
            netCapture(); // the corrected state of this boundary replaces the mispredicted one
            if (g_frame >= g_net.liveFrame)
            {
                g_net.resim = false;
                g_net.passCost.push_back(g_net.passEeMs);
            }
            else
            {
                g_net.usedRemote[g_frame] = remoteInputFor(g_frame);
                return;
            }
        }
        else
        {
            netCapture();
            // Reveal the remote input that "arrives" now: frames up to f - delay - jitter, never more than max behind.
            const uint64_t lag = g_cfg.netDelay + jitterFor(g_frame);
            uint64_t newRevealed = g_frame > lag ? g_frame - lag : 0;
            newRevealed = std::max(newRevealed, g_net.revealed);
            if (g_frame > g_cfg.netMax)
                newRevealed = std::max(newRevealed, g_frame - g_cfg.netMax);
            newRevealed = std::min(newRevealed, g_frame - 1u);
            uint64_t firstWrong = ~0ull;
            for (uint64_t k = g_net.revealed + 1u; k <= newRevealed; ++k)
            {
                ++g_net.revealedFrames;
                const auto used = g_net.usedRemote.find(k);
                if (k >= g_cfg.netStart && used != g_net.usedRemote.end() && used->second != scriptedButtons(1, k))
                {
                    firstWrong = std::min(firstWrong, k);
                    ++g_net.mispredicted;
                }
            }
            g_net.revealed = newRevealed;
            if (firstWrong != ~0ull)
            {
                const size_t slot = firstWrong % g_net.ring.size();
                if (g_net.ringFrame[slot] != firstWrong)
                    std::fprintf(g_log, "NET no snapshot for frame %" PRIu64 " (have %" PRIu64 ")\n", firstWrong,
                                 g_net.ringFrame[slot]);
                else
                {
                    const uint64_t depth = g_frame - firstWrong;
                    const auto t0 = std::chrono::steady_clock::now();
                    ps2_rollback::restore(*g_runtime, g_net.ring[slot]);
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    g_net.restoreMs += ms;
                    g_net.passEeMs = ms;
                    ++g_net.rollbacks;
                    g_net.maxDepth = std::max(g_net.maxDepth, depth);
                    ++g_net.depthHist[std::min<uint64_t>(depth, 16u)];
                    g_net.liveFrame = g_frame;
                    g_net.resim = true;
                    g_frame = firstWrong;
                }
            }
        }
        g_net.usedRemote[g_frame] = remoteInputFor(g_frame);
        while (!g_net.usedRemote.empty() && g_net.usedRemote.begin()->first + 64u < g_frame)
            g_net.usedRemote.erase(g_net.usedRemote.begin());
        if (g_wallFrames % 600u == 0u)
        {
            std::string hist;
            for (int d = 0; d <= 16; ++d)
                if (g_net.depthHist[d])
                    hist += " " + std::to_string(d) + ":" + std::to_string(g_net.depthHist[d]);
            std::fprintf(g_log,
                         "NETSUMMARY wall frames %" PRIu64 " logical %" PRIu64 ": rollbacks %" PRIu64 " resim frames %" PRIu64
                         " mispredicted %" PRIu64 " of %" PRIu64 " revealed, max depth %" PRIu64 " depths%s; restore avg %.2f,"
                         " capture avg %.2f ms, rollback cost p50 %.2f p90 %.2f max %.2f ms\n",
                         g_wallFrames, g_frame, g_net.rollbacks, g_net.resimFrames, g_net.mispredicted,
                         g_net.revealedFrames, g_net.maxDepth, hist.c_str(),
                         g_net.rollbacks ? g_net.restoreMs / static_cast<double>(g_net.rollbacks) : 0.0,
                         g_net.captures ? g_net.captureMs / static_cast<double>(g_net.captures) : 0.0,
                         percentile(g_net.passCost, 0.5), percentile(g_net.passCost, 0.9), percentile(g_net.passCost, 1.0));
        }
    }

    // ---------------------------------------------------------------- the boundary
    const char *passLabel()
    {
        if (g_cfg.mode == Mode::SelfNet)
            return g_net.resim ? "R" : (g_frame >= g_cfg.netStart ? "S" : "-");
        return g_test.resim ? "R" : (g_test.wrong ? "W" : (g_test.active ? "1" : "-"));
    }

    void boundary(const EeBoundaryInfo &info)
    {
        if (info.reason != EeWaitReason::None)
        {
            std::string key = std::string(reasonName(info.reason)) + ":" + std::to_string(info.waitId);
            ++g_wakeCounts[key];
        }
        if (!isBoundary(info))
            return;

        const auto hookStart = std::chrono::steady_clock::now();
        const uint32_t executedCuts = g_frameCuts; // cuts the frame that just ended ran with
        ++g_frame;
        ++g_wallFrames;
        std::vector<uint64_t> pages;
        const ps2_rollback::StateHash hash = ps2_rollback::hashState(*g_runtime, g_cfg.masks, &pages);
        size_t dirty = 0;
        if (g_prevPages.size() == pages.size())
            for (size_t p = 0; p < pages.size(); ++p)
                dirty += pages[p] != g_prevPages[p] ? 1u : 0u;
        g_prevPages = pages;

        // EE time of the frame: wall time between boundaries minus host pacing waits minus this hook's own time.
        const uint64_t waitNs = g_runtime->eeScheduler().hostWaitNanoseconds();
        const double wallMs = g_lastBoundary.time_since_epoch().count() == 0
                                  ? 0.0
                                  : std::chrono::duration<double, std::milli>(hookStart - g_lastBoundary).count();
        const double eeMs = wallMs - static_cast<double>(waitNs - g_lastWaitNs) / 1e6 - g_hookMs;
        const double gsMs = static_cast<double>(g_gsNs - g_lastGsNs) / 1e6;
        g_lastGsNs = g_gsNs;
        const ps2_rollback::HostProfile &prof = ps2_rollback::g_hostProfile;
        const double vuMs = static_cast<double>(prof.vu1Ns - g_lastProfile.vu1Ns) / 1e6;
        const double vifMs = static_cast<double>(prof.vif1Ns - g_lastProfile.vif1Ns) / 1e6;
        const double iopMs = static_cast<double>(prof.iopNs - g_lastProfile.iopNs) / 1e6;
        const uint64_t vuRuns = prof.vu1Runs - g_lastProfile.vu1Runs;
        g_lastProfile = prof;

        const char *pass = passLabel();
        std::fprintf(g_log, "f=%" PRIu64 " tick=%" PRIu64 " dt=%" PRIu64 " cyc=%" PRIu64 " dcyc=%" PRIu64
                            " h=%016" PRIx64 " ram=%016" PRIx64 " oth=%016" PRIx64 " sch=%016" PRIx64
                            " idle=%" PRIu64 " in=%04x in2=%04x ee=%.2f gs=%.2f vif=%.2f vu=%.2f/%" PRIu64 " iop=%.2f wall=%.2f hk=%.2f dirty=%zu t=%.2f p=%s cut=%x run=%d loop=%" PRIu64
                            " wake=%s:%d reg=%s\n",
                     g_frame, info.vsyncTick, info.vsyncTick - g_lastTick, info.eeCycle, info.eeCycle - g_lastCycle,
                     hash.total, hash.rdram, hash.other, hash.scheduler,
                     g_runtime->eeScheduler().idleSkippedCycles() - g_lastIdle, scriptedButtons(0, g_frame),
                     g_cfg.pad2 ? scriptedButtons(1, g_frame) : 0u, eeMs, gsMs, vifMs, vuMs, vuRuns, iopMs, wallMs, g_hookMs, dirty,
                     std::chrono::duration<double>(hookStart - g_start).count(), pass, executedCuts, info.runningThreadId,
                     tekken4::g_frames - g_lastLoops, reasonName(info.reason), info.waitId, regionsText(hash).c_str());
        g_lastLoops = tekken4::g_frames;
        g_lastIdle = g_runtime->eeScheduler().idleSkippedCycles();
        g_lastTick = info.vsyncTick;
        g_lastCycle = info.eeCycle;
        if (wallMs > 0.0 && g_frame > 2u)
        {
            const std::string kind = pass[0] == 'R' ? ((executedCuts & ps2_rollback::CutDraw) ? "R" : "Rtail") : pass;
            g_eeByKind[kind].push_back(eeMs);
            g_eeByKind[kind + ".gs"].push_back(gsMs);
            g_eeByKind[kind + ".vif"].push_back(vifMs);
            g_eeByKind[kind + ".vu"].push_back(vuMs);
            g_eeByKind[kind + ".iop"].push_back(iopMs);
        }

        if (g_pagesLog && g_frame >= g_cfg.pagesFrom && g_frame <= g_cfg.pagesTo)
        {
            std::fprintf(g_pagesLog, "f=%" PRIu64, g_frame);
            for (const uint64_t page : pages)
                std::fprintf(g_pagesLog, " %016" PRIx64, page);
            std::fprintf(g_pagesLog, "\n");
        }
        if (std::find(g_cfg.dumpFrames.begin(), g_cfg.dumpFrames.end(), g_frame) != g_cfg.dumpFrames.end())
            dumpFrame(g_frame);

        if (g_cfg.mode == Mode::Test)
            testBoundary(hash, pages, eeMs);
        else if (g_cfg.mode == Mode::SelfNet)
            netBoundary(eeMs);

        g_frameCuts = cutsForNextFrame();
        ps2_rollback::setActiveCuts(g_frameCuts);
        startFrameIop((g_frameCuts & ps2_rollback::CutIop) == 0u);

        if (g_wallFrames % 600u == 0u)
        {
            std::string wakes;
            for (const auto &[key, count] : g_wakeCounts)
                wakes += " " + key + "=" + std::to_string(count);
            std::fprintf(g_log, "WAKES after %" PRIu64 " wall frames:%s\n", g_wallFrames, wakes.c_str());
            std::fprintf(g_log, "COST ee ms per executed frame by pass:%s\n", costText().c_str());
            std::fprintf(g_log, "IOP %s\n", iopText().c_str());
        }
        std::fflush(g_log);
        if (g_pagesLog)
            std::fflush(g_pagesLog);
        if (g_sndLog)
            std::fflush(g_sndLog);

        g_lastWaitNs = g_runtime->eeScheduler().hostWaitNanoseconds();
        const auto hookEnd = std::chrono::steady_clock::now();
        g_hookMs = std::chrono::duration<double, std::milli>(hookEnd - hookStart).count();
        g_lastBoundary = hookStart;
    }

    // The game's srand (0x2a2528: *(_impure_ptr) + 88 = seed), logged with its caller so the seed source is known
    // (phase 0 question 7). Same effect as the original; only installed in rollback mode without TEKKEN4_RAND_SEED.
    void loggedSrand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t seed = getRegU32(ctx, 4);
        const uint32_t ra = getRegU32(ctx, 31);
        uint32_t reent = 0u;
        std::memcpy(&reent, rdram + 0x304b94u, 4);
        SET_GPR_U32(ctx, 2, 0x300000u);
        SET_GPR_U32(ctx, 3, reent);
        if (uint8_t *state = getMemPtr(rdram, reent + 88u))
            std::memcpy(state, &seed, 4);
        if (g_log)
            std::fprintf(g_log, "SRAND seed=0x%08x ra=0x%08x frame=%" PRIu64 " cycle=%" PRIu64 "\n", seed, ra, g_frame,
                         g_runtime ? g_runtime->eeScheduler().eeCycle() : 0u);
        ctx->pc = ra;
    }

    void parseScript(PadScript &script, const char *input, const char *fuzz)
    {
        if (input)
        {
            g_cfg.scripted = true;
            for (const std::string &item : split(input, ','))
            {
                const auto parts = split(item, ':');
                if (parts.size() < 2)
                    continue;
                ScriptPress press;
                press.frame = parseNumber(parts[0]);
                for (const std::string &name : split(parts[1], '+'))
                    press.buttons |= buttonByName(name);
                if (parts.size() >= 3)
                    press.hold = parseNumber(parts[2]);
                script.presses.push_back(press);
            }
        }
        if (fuzz)
        {
            g_cfg.scripted = true;
            const auto parts = split(fuzz, ':');
            if (!parts.empty())
                script.fuzzFrom = parseNumber(parts[0]);
            script.fuzzTo = parts.size() >= 2 ? parseNumber(parts[1]) : ~0ull;
        }
    }

    void apply(PS2Runtime &runtime)
    {
        const char *mode = std::getenv("TEKKEN4_RB_MODE");
        if (!mode || !*mode)
            return;
        if (std::strcmp(mode, "measure") == 0)
            g_cfg.mode = Mode::Measure;
        else if (std::strcmp(mode, "test") == 0)
            g_cfg.mode = Mode::Test;
        else if (std::strcmp(mode, "selfnet") == 0)
            g_cfg.mode = Mode::SelfNet;
        else
        {
            RUNTIME_LOG("[rollback] unknown TEKKEN4_RB_MODE=" << mode << " (measure|test|selfnet), rollback off\n");
            return;
        }
        g_runtime = &runtime;
        g_start = std::chrono::steady_clock::now();
        g_cfg.masks = kBuiltinMasks;
        if (const char *masks = std::getenv("TEKKEN4_RB_MASK"))
            for (const std::string &item : split(masks, ','))
            {
                const auto parts = split(item, ':');
                if (parts.size() == 2)
                    g_cfg.masks.push_back({static_cast<uint32_t>(parseNumber(parts[0])) & 0x1FFFFFFFu,
                                           static_cast<uint32_t>(parseNumber(parts[1]))});
            }
        g_cfg.pads[1].salt = 0x5EC0u;
        parseScript(g_cfg.pads[0], std::getenv("TEKKEN4_RB_INPUT"), std::getenv("TEKKEN4_RB_FUZZ"));
        parseScript(g_cfg.pads[1], std::getenv("TEKKEN4_RB_INPUT2"), std::getenv("TEKKEN4_RB_FUZZ2"));
        if (const char *pad2 = std::getenv("TEKKEN4_RB_PAD2"); pad2 && parseNumber(pad2) != 0)
            g_cfg.pad2 = true;
        if (g_cfg.mode == Mode::SelfNet)
        {
            g_cfg.pad2 = true;
            g_cfg.scripted = true;
        }
        if (const char *which = std::getenv("TEKKEN4_RB_BOUNDARY"))
        {
            const auto parts = split(which, ':');
            const std::string kind = parts.empty() ? "" : parts[0];
            g_cfg.boundaryVblank = kind == "vblank";
            if (kind == "any")
                g_cfg.boundaryAny = true;
            else if (kind == "sema")
                g_cfg.boundaryReason = EeWaitReason::Semaphore;
            else if (kind == "evf")
                g_cfg.boundaryReason = EeWaitReason::EventFlag;
            else if (kind == "sleep")
                g_cfg.boundaryReason = EeWaitReason::Sleep;
            else
                g_cfg.boundaryReason = EeWaitReason::VSync;
            if (parts.size() >= 2)
                g_cfg.boundaryId = static_cast<int>(parseNumber(parts[1]));
        }
        if (const char *dump = std::getenv("TEKKEN4_RB_DUMP"))
            for (const std::string &item : split(dump, ','))
                g_cfg.dumpFrames.push_back(parseNumber(item));
        if (const char *pagesSpec = std::getenv("TEKKEN4_RB_PAGES"))
        {
            const auto parts = split(pagesSpec, ':');
            if (parts.size() == 2)
            {
                g_cfg.pagesFrom = parseNumber(parts[0]);
                g_cfg.pagesTo = parseNumber(parts[1]);
                g_pagesLog = std::fopen("rb_pages.log", "w");
            }
        }
        if (const char *test = std::getenv("TEKKEN4_RB_TEST"))
            for (const std::string &item : split(test, ','))
            {
                const auto parts = split(item, '=');
                if (parts.size() != 2)
                    continue;
                if (parts[0] == "start")
                    g_cfg.testStart = parseNumber(parts[1]);
                else if (parts[0] == "every")
                    g_cfg.testEvery = std::max<uint64_t>(2u, parseNumber(parts[1]));
                else if (parts[0] == "depth")
                    g_cfg.testDepth = std::max<uint64_t>(1u, parseNumber(parts[1]));
                else if (parts[0] == "words")
                    g_cfg.testWords = parseNumber(parts[1]) != 0;
                else if (parts[0] == "mispredict")
                    g_cfg.testMispredict = parseNumber(parts[1]) != 0;
            }
        if (g_cfg.mode == Mode::Test && g_cfg.testDepth >= g_cfg.testEvery)
            g_cfg.testDepth = g_cfg.testEvery - 1u;
        if (const char *net = std::getenv("TEKKEN4_RB_NET"))
            for (const std::string &item : split(net, ','))
            {
                const auto parts = split(item, '=');
                if (parts.size() != 2)
                    continue;
                if (parts[0] == "start")
                    g_cfg.netStart = parseNumber(parts[1]);
                else if (parts[0] == "delay")
                    g_cfg.netDelay = std::max<uint64_t>(1u, parseNumber(parts[1]));
                else if (parts[0] == "jitter")
                    g_cfg.netJitter = parseNumber(parts[1]);
                else if (parts[0] == "max")
                    g_cfg.netMax = std::max<uint64_t>(1u, parseNumber(parts[1]));
            }
        if (g_cfg.netMax < g_cfg.netDelay)
            g_cfg.netMax = g_cfg.netDelay;
        if (const char *cut = std::getenv("TEKKEN4_RB_CUT"))
        {
            g_cfg.cuts = 0;
            for (const std::string &item : split(cut, ','))
            {
                if (item == "draw")
                    g_cfg.cuts |= ps2_rollback::CutDraw;
                else if (item == "upload")
                    g_cfg.cuts |= ps2_rollback::CutUpload;
                else if (item == "iop")
                    g_cfg.cuts |= ps2_rollback::CutIop;
                else if (item == "present")
                    g_cfg.cuts |= ps2_rollback::CutPresent;
                else if (item == "vu1")
                    g_cfg.cuts |= ps2_rollback::CutVu1;
            }
        }
        // VU1 data memory is output state once VU1 runs are cut (docs/ROLLBACK_PHASE2.md): not hashed then.
        ps2_rollback::g_hostProfile.hashVu1Data = (g_cfg.cuts & ps2_rollback::CutVu1) == 0u;
        if (const char *hashVu1 = std::getenv("TEKKEN4_RB_HASHVU1"))
            ps2_rollback::g_hostProfile.hashVu1Data = parseNumber(hashVu1) != 0;
        ps2_rollback::g_hostProfile.on = true;
        if (const char *tail = std::getenv("TEKKEN4_RB_CUT_TAIL"))
            g_cfg.cutTail = parseNumber(tail);
        if (const char *snd = std::getenv("TEKKEN4_RB_SND"))
            g_cfg.sndForward = std::strcmp(snd, "forward") == 0;
        if (const char *trace = std::getenv("TEKKEN4_RB_SNDTRACE"))
            g_cfg.sndTrace = parseNumber(trace) != 0;

        g_log = std::fopen("rb_frames.log", "w");
        if (!g_log)
        {
            RUNTIME_LOG("[rollback] cannot open rb_frames.log, rollback off\n");
            return;
        }
        if (g_cfg.sndTrace)
            g_sndLog = std::fopen("rb_sound.log", "w");
        if (g_cfg.mode == Mode::SelfNet)
        {
            const size_t size = static_cast<size_t>(g_cfg.netMax + g_cfg.netJitter + 2u);
            g_net.ring.resize(size);
            g_net.ringFrame.assign(size, ~0ull);
        }
        std::fprintf(g_log, "CONFIG mode=%s cuts=%x tail=%" PRIu64 " snd=%s pad2=%d net=start:%" PRIu64 ",delay:%" PRIu64
                            ",jitter:%" PRIu64 ",max:%" PRIu64 "\n",
                     mode, g_cfg.cuts, g_cfg.cutTail, g_cfg.sndForward ? "forward" : "replay", g_cfg.pad2 ? 1 : 0,
                     g_cfg.netStart, g_cfg.netDelay, g_cfg.netJitter, g_cfg.netMax);
        if (g_cfg.scripted)
            ps2_stubs::setPadInputProvider(padProvider);
        if (const char *perf = std::getenv("TEKKEN4_PERF_LOG"); !perf || !*perf)
        {
            // GS packet processing runs on the EE thread: time it so EE-only cost = ee - gs (TEKKEN4_PERF_LOG
            // installs its own wrapper; then gs= stays 0).
            PS2Runtime *rt = &runtime;
            runtime.gifArbiter().setProcessPacketFn([rt](const uint8_t *data, uint32_t size)
                                                    {
                                                        const auto t0 = std::chrono::steady_clock::now();
                                                        rt->gs().processGIFPacket(data, size);
                                                        g_gsNs += static_cast<uint64_t>(
                                                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                std::chrono::steady_clock::now() - t0).count());
                                                    });
        }
        if (const char *seed = std::getenv("TEKKEN4_RAND_SEED"); !seed || !*seed)
            runtime.registerFunction(0x002a2528u, loggedSrand);
        // The IOP gate logs what the IOP does to the EE on every live frame (only needed when frames are
        // re-simulated with the IOP frozen, but cheap, and measure runs then show the same counters).
        ps2_rollback::setIopGate(&g_gate);
        // The main loop's vsync wait FUN_0022a140 polls 0x1ccafb7 at 0x22a150 until the vblank handler clears it:
        // pure cycle burning, skipped in bulk up to the next event (TEKKEN4_RB_IDLE=0: iterate as the game does).
        if (const char *idle = std::getenv("TEKKEN4_RB_IDLE"); !idle || parseNumber(idle) != 0)
            runtime.eeScheduler().setIdleLoopPcs({0x0022a150u});
        runtime.eeScheduler().setBoundaryHook(boundary);
        RUNTIME_LOG("[rollback] TEKKEN4_RB_MODE=" << mode << ": boundary hook installed, "
                                                  << g_cfg.masks.size() << " hash masks, scripted input "
                                                  << (g_cfg.scripted ? "on" : "off") << ", resim cuts 0x" << std::hex
                                                  << g_cfg.cuts << std::dec << "\n");
    }
}

PS2_REGISTER_GAME_OVERRIDE("Tekken 4 rollback (SLUS-20328 v1.00)", "SLUS_203.28", 0x00180088u, 0xB975F704u,
                           tekken4_rollback::apply)
