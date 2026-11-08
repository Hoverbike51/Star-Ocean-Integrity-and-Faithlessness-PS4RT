// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4rt_camera_probe.h"
#include "ps4rt_camera_probe_profile.h"
#include "ps4rt_hud_hook.h"
#include "common/types.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>

extern "C" int runtime_memory_is_mapped(std::uintptr_t address,std::uint64_t bytes);

namespace Ps4RtCameraProbe {
namespace {
using FollowFn = void(PS4_SYSV_ABI *)(void*,float);
FollowFn original{};
Ps4RtHud::HookSet<1> hooks;
using SwitchFn=void(PS4_SYSV_ABI *)(void*,std::uint32_t,std::uint32_t);
SwitchFn original_switch{};
Ps4RtHud::HookSet<1> switch_hook;
std::uintptr_t base{};
std::span<const unsigned char> loaded_image;
std::filesystem::path report_path;
std::atomic_flag observing = ATOMIC_FLAG_INIT;
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> calls{0}, skipped{0};
std::atomic<std::uint64_t> host_reads{0}, rejected_reads{0};

struct Snapshot {
    std::uint64_t milliseconds{}, call{};
    std::uintptr_t controller{}, target{}, camera{}, lens{}, vtable_rva{};
    int mode=-1, previous_mode=-1;
    unsigned manager_chain_level=0, camera_chain_level=0;
    float native_delta{}, radians{}, aspect{}, post_radians{};
    bool manager_valid=false, manager_matches=false, script_lock=false, lens_valid=false;
    // Observed native storage, deliberately not named as a verified target or
    // collision solution yet. This is the evidence needed before editing it.
    std::array<float,12> follow_before{}, follow_after{};
    std::array<float,12> actor_transform{}, camera_transform{};
    bool follow_before_valid=false, follow_after_valid=false;
    bool actor_transform_valid=false, camera_transform_valid=false;
    std::uint32_t actor_handle{}, controlled_handle{};
    int character_id=-1;
    std::array<std::int16_t,9> party_ids{-1,-1,-1,-1,-1,-1,-1,-1,-1};
    bool party_valid=false, control_valid=false;
};
struct SwitchEvent {
    std::uint64_t milliseconds{};
    std::uintptr_t caller_rva{};
    std::uint32_t old_handle{},requested_handle{},before{},after{};
    bool before_valid=false,after_valid=false,scene_matches=false;
};
std::array<SwitchEvent,64> switches{};
std::atomic<std::size_t> switch_count{0};
std::atomic_flag switching=ATOMIC_FLAG_INIT;
std::array<Snapshot,2048> snapshots{};
std::atomic<std::size_t> count{0};
ULONGLONG started{}, next_sample{};
std::atomic<std::uint64_t> invalid_delta{0};
std::atomic<float> delta_min{std::numeric_limits<float>::max()}, delta_max{0};

bool HostReadable(std::uintptr_t address,std::size_t bytes) {
    if (!address || bytes>std::numeric_limits<std::uintptr_t>::max()-address) return false;
    const auto end=address+bytes;
    while (address<end) {
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address),&page,sizeof page) ||
            page.State!=MEM_COMMIT || (page.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
            !(page.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|
                           PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
        const auto first=reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        if (first>address || page.RegionSize>std::numeric_limits<std::uintptr_t>::max()-first ||
            first+page.RegionSize<=address) return false;
        address=std::min(end,first+page.RegionSize);
    }
    return true;
}

template<typename T> bool Read(std::uintptr_t object,std::size_t offset,T& value) {
    if (!object || offset>std::numeric_limits<std::uintptr_t>::max()-object) return false;
    const auto address=object+offset;
    if (sizeof(T)>std::numeric_limits<std::uintptr_t>::max()-address) return false;
    const auto image_offset=address>=base?address-base:std::numeric_limits<std::uintptr_t>::max();
    // The loader-owned executable/BSS is not part of the runtime guest-mmap
    // registry. Its bounded span remains alive throughout guest execution.
    const bool in_image=image_offset<=loaded_image.size() &&
                        sizeof(T)<=loaded_image.size()-image_offset;
    if (!in_image && !runtime_memory_is_mapped(address,sizeof(T))) {
        // Some engine singletons use the host CRT heap as well. Inspect only
        // these exact fields in committed readable pages, never guard/reserved
        // memory. This is a read-only probe, not authority for a native write.
        if (!HostReadable(address,sizeof(T))) {++rejected_reads; return false;}
        ++host_reads;
    }
    std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(T));
    return true;
}
template<std::size_t N>
bool ReadFloats(std::uintptr_t object,std::size_t offset,std::array<float,N>& value) {
    std::array<float,N> candidate{};
    if (!Read(object,offset,candidate) ||
        !std::all_of(candidate.begin(),candidate.end(),[](float x) {return std::isfinite(x);}))
        return false;
    value=candidate; return true;
}
bool ReadFollow(std::uintptr_t object,std::array<float,12>& value) {
    // Only observed vector slots. Do not export intervening padding/scratch
    // words as floats: they are not initialized camera coordinates.
    std::array<float,12> candidate{};
    std::size_t index=0;
    for (const auto offset:{0x140u,0x160u,0x180u}) {
        std::array<float,4> vector{};
        if (!ReadFloats(object,offset,vector)) return false;
        std::copy(vector.begin(),vector.end(),candidate.begin()+index); index+=4;
    }
    value=candidate; return true;
}

bool VirtualRoot(std::uintptr_t object,std::uintptr_t& root) {
    std::uintptr_t table{};std::int64_t delta{};
    if (!Read(object,0,table) || table<base+24 || table-base>=loaded_image.size() ||
        !Read(table-24,0,delta) || delta< -0x200000 || delta>0x200000) return false;
    if ((delta<0 && object<std::uint64_t(-delta)) ||
        (delta>0 && object>UINTPTR_MAX-std::uint64_t(delta))) return false;
    root=delta<0?object-std::uint64_t(-delta):object+std::uint64_t(delta);
    return true;
}
bool SceneRoot(std::uintptr_t& root) {
    std::uintptr_t scene{},owner{};
    return Read(base,SceneGlobal,scene) && Read(scene,0x40,owner) && VirtualRoot(owner,root);
}
void PS4_SYSV_ABI ControlSwitch(void* manager,std::uint32_t old_handle,std::uint32_t requested) {
    // Observe actual native switches only. Never manufacture a request, change
    // its arguments, call a Lua wrapper or manipulate the party from this probe.
    const bool owner=enabled.load(std::memory_order_relaxed) && !switching.test_and_set(std::memory_order_acquire);
    if (!owner) {original_switch(manager,old_handle,requested);return;}
    const auto index=switch_count.load(std::memory_order_relaxed);
    SwitchEvent event;
    std::uintptr_t observed_root{};
    if (index<switches.size()) {
        event.milliseconds=GetTickCount64()-started;
        const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        if(caller>=base && caller-base<loaded_image.size()) event.caller_rva=caller-base;
        event.old_handle=old_handle;event.requested_handle=requested;
        std::uintptr_t scene_root{},manager_root{};
        event.scene_matches=SceneRoot(scene_root) && VirtualRoot(reinterpret_cast<std::uintptr_t>(manager),manager_root) && scene_root==manager_root;
        observed_root=scene_root;
        event.before_valid=event.scene_matches && Read(scene_root,0x186a0,event.before);
    }
    original_switch(manager,old_handle,requested);
    if(index<switches.size()) {
        std::uintptr_t root{};
        event.after_valid=event.scene_matches && SceneRoot(root) && root==observed_root && Read(root,0x186a0,event.after);
        switches[index]=event;switch_count.store(index+1,std::memory_order_release);
    }
    switching.clear(std::memory_order_release);
}

Snapshot Observe(void* object,float delta,std::uint64_t call,ULONGLONG now) {
    Snapshot value;
    value.controller=reinterpret_cast<std::uintptr_t>(object);
    value.native_delta=std::isfinite(delta)?delta:0;
    value.milliseconds=now-started; value.call=call;
    std::uintptr_t scene{}, owner{}, manager{}, controller{}, table{};
    if (Read(base,SceneGlobal,scene) && scene) value.manager_chain_level=1;
    if (value.manager_chain_level && Read(scene,0x40,owner) && owner &&
        owner<=std::numeric_limits<std::uintptr_t>::max()-0x20) {
        value.manager_chain_level=2;
        manager=owner+0x20;
        unsigned char lock{};
        value.manager_valid=Read(manager,0x50,value.previous_mode) && Read(manager,0x58,value.mode) &&
            Read(manager,0x6c,lock) && Read(manager,8,controller);
        value.script_lock=lock!=0;
        value.manager_matches=value.manager_valid && controller==value.controller;
        if (value.manager_valid) value.manager_chain_level=3;
    }
    Read(value.controller,0xa8,value.target);
    Read(value.target,0x54,value.actor_handle);
    std::uintptr_t status{},root{},party{}; std::int16_t character=-1;
    if(Read(value.target,0x5a00,status) && Read(status,8,character) && character>=0 && character<14)
        value.character_id=character;
    value.control_valid=SceneRoot(root) && Read(root,0x186a0,value.controlled_handle);
    if(Read(base,0xb839f8,party) && party) {
        value.party_valid=true;
        for(std::size_t slot=0;slot<value.party_ids.size();++slot) {
            if(!Read(party,0x54+0x21f80+slot*4,character) || character< -1 || character>=14) {
                value.party_valid=false;value.party_ids.fill(-1);break;
            }
            value.party_ids[slot]=character;
        }
    }
    value.follow_before_valid=ReadFollow(value.controller,value.follow_before);
    value.actor_transform_valid=ReadFloats(value.target,0x70,value.actor_transform);
    if (Read(value.controller,0,table) && table>=base && table-base<0xb83a18)
        value.vtable_rva=table-base;
    std::uintptr_t renderer{}, system{};
    if (Read(base,RendererGlobal,renderer) && renderer) value.camera_chain_level=1;
    if (value.camera_chain_level && Read(renderer,0xb8,system) && system) value.camera_chain_level=2;
    if (value.camera_chain_level==2 && Read(system,8,value.camera) && value.camera) value.camera_chain_level=3;
    if (value.camera_chain_level==3 && Read(value.camera,0xdf0,value.lens) && value.lens) {
        value.camera_chain_level=4;
        float radians{}, aspect{};
        value.lens_valid=Read(value.lens,0xc,radians) && Read(value.camera,0xeb0,aspect) &&
            std::isfinite(radians) && radians>0 && radians<3.141593f &&
            std::isfinite(aspect) && aspect>0 && aspect<10;
        if (value.lens_valid) {value.radians=radians; value.aspect=aspect; value.camera_chain_level=5;}
        value.camera_transform_valid=ReadFloats(value.camera,0x130,value.camera_transform);
    }
    return value;
}

void PS4_SYSV_ABI Follow(void* object,float delta) {
    const auto call=calls.fetch_add(1,std::memory_order_relaxed)+1;
    // Never wait for another guest thread or for report serialization.
    bool owner=enabled.load(std::memory_order_relaxed) &&
               !observing.test_and_set(std::memory_order_acquire);
    if (!owner) {skipped.fetch_add(1,std::memory_order_relaxed); original(object,delta); return;}
    if (std::isfinite(delta) && delta>0) {
        delta_min.store(std::min(delta_min.load(std::memory_order_relaxed),delta),std::memory_order_relaxed);
        delta_max.store(std::max(delta_max.load(std::memory_order_relaxed),delta),std::memory_order_relaxed);
    } else ++invalid_delta;
    const auto now=GetTickCount64();
    const auto index=count.load(std::memory_order_relaxed);
    const bool sample=index<snapshots.size() && now>=next_sample;
    Snapshot value;
    if (sample) value=Observe(object,delta,call,now);
    // The native update always executes once with the unmodified arguments.
    original(object,delta);
    if (sample) {
        float after{};
        if (value.lens_valid && Read(value.lens,0xc,after) && std::isfinite(after))
            value.post_radians=after;
        value.follow_after_valid=ReadFollow(value.controller,value.follow_after);
        snapshots[index]=value;
        count.store(index+1,std::memory_order_release);
        next_sample=now+50;
    }
    observing.clear(std::memory_order_release);
}
}

