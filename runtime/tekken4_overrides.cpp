// Tekken 4 (SCES-50878, PAL v2.00) overrides for PS2Recomp's ps2xRuntime.
// tools\stage_generated.cmd copies this file into ps2xRuntime/src/runner next to the generated code.
// Addresses are for this ELF only (matched by name, entry point and CRC32 below).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "runtime/ps2_memory.h"

#include <cstring>
#include <mutex>
#include <set>

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

    std::mutex g_mutex;
    std::set<uint32_t> g_soundClients;
    std::set<uint32_t> g_loggedCalls;

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
        runtime.registerFunction(kSceSifBindRpc, sifBindRpc);
        runtime.registerFunction(kSceSifCallRpc, sifCallRpc);
        runtime.registerFunction(kSceSifCheckStatRpc, sifCheckStatRpc);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Tekken 4 (SCES-50878 v2.00)", "SCES_508.78", 0x00180088u, 0xBEFFF97Au, tekken4::apply)
