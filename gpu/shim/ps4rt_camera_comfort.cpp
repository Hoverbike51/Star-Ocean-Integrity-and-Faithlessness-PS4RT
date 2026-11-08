// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4rt_camera_comfort.h"
#include "ps4rt_camera_probe_profile.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Ps4RtCameraComfort {
namespace {
constexpr std::size_t site_offset=0x1b6959, original_constant=0x8607fc;
constexpr std::array<unsigned char,7> instruction{0x48,0x8d,0x05,0x9c,0x9e,0x6a,0};
constexpr std::array<unsigned char,16> consumer{
    0xc5,0xfa,0x10,0x18,0xc4,0xe3,0x79,0x04,0xc9,0xe5,
    0xc5,0xfa,0x11,0x9d,0x60,0xfd};
// Dedicated aligned Windows storage, kept alive until process exit. The native
// code performs one aligned 32-bit load; InterlockedExchange publishes each
// live selection as one 32-bit value without locks or extra work per update.
std::atomic<LONG*> coefficient{nullptr};

bool Matches(std::span<const unsigned char> image,std::string_view serial,std::string_view version) {
    using namespace Ps4RtCameraProbe;
    if (!ProfileMatches(image,serial,version) ||
        !Signature(image,site_offset,instruction) || !Signature(image,0x1b69b0,consumer)) return false;
    float original{};
    std::memcpy(&original,image.data()+original_constant,sizeof original);
    return std::bit_cast<std::uint32_t>(original)==std::bit_cast<std::uint32_t>(0.2f);
}

// Allocate only a genuinely free Windows region, within RIP-relative range.
// Never borrow game BSS, reserve guest memory or create executable storage.
LONG* AllocateNear(unsigned char* site) {
    const auto origin=reinterpret_cast<std::uintptr_t>(site)&~std::uintptr_t(65535);
    for (std::uintptr_t delta=65536;delta<=256ull*1024*1024;delta+=65536) {
        for (int sign:{1,-1}) {
            if ((sign<0 && origin<delta) || (sign>0 && origin>UINTPTR_MAX-delta)) continue;
            const auto address=sign>0?origin+delta:origin-delta;
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info) || info.State!=MEM_FREE) continue;
            auto* slot=static_cast<LONG*>(VirtualAlloc(reinterpret_cast<void*>(address),4096,
                MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
            if (!slot) continue;
            const auto distance=reinterpret_cast<std::intptr_t>(slot)-reinterpret_cast<std::intptr_t>(site+7);
            if (distance>=INT32_MIN && distance<=INT32_MAX) return slot;
            VirtualFree(slot,0,MEM_RELEASE);
        }
    }
    return nullptr;
}
}

bool Available() {return coefficient.load(std::memory_order_acquire)!=nullptr;}

bool Select(int mode) {
    if (mode<Original || mode>=Count) return false;
    auto* slot=coefficient.load(std::memory_order_acquire);
    if (!slot) return false;
    InterlockedExchange(slot,std::bit_cast<LONG>(Coefficient(mode)));
    return true;
}

bool Initialize(std::span<unsigned char> image,std::string_view serial,std::string_view version,int mode) {
    if (Available() || mode<Original || mode>=Count || !Matches(image,serial,version)) return false;
    auto* site=image.data()+site_offset;
    auto* slot=AllocateNear(site);
    if (!slot) return false;
    *slot=std::bit_cast<LONG>(Coefficient(mode));
    auto replacement=instruction;
    const auto displacement=std::int32_t(reinterpret_cast<std::intptr_t>(slot)-reinterpret_cast<std::intptr_t>(site+7));
    std::memcpy(replacement.data()+3,&displacement,sizeof displacement);
    DWORD old{};
    if (!VirtualProtect(site,replacement.size(),PAGE_EXECUTE_READWRITE,&old)) {
        VirtualFree(slot,0,MEM_RELEASE); return false;
    }
    // Only this function's LEA is redirected, not the shared 0.2 constant.
    // Native substeps, distance limiter, initialization/teleport reset, manual
    // angles and the downstream collision solver are left byte-for-byte intact.
    std::memcpy(site,replacement.data(),replacement.size());
    FlushInstructionCache(GetCurrentProcess(),site,replacement.size());
    DWORD ignored{};
    if (!VirtualProtect(site,replacement.size(),old,&ignored)) {
        std::fputs("Camera comfort: fatal page-protection failure\n",stderr);
        std::fflush(stderr); ExitProcess(26);
    }
    coefficient.store(slot,std::memory_order_release);
    std::printf("Camera comfort: guarded native positional filter ready (mode %d); FOV and rotation unchanged\n",mode);
    return true;
}
}
