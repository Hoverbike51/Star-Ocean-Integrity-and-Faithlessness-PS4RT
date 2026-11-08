// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace Ps4RtLeader {
// Startup-only redirect of one verified E8 call. The relay tail-jumps without
// changing registers, so the original System V arguments and return site survive.
// The shared native function and every other caller remain untouched.
class ReturnCallHook {
    unsigned char* site{};
    unsigned char* relay{};
    std::array<unsigned char,5> saved{};
    static bool Write(unsigned char* at,const void* value) {
        DWORD old{};
        if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&old))return false;
        std::memcpy(at,value,5);FlushInstructionCache(GetCurrentProcess(),at,5);
        DWORD ignored{};
        if(!VirtualProtect(at,5,old,&ignored)) {
            std::fputs("Exploration leader: fatal call-site protection failure\n",stderr);
            std::fflush(stderr);ExitProcess(26);
        }
        return true;
    }
public:
    bool Install(unsigned char* at,const void* native_target,const void* replacement) {
        if(relay || !at || !native_target || !replacement || at[0]!=0xe8)return false;
        std::int32_t displacement{};std::memcpy(&displacement,at+1,4);
        if(reinterpret_cast<std::intptr_t>(at+5)+displacement!=reinterpret_cast<std::intptr_t>(native_target))return false;
        const auto origin=reinterpret_cast<std::uintptr_t>(at)&~std::uintptr_t(65535);
        for(std::uintptr_t delta=65536;delta<=256ull*1024*1024 && !relay;delta+=65536) {
            for(int sign:{1,-1}) {
                if((sign<0 && origin<delta) || (sign>0 && origin>UINTPTR_MAX-delta))continue;
                const auto address=sign>0?origin+delta:origin-delta;
                MEMORY_BASIC_INFORMATION info{};
                if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof info) || info.State!=MEM_FREE)continue;
                relay=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(address),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
                if(relay)break;
            }
        }
        if(!relay)return false;
        const auto distance=reinterpret_cast<std::intptr_t>(relay)-reinterpret_cast<std::intptr_t>(at+5);
        if(distance<INT32_MIN || distance>INT32_MAX) {VirtualFree(relay,0,MEM_RELEASE);relay=nullptr;return false;}
        const unsigned char jump[]={0xff,0x25,0,0,0,0};
        std::memcpy(relay,jump,6);std::memcpy(relay+6,&replacement,8);
        DWORD old{};
        if(!VirtualProtect(relay,4096,PAGE_EXECUTE_READ,&old)) {VirtualFree(relay,0,MEM_RELEASE);relay=nullptr;return false;}
        FlushInstructionCache(GetCurrentProcess(),relay,14);
        std::array<unsigned char,5> call{0xe8};const auto relative=std::int32_t(distance);
        std::memcpy(call.data()+1,&relative,4);std::memcpy(saved.data(),at,5);
        if(!Write(at,call.data())) {VirtualFree(relay,0,MEM_RELEASE);relay=nullptr;return false;}
        site=at;return true;
    }
    // Controlled tests/startup rollback only; never remove with guest threads live.
    bool Remove() {
        if(!relay)return true;
        if(!Write(site,saved.data()))return false;
        VirtualFree(relay,0,MEM_RELEASE);relay=nullptr;site=nullptr;return true;
    }
};
}