void Initialize(std::span<unsigned char> image,std::string_view serial,std::string_view version) {
    const auto* requested=_wgetenv(L"PS4RT_PERF_CAPTURE_DIR");
    if (!requested || !*requested || enabled.load(std::memory_order_relaxed)) return;
    if (!ProfileMatches(image,serial,version)) {
        std::puts("Camera probe: executable signature refused; native camera unchanged"); return;
    }
    try {
        report_path=std::filesystem::path(requested)/
            (L"camera-follow-"+std::to_wstring(GetCurrentProcessId())+L"-"+
             std::to_wstring(GetTickCount64())+L".json");
        base=reinterpret_cast<std::uintptr_t>(image.data());
        loaded_image=image;
        if (!hooks.Install({image.data()+FollowUpdateAddress},{reinterpret_cast<const void*>(&Follow)})) {
            std::puts("Camera probe: native observation hook unavailable; camera unchanged"); return;
        }
        original=reinterpret_cast<FollowFn>(hooks.Original(0));
        if (Signature(image,ControlSwitchAddress,ControlSwitchSignature) &&
            switch_hook.Install({image.data()+ControlSwitchAddress},{reinterpret_cast<const void*>(&ControlSwitch)})) {
            original_switch=reinterpret_cast<SwitchFn>(switch_hook.Original(0));
            std::puts("Leader probe: observing native control switches; exploration controls unchanged");
        }
        started=GetTickCount64(); next_sample=started;
        enabled.store(true,std::memory_order_release);
        std::puts("Camera probe: read-only native-follow capture enabled; no camera or leader modification");
    } catch (const std::exception& error) {
        std::printf("Camera probe: initialization refused (%s)\n",error.what());
    }
}

