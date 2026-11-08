// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>

namespace Ps4RtHud {
// Startup-only detours. Only whitelisted complete position-independent
// prologues or single vertex-writer instructions are accepted.
template<size_t Count> class HookSet {
    static_assert(Count>0&&Count<=32);
    unsigned char* memory{};
    std::array<unsigned char*,Count> sites{};
    std::array<std::array<unsigned char,8>,Count> originals{};
    std::array<size_t,Count> sizes{};
    static void Jump(unsigned char* at,const void* target) {
        const unsigned char code[]={0xff,0x25,0,0,0,0};
        std::memcpy(at,code,6);std::memcpy(at+6,&target,8);
    }
    static bool Write(unsigned char* at,const void* bytes,size_t size) {
        DWORD old=0;if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
        std::memcpy(at,bytes,size);FlushInstructionCache(GetCurrentProcess(),at,size);
        DWORD ignored=0;
        if(!VirtualProtect(at,size,old,&ignored)) {
            // Continuing with partially installed detours or changed page
            // protections is unsafe. This is never an ordinary compatibility refusal.
            std::fputs("HUD layout: fatal detour page-protection failure\n",stderr);
            std::fflush(stderr);ExitProcess(26);
        }
        return true;
    }
public:
    HookSet()=default;HookSet(const HookSet&)=delete;
    bool Install(std::array<unsigned char*,Count> addresses,std::array<const void*,Count> targets,std::array<size_t,Count> lengths={}) {
        if(memory)return false;
        constexpr unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57};
        constexpr unsigned char prologue_r14[]={0x55,0x48,0x89,0xe5,0x41,0x56};
        constexpr unsigned char vertex[]={0x4c,0x8b,0x87,0x38,0x03,0,0};
        constexpr unsigned char batch[]={0x48,0x63,0x87,0x1c,0x20,0,0};
        constexpr unsigned char extended[]={0x48,0x83,0xbf,0x38,0x03,0,0,0};
        // Verified handle allocator: mov ecx,[rdi+0x13c]. This is a complete
        // position-independent six-byte instruction, not a function prologue.
        constexpr unsigned char allocator[]={0x8b,0x8f,0x3c,0x01,0,0};
        for(size_t n=0;n<Count;++n){if(!lengths[n])lengths[n]=6;const auto len=lengths[n];auto* at=addresses[n];
            if(!((len==6&&(!std::memcmp(at,prologue,6)||!std::memcmp(at,prologue_r14,6)||!std::memcmp(at,allocator,6)))||(len==7&&(!std::memcmp(at,vertex,7)||!std::memcmp(at,batch,7)))||(len==8&&!std::memcmp(at,extended,8))))return false;}
        sizes=lengths;
        const auto origin=reinterpret_cast<std::uintptr_t>(addresses[0])&~std::uintptr_t(65535);
        // Reserve only genuinely free Windows regions. Never commit pages in a
        // guest reservation or steal space from the game image/import stubs.
        for(std::uintptr_t delta=65536;delta<=256ull*1024*1024&&!memory;delta+=65536) {
            for(int sign:{1,-1}) {
                if(sign<0&&origin<delta)continue;
                auto* wanted=reinterpret_cast<void*>(sign>0?origin+delta:origin-delta);
                MEMORY_BASIC_INFORMATION info{};
                if(VirtualQuery(wanted,&info,sizeof info)&&info.State==MEM_FREE)
                    memory=static_cast<unsigned char*>(VirtualAlloc(wanted,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
                if(memory)break;
            }
        }
        if(!memory)return false;
        sites=addresses;
        for(size_t n=0;n<Count;++n) {
            auto* stub=memory+n*128;
            const auto distance=reinterpret_cast<std::intptr_t>(stub+32)-reinterpret_cast<std::intptr_t>(sites[n]+5);
            if(distance<std::numeric_limits<std::int32_t>::min()||distance>std::numeric_limits<std::int32_t>::max()) {
                VirtualFree(memory,0,MEM_RELEASE);memory=nullptr;return false;
            }
            std::memcpy(originals[n].data(),sites[n],sizes[n]);std::memcpy(stub,sites[n],sizes[n]);
            Jump(stub+sizes[n],sites[n]+sizes[n]);Jump(stub+32,targets[n]);
        }
        DWORD old=0;if(!VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old)) {
            VirtualFree(memory,0,MEM_RELEASE);memory=nullptr;return false;
        }
        FlushInstructionCache(GetCurrentProcess(),memory,4096);
        for(size_t n=0;n<Count;++n) {
            unsigned char branch[8]={0xe9,0,0,0,0,0x90,0x90,0x90};
            const auto distance=std::int32_t(reinterpret_cast<std::intptr_t>(memory+n*128+32)-reinterpret_cast<std::intptr_t>(sites[n]+5));
            std::memcpy(branch+1,&distance,4);
            if(!Write(sites[n],branch,sizes[n])) {
                for(size_t k=0;k<n;++k)if(!Write(sites[k],originals[k].data(),sizes[k]))ExitProcess(26);
                VirtualFree(memory,0,MEM_RELEASE);memory=nullptr;return false;
            }
        }
        return true;
    }
    void* Original(size_t n) const {return memory?memory+n*128:nullptr;}
    // Used only by a controlled self-test, never while guest threads are active.
    bool Remove() {
        if(!memory)return true;
        for(size_t n=0;n<Count;++n)if(!Write(sites[n],originals[n].data(),sizes[n]))return false;
        VirtualFree(memory,0,MEM_RELEASE);memory=nullptr;return true;
    }
};
using HookPair=HookSet<2>;
}
