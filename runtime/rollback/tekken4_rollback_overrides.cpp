// Tekken 4 (SLUS-20328, NTSC-U v1.00): rollback netcode phases 0 and 1 (docs/ROLLBACK_PLAN.md, docs/ROLLBACK_PHASE01.md).
// Nothing here runs unless TEKKEN4_RB_MODE is set: normal play keeps the default runtime path (no hook installed).
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
// TEKKEN4_RB_INPUT         frame-exact scripted pad 1 input instead of the keyboard (both runs see the same input):
//                          "<frame>:<Btn>[+<Btn>...][:<hold frames>],..." e.g. "500:Start,800:Cross:4"
//                          (Up Down Left Right Start Select Cross Circle Square Triangle L1 R1 L2 R2; hold 6).
// TEKKEN4_RB_FUZZ          "<from>[:<to>]": from that frame on, pad 1 plays a pseudo-random fight input (directions,
//                          punches, kicks; never Start/Select) that is a pure function of the frame number.
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
#include "Stubs/Pad.h"

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

    struct Config
    {
        Mode mode = Mode::Off;
        bool scripted = false;
        std::vector<ScriptPress> presses;
        uint64_t fuzzFrom = ~0ull, fuzzTo = ~0ull;
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
    };

    Config g_cfg;
    PS2Runtime *g_runtime = nullptr;
    uint64_t g_frame = 0;  // logical frame = boundary count since boot; restored with the snapshot
    uint64_t g_wallFrames = 0;
    FILE *g_log = nullptr;
    FILE *g_pagesLog = nullptr;
    ps2_rollback::Snapshot g_snapshot;
    uint64_t g_snapshotFrame = 0;
    TestState g_test;
    std::chrono::steady_clock::time_point g_lastBoundary{};
    uint64_t g_lastWaitNs = 0;
    uint64_t g_lastCycle = 0;
    uint64_t g_lastTick = 0;
    uint64_t g_lastLoops = 0;
    double g_hookMs = 0;
    std::map<std::string, uint64_t> g_wakeCounts;
    std::vector<uint64_t> g_prevPages; // previous boundary's page hashes: dirty-page count per frame
    uint64_t g_gsNs = 0, g_lastGsNs = 0; // host time in GS packet processing on the EE thread
    std::chrono::steady_clock::time_point g_start{};

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
    uint16_t fuzzButtons(uint64_t frame)
    {
        uint64_t x = (frame / 6u) * 0x9E3779B97F4A7C15ull + 0x1234567u;
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

    uint16_t scriptedButtons(uint64_t frame)
    {
        uint16_t buttons = 0;
        for (const ScriptPress &press : g_cfg.presses)
            if (frame >= press.frame && frame < press.frame + press.hold)
                buttons |= press.buttons;
        if (frame >= g_cfg.fuzzFrom && frame < g_cfg.fuzzTo)
            buttons |= fuzzButtons(frame);
        return buttons;
    }

    bool padProvider(int port, uint16_t &buttons, uint8_t (&axes)[4])
    {
        (void)axes;
        if (port != 0)
            return false; // pad 2 not connected (as with no host gamepad)
        uint16_t pressed = scriptedButtons(g_frame);
        if (g_test.wrong)
            pressed = static_cast<uint16_t>((fuzzButtons(g_frame * 7u + 3u) | kBtnLeft) & ~(kBtnStart | kBtnSelect));
        buttons = static_cast<uint16_t>(~pressed);
        return true;
    }

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

        const char *pass = g_test.resim ? "R" : (g_test.wrong ? "W" : (g_test.active ? "1" : "-"));
        std::fprintf(g_log, "f=%" PRIu64 " tick=%" PRIu64 " dt=%" PRIu64 " cyc=%" PRIu64 " dcyc=%" PRIu64
                            " h=%016" PRIx64 " ram=%016" PRIx64 " oth=%016" PRIx64 " sch=%016" PRIx64
                            " in=%04x ee=%.2f gs=%.2f wall=%.2f hk=%.2f dirty=%zu t=%.2f p=%s run=%d loop=%" PRIu64 " wake=%s:%d reg=%s\n",
                     g_frame, info.vsyncTick, info.vsyncTick - g_lastTick, info.eeCycle, info.eeCycle - g_lastCycle,
                     hash.total, hash.rdram, hash.other, hash.scheduler, scriptedButtons(g_frame), eeMs, gsMs, wallMs, g_hookMs, dirty,
                     std::chrono::duration<double>(hookStart - g_start).count(), pass, info.runningThreadId,
                     tekken4::g_frames - g_lastLoops, reasonName(info.reason), info.waitId, regionsText(hash).c_str());
        g_lastLoops = tekken4::g_frames;
        g_lastTick = info.vsyncTick;
        g_lastCycle = info.eeCycle;

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
        {
            if (g_test.resim)
            {
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
        if (g_wallFrames % 600u == 0u)
        {
            std::string wakes;
            for (const auto &[key, count] : g_wakeCounts)
                wakes += " " + key + "=" + std::to_string(count);
            std::fprintf(g_log, "WAKES after %" PRIu64 " wall frames:%s\n", g_wallFrames, wakes.c_str());
        }
        std::fflush(g_log);
        if (g_pagesLog)
            std::fflush(g_pagesLog);

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

    void apply(PS2Runtime &runtime)
    {
        const char *mode = std::getenv("TEKKEN4_RB_MODE");
        if (!mode || !*mode)
            return;
        if (std::strcmp(mode, "measure") == 0)
            g_cfg.mode = Mode::Measure;
        else if (std::strcmp(mode, "test") == 0)
            g_cfg.mode = Mode::Test;
        else
        {
            RUNTIME_LOG("[rollback] unknown TEKKEN4_RB_MODE=" << mode << " (measure|test), rollback off\n");
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
        if (const char *input = std::getenv("TEKKEN4_RB_INPUT"))
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
                g_cfg.presses.push_back(press);
            }
        }
        if (const char *fuzz = std::getenv("TEKKEN4_RB_FUZZ"))
        {
            g_cfg.scripted = true;
            const auto parts = split(fuzz, ':');
            if (!parts.empty())
                g_cfg.fuzzFrom = parseNumber(parts[0]);
            g_cfg.fuzzTo = parts.size() >= 2 ? parseNumber(parts[1]) : ~0ull;
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

        g_log = std::fopen("rb_frames.log", "w");
        if (!g_log)
        {
            RUNTIME_LOG("[rollback] cannot open rb_frames.log, rollback off\n");
            return;
        }
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
        runtime.eeScheduler().setBoundaryHook(boundary);
        RUNTIME_LOG("[rollback] TEKKEN4_RB_MODE=" << mode << ": boundary hook installed, "
                                                  << g_cfg.masks.size() << " hash masks, scripted input "
                                                  << (g_cfg.scripted ? "on" : "off") << "\n");
    }
}

PS2_REGISTER_GAME_OVERRIDE("Tekken 4 rollback (SLUS-20328 v1.00)", "SLUS_203.28", 0x00180088u, 0xB975F704u,
                           tekken4_rollback::apply)