void Report() {
    if (!enabled.exchange(false,std::memory_order_acq_rel)) return;
    // A native update can still be in flight when the custom menu exits.
    // Serialize the immutable, release-published prefix without waiting for it.
    // The last unfinished observation may be omitted; completed data is durable.
    const auto published=count.load(std::memory_order_acquire);
    try {
        std::filesystem::create_directories(report_path.parent_path());
        std::ofstream out(report_path,std::ios::binary|std::ios::trunc);
        out.exceptions(std::ios::badbit|std::ios::failbit);
        out<<std::setprecision(9)
           <<"{\n  \"schema\": 3,\n  \"purpose\": \"Read-only native-follow and control-switch observations\",\n"
           <<"  \"title_id\": \"CUSA03219\",\n  \"app_ver\": \"01.00\",\n"
           <<"  \"native_delta_units\": \"Unverified; do not assume seconds\",\n"
           <<"  \"raw_follow_storage\": \"Three float4 slots at controller+0x140, +0x160, +0x180; padding excluded\",\n"
           <<"  \"raw_transform_storage\": \"12 float words at actor+0x70 and camera+0x130; no transform is modified\",\n"
           <<"  \"calls\": "<<calls.load()<<",\n  \"concurrent_or_disabled_calls\": "<<skipped.load()
           <<",\n  \"host_heap_field_reads\": "<<host_reads.load()
           <<",\n  \"rejected_field_reads\": "<<rejected_reads.load()
           <<",\n  \"invalid_delta_count\": "<<invalid_delta.load()
           <<",\n  \"native_delta_min\": "<<(delta_min.load()==std::numeric_limits<float>::max()?0:delta_min.load())
           <<",\n  \"native_delta_max\": "<<delta_max.load()<<",\n  \"sample_capacity\": "<<snapshots.size()
           <<",\n  \"samples\": [\n";
        // Process-local ordinals preserve object identity without exporting addresses.
        std::array<std::uintptr_t,8192> identities{}; std::size_t identities_count=0;
        const auto identity=[&](std::uintptr_t pointer)->std::size_t {
            if (!pointer) return 0;
            for (std::size_t n=0;n<identities_count;++n) if (identities[n]==pointer) return n+1;
            if (identities_count==identities.size()) return 0;
            identities[identities_count++]=pointer; return identities_count;
        };
        for (std::size_t n=0;n<published;++n) {
            const auto& s=snapshots[n];
            if (n) out<<",\n";
            out<<"    {\"ms\": "<<s.milliseconds<<", \"call\": "<<s.call
               <<", \"controller\": "<<identity(s.controller)<<", \"target\": "<<identity(s.target)
               <<", \"camera\": "<<identity(s.camera)<<", \"lens\": "<<identity(s.lens)
               <<", \"controller_vtable_rva\": "<<s.vtable_rva
               <<", \"manager_valid\": "<<(s.manager_valid?"true":"false")
               <<", \"manager_chain_level\": "<<s.manager_chain_level
               <<", \"camera_chain_level\": "<<s.camera_chain_level
               <<", \"manager_matches\": "<<(s.manager_matches?"true":"false")
               <<", \"mode\": "<<s.mode<<", \"previous_mode\": "<<s.previous_mode
               <<", \"script_lock\": "<<(s.script_lock?"true":"false")
               <<", \"native_delta\": "<<s.native_delta
               <<", \"lens_valid\": "<<(s.lens_valid?"true":"false")
               <<", \"lens_radians_before\": "<<s.radians
               <<", \"lens_radians_after\": "<<s.post_radians
               <<", \"camera_aspect\": "<<s.aspect;
            out<<", \"actor_handle\": "<<s.actor_handle<<", \"character_id\": "<<s.character_id
               <<", \"control_valid\": "<<(s.control_valid?"true":"false")
               <<", \"controlled_handle\": "<<s.controlled_handle
               <<", \"party_valid\": "<<(s.party_valid?"true":"false")<<", \"party_ids\": [";
            for(std::size_t i=0;i<s.party_ids.size();++i){if(i)out<<", ";out<<s.party_ids[i];}
            out<<']';
            const auto floats=[&](const char* name,bool valid,const auto& data) {
                out<<", \""<<name<<"\": ";
                if (!valid) {out<<"null"; return;}
                out<<'[';
                for (std::size_t i=0;i<data.size();++i) {if (i) out<<", "; out<<data[i];}
                out<<']';
            };
            floats("follow_before",s.follow_before_valid,s.follow_before);
            floats("follow_after",s.follow_after_valid,s.follow_after);
            floats("actor_transform",s.actor_transform_valid,s.actor_transform);
            floats("camera_transform",s.camera_transform_valid,s.camera_transform);
            out<<'}';
        }
        out<<"\n  ],\n  \"native_control_switches\": [\n";
        const auto completed=switch_count.load(std::memory_order_acquire);
        for(std::size_t i=0;i<completed;++i) {
            const auto& e=switches[i];if(i)out<<",\n";
            out<<"    {\"ms\": "<<e.milliseconds<<", \"caller_rva\": "<<e.caller_rva
               <<", \"old_handle\": "<<e.old_handle<<", \"requested_handle\": "<<e.requested_handle
               <<", \"scene_matches\": "<<(e.scene_matches?"true":"false")
               <<", \"before_valid\": "<<(e.before_valid?"true":"false")<<", \"before\": "<<e.before
               <<", \"after_valid\": "<<(e.after_valid?"true":"false")<<", \"after\": "<<e.after<<'}';
        }
        out<<"\n  ]\n}\n"; out.close();
        std::puts("Camera probe: native-follow JSON written to the performance capture folder");
    } catch (const std::exception& error) {
        std::printf("Camera probe: report failed (%s)\n",error.what());
    }
}
}
