// Tekken 4 (SCES-50878, PAL v2.00): fight camera debug log, a second game override next to tekken4_overrides.cpp
// (every override registered for this ELF is applied). tools\stage_generated.cmd copies it into ps2xRuntime.
//
// TEKKEN4_CAMERA_LOG=<lines>: wraps the camera update FUN_00182430 (Grok fight-camera analysis,
// logs/grok/fight-camera_20261022_005358.json: FUN_00182430 -> FUN_0018e1e0 eye = base + dir * 1000 ->
// FUN_00193800 look-at) and logs, every 60th call, the values to compare with PCSX2 (tools/pine.py):
// the camera pointer words at 0x2AA0D0 / 0x2AA0D4, cam = p + 0xF08, slot index *(cam + 0x174), the slot's
// 4x4 matrix at 0x01CE2270 + index * 0xC0 and the float at slot + 0x74.
#include "game_overrides.h"
#include "ps2_log.h"
#include "ps2_runtime.h"
#include <ps2_recompiled_functions.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace tekken4_camera
{
    constexpr uint32_t kRamMask = 0x01FFFFFFu;
    std::atomic<int> g_linesLeft{0};
    std::atomic<uint32_t> g_calls{0};

    uint32_t readWord(const uint8_t *rdram, uint32_t addr)
    {
        uint32_t v = 0u;
        std::memcpy(&v, rdram + (addr & kRamMask & ~3u), 4);
        return v;
    }

    float readFloat(const uint8_t *rdram, uint32_t addr)
    {
        float f = 0.0f;
        std::memcpy(&f, rdram + (addr & kRamMask & ~3u), 4);
        return f;
    }

    void logCamera(const uint8_t *rdram, uint32_t call)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(3) << "[camera] call " << call;
        for (const uint32_t pointerAddr : {0x002AA0D0u, 0x002AA0D4u})
        {
            const uint32_t p = readWord(rdram, pointerAddr);
            o << " | p@" << std::hex << pointerAddr << "=0x" << p << std::dec;
            if (p == 0u || (p & kRamMask) != p)
                continue;
            const uint32_t cam = p + 0xF08u;
            const uint32_t index = readWord(rdram, cam + 0x174u);
            o << " idx " << index;
            if (index > 64u)
                continue;
            const uint32_t slot = 0x01CE2270u + index * 0xC0u;
            o << " m=[";
            for (uint32_t i = 0; i < 16u; ++i)
                o << (i ? (i % 4u == 0u ? "; " : " ") : "") << readFloat(rdram, slot + i * 4u);
            o << "] f74 " << readFloat(rdram, slot + 0x74u);
        }
        RUNTIME_LOG(o.str() << "\n");
    }

    // First calls and every 60th: the call rate tells which of the three runs every frame.
    bool shouldLog(uint32_t call)
    {
        return (call <= 3u || call % 60u == 0u) && g_linesLeft.fetch_sub(1) > 0;
    }

    void cameraUpdateLogged(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        FUN_00182430_0x182430(rdram, ctx, runtime);
        const uint32_t call = g_calls.fetch_add(1u) + 1u;
        if (shouldLog(call))
            logCamera(rdram, call);
    }

    // FUN_0018e1e0 (eye = base + dir * 1000) and FUN_00193800 (look-at): arguments in, a0 = the result pointer.
    std::atomic<uint32_t> g_eyeCalls{0}, g_lookAtCalls{0};
    void logArgs(const char *what, uint32_t call, const uint32_t (&args)[4], const uint8_t *rdram)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(3) << "[camera] " << what << " call " << call << std::hex;
        for (int r = 0; r < 4; ++r)
            o << " a" << r << "=0x" << args[r];
        const uint32_t a0 = args[0];
        if (a0 != 0u && (a0 & kRamMask) == a0)
        {
            o << std::dec << " *a0=[";
            for (uint32_t i = 0; i < 8u; ++i)
                o << (i ? " " : "") << readFloat(rdram, a0 + i * 4u);
            o << "]";
        }
        RUNTIME_LOG(o.str() << "\n");
    }

    void eyeLogged(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t call = g_eyeCalls.fetch_add(1u) + 1u;
        const bool log = shouldLog(call);
        uint32_t args[4] = {getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6), getRegU32(ctx, 7)};
        FUN_0018e1e0_0x18e1e0(rdram, ctx, runtime);
        if (log)
            logArgs("eye FUN_0018e1e0 (a0 after)", call, args, rdram);
    }

    void lookAtLogged(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t call = g_lookAtCalls.fetch_add(1u) + 1u;
        const bool log = shouldLog(call);
        uint32_t args[4] = {getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6), getRegU32(ctx, 7)};
        FUN_00193800_0x193800(rdram, ctx, runtime);
        if (log)
            logArgs("lookat FUN_00193800 (a0 after)", call, args, rdram);
    }

    void apply(PS2Runtime &runtime)
    {
        if (const char *lines = std::getenv("TEKKEN4_CAMERA_LOG"))
        {
            g_linesLeft = std::atoi(lines);
            runtime.registerFunction(0x00182430u, cameraUpdateLogged);
            runtime.registerFunction(0x0018e1e0u, eyeLogged);
            runtime.registerFunction(0x00193800u, lookAtLogged);
            RUNTIME_LOG("[camera] logging FUN_00182430 / FUN_0018e1e0 / FUN_00193800 (first 3 calls, then every 60th), "
                        << g_linesLeft.load() << " lines\n");
        }
    }
}

PS2_REGISTER_GAME_OVERRIDE("Tekken 4 camera log (SCES-50878 v2.00)", "SCES_508.78", 0x00180088u, 0xBEFFF97Au,
                           tekken4_camera::apply)
