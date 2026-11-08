// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4rt_exploration_leader.h"
#include "ps4rt_camera_probe_profile.h"
#include "ps4rt_hud_hook.h"
#include "ps4rt_leader_call_hook.h"
#include "ps4rt_settings.h"
#include "common/types.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" int ps4rtgpu_overlay_captures_input(void);
extern "C" int runtime_memory_is_mapped(std::uintptr_t,std::uint64_t);
namespace Ps4RtLeader {
namespace {
constexpr std::size_t UpdateAddress=0x197da0,CollectAddress=0x1a0d80;
constexpr std::size_t ReturnCallAddress=0x29baae;
constexpr std::size_t PortalContactAddress=0x1be490,PortalFireAddress=0x1bfc60;
constexpr std::array<unsigned char,24> PortalContactSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
    0x53,0x48,0x81,0xec,0x88,0,0,0,0x48,0x8b,0x1d,0xf5};
constexpr std::array<unsigned char,24> PortalFireSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,
    0x63,0xc6,0x48,0x89,0xfb,0x4c,0x69,0xf8,0xd0,0,0,0};
constexpr std::array<unsigned char,9> PortalCountdownSignature{
    0x43,0xc7,0x44,0x35,0x7c,0x0a,0,0,0};
// Observed caller 0x29bab3 resets the player to party slot zero, independent
// of story-script ChCoPl calls. Guard both its owner and exact call context.
constexpr std::array<unsigned char,24> ReturnOwnerSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
    0x53,0x48,0x81,0xec,0xc8,0,0,0,0x4c,0x8b,0x3d,0x55};
constexpr std::array<unsigned char,24> ReturnContextSignature{
    0x48,0x8d,0x05,0x94,0x7f,0x8e,0,0x49,0x8b,0x4d,0x08,0x31,
    0xf6,0x48,0x8b,0x18,0x48,0x8b,0x41,0x38,0x44,0x8b,0x70,0x54};
constexpr std::array<unsigned char,12> ReturnCallSignature{
    0xe8,0x1d,0x58,0xf0,0xff,0x49,0x8b,0x07,0x48,0x3b,0x45,0xd0};
constexpr std::array<unsigned char,24> UpdateSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
    0x53,0x48,0x83,0xe4,0xe0,0x48,0x81,0xec,0x00,0x03,0,0};
constexpr std::array<unsigned char,24> CollectSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
    0x53,0x48,0x83,0xec,0x78,0x48,0x8b,0x05,0x08,0xfe,0x86,0};
// Nine entries are initialized exactly as in native selector 0x1a4070.
struct Candidate {void* actor{}; std::int16_t id=-1; unsigned char restricted{}; unsigned char padding[5]{};};
static_assert(sizeof(Candidate)==16 && offsetof(Candidate,restricted)==10);
using UpdateFn=void(PS4_SYSV_ABI *)(void*,float);
using CollectFn=int(PS4_SYSV_ABI *)(void*,Candidate*,std::uint32_t,int*,int);
using SwitchFn=void(PS4_SYSV_ABI *)(void*,std::uint32_t,std::uint32_t);
using BlockedFn=bool(PS4_SYSV_ABI *)(void*,int);
using PortalContactFn=void(PS4_SYSV_ABI *)(void*,int);
using PortalFireFn=bool(PS4_SYSV_ABI *)(void*,int);
UpdateFn original{};
CollectFn collect{};
SwitchFn change{};
PortalContactFn portal_contact{};
PortalFireFn portal_fire{};
Ps4RtHud::HookSet<3> hook;
ReturnCallHook return_hook;
std::span<unsigned char> image;
std::uintptr_t base{};
std::atomic<bool> installed{false},pending{false};
std::atomic<Result> last_result{Result::None};
std::atomic<ULONGLONG> interested_until{0};
std::atomic_flag processing=ATOMIC_FLAG_INIT;
SRWLOCK state_lock=SRWLOCK_INIT;
struct State {
    View view; std::uintptr_t root{},actor{}; std::uint32_t handle{}; ULONGLONG at{};
    unsigned gate=1,phase{},flags{};unsigned char manager_flags{};int battles{};
    std::array<std::int16_t,9> party{};
} state;
struct Selection {
    std::uintptr_t manager{},root{},actor{};
    std::uint32_t handle{},default_handle{};
    int id=-1;
    std::array<std::int16_t,9> party{};
};
SRWLOCK selection_lock=SRWLOCK_INIT;
Selection selection;
std::atomic<bool> selection_active{false};
std::atomic<unsigned> suppressed_returns{0};
struct Message {int id=-1; std::uintptr_t root{},actor{}; std::uint32_t handle{}; ULONGLONG at{};} message;
std::atomic<ULONGLONG> next_inspection{0};
std::atomic<unsigned> input_buttons{0};
std::atomic<ULONGLONG> direction_message{0},preference_changed_at{0};
std::atomic<unsigned> preference_revision{0},saved_revision{0};
ULONGLONG next_restore{};
// The native collision callback refreshes MapLink contact every simulation
// update (including its ten-frame contact countdown). Observe that callback,
// never infer a volume exit from elapsed wall time or an idle character.
std::atomic<unsigned long long> simulation_frame{1},last_portal_contact{0};
std::atomic<std::uintptr_t> guarded_portals{0};
std::atomic<std::uintptr_t> guarded_actor{0};
std::atomic<std::uint32_t> guarded_handle{0};
std::atomic<unsigned long long> portal_refusals{0},portal_arms{0};
std::array<std::atomic<std::uint64_t>,2> blocked_portals{};
struct PortalGuard {
    std::uintptr_t root{},actor{},map{};
    std::uint32_t handle{};
    std::array<float,3> position{};
    ULONGLONG at{};
} portal_guard;
struct Timing {
    std::atomic<unsigned long long> calls{0},ticks{0},maximum{0};
    void Add(unsigned long long elapsed) {
        ++calls;ticks.fetch_add(elapsed,std::memory_order_relaxed);
        auto before=maximum.load(std::memory_order_relaxed);
        while(before<elapsed && !maximum.compare_exchange_weak(before,elapsed,std::memory_order_relaxed)){}
    }
} process_timing,return_timing;
std::atomic<unsigned long long> native_queries{0};
std::atomic<unsigned long long> rejected_reads{0};
bool measure{};
struct Measure {
    Timing& timing;LARGE_INTEGER start{};
    explicit Measure(Timing& value):timing(value){if(measure)QueryPerformanceCounter(&start);}
    ~Measure(){if(measure){LARGE_INTEGER end;QueryPerformanceCounter(&end);timing.Add(end.QuadPart-start.QuadPart);}}
};
void Prefer(int id) {
    Ps4RtSettings::Get().exploration_leader=id;
    preference_changed_at=GetTickCount64();
    preference_revision.fetch_add(1,std::memory_order_release);
}

bool Readable(std::uintptr_t at,std::size_t bytes) {
    if(!at || bytes>UINTPTR_MAX-at)return false;
    // The loader span owns executable/BSS storage for the whole session. All
    // observed scene/party/actor fields live in runtime-registered guest maps.
    // VirtualQuery on these large Windows views can cost milliseconds per
    // FIELD (owner capture: 31 ms per return check). Use the same generation-
    // aware mapping registry as the GPU and camera probe, never cache a stale
    // page permission and never accept an arbitrary host-heap pointer.
    if(at>=base && at-base<=image.size() && bytes<=image.size()-(at-base))return true;
    const bool mapped=runtime_memory_is_mapped(at,bytes)!=0;
    if(!mapped)rejected_reads.fetch_add(1,std::memory_order_relaxed);
    return mapped;
}
template<class T> bool Read(std::uintptr_t object,std::size_t offset,T& value) {
    if(offset>UINTPTR_MAX-object || !Readable(object+offset,sizeof value))return false;
    std::memcpy(&value,reinterpret_cast<void*>(object+offset),sizeof value);return true;
}
bool Position(std::uintptr_t actor,std::array<float,3>& result) {
    // Translation column of the observed native 3x4 actor transform.
    for(unsigned i=0;i<3;++i)
        if(!Read(actor,0x7c+i*16,result[i]) || !std::isfinite(result[i]))return false;
    return true;
}
void ClearPortalGuard(){guarded_portals.store(0,std::memory_order_release);}
void ArmPortalGuard(const State& current) {
    ClearPortalGuard();
    PortalGuard value;value.root=current.root;value.actor=current.actor;value.handle=current.handle;
    value.at=GetTickCount64();
    if(!Read(current.root,0x186a8,value.map) || !value.map || !Position(current.actor,value.position))return;
    portal_guard=value;
    guarded_actor=current.actor;guarded_handle=current.handle;
    for(auto& mask:blocked_portals)mask=0;
    last_portal_contact=simulation_frame.load(std::memory_order_relaxed);
    guarded_portals.store(current.root+0xaec0,std::memory_order_release);
    ++portal_arms;
}
void UpdatePortalGuard(const State& current,ULONGLONG now) {
    if(!guarded_portals.load(std::memory_order_acquire))return;
    std::uintptr_t map{};std::array<float,3> position{};
    if(current.root!=portal_guard.root || current.actor!=portal_guard.actor || current.handle!=portal_guard.handle ||
       !Read(current.root,0x186a8,map) || map!=portal_guard.map) {ClearPortalGuard();return;}
    if(now<portal_guard.at || now-portal_guard.at<1000 || !Position(current.actor,position))return;
    const auto frame=simulation_frame.load(std::memory_order_relaxed);
    const auto contact=last_portal_contact.load(std::memory_order_acquire);
    // Two complete updates with no native overlap notification, plus movement
    // away from the switch position. A held overlap cannot expire by timeout.
    // 25 world units is a conservative exit margin, not a trigger-size guess.
    double distance2=0;
    for(unsigned i=0;i<3;++i){const double delta=double(position[i])-portal_guard.position[i];distance2+=delta*delta;}
    if(frame<=contact || frame-contact<3 || distance2<25.0*25.0)return;
    // The native timed-entry mode may still request a transition for ten
    // frames AFTER contact ends. Drain that native countdown under the guard
    // so releasing it cannot execute a stale request just outside the volume.
    for(unsigned bank=0;bank<blocked_portals.size();++bank) {
        auto bits=blocked_portals[bank].load(std::memory_order_acquire);
        while(bits) {
            const unsigned index=bank*64+std::countr_zero(bits);int countdown{};
            if(!Read(current.root+0xaec0,0x7c+index*0xd0,countdown) || countdown>0)return;
            bits&=bits-1;
        }
    }
    ClearPortalGuard();
}
void PS4_SYSV_ABI PortalContact(void* portals,int index) {
    if(index>=0 && index<128 && guarded_portals.load(std::memory_order_acquire)==reinterpret_cast<std::uintptr_t>(portals))
        last_portal_contact.store(simulation_frame.load(std::memory_order_relaxed),std::memory_order_release);
    portal_contact(portals,index);
}
bool Inside(std::uintptr_t at,std::size_t bytes=1) {
    return at>=base && at-base<=image.size() && bytes<=image.size()-(at-base);
}
bool Root(std::uintptr_t object,std::uintptr_t& root) {
    std::uintptr_t table{};std::int64_t delta{};
    if(!Read(object,0,table) || table<base+24 || !Inside(table) || !Read(table-24,0,delta) ||
       delta< -0x200000 || delta>0x200000 || (delta<0 && object<std::uint64_t(-delta)) ||
       (delta>0 && object>UINTPTR_MAX-std::uint64_t(delta)))return false;
    root=delta<0?object-std::uint64_t(-delta):object+std::uint64_t(delta);return true;
}
bool Party(std::array<std::int16_t,9>& ids) {
    std::uintptr_t party{};
    if(!Read(base,0xb839f8,party) || !party)return false;
    for(unsigned slot=0;slot<ids.size();++slot)
        if(!Read(party,0x54+0x21f80+slot*4,ids[slot]) || ids[slot]<-1 || ids[slot]>13)return false;
    return true;
}
// These are the native manager's ready phase, spawning queue, scene lock and
// camera/script ownership checks. Unknown or transitional state fails closed.
bool Context(void* manager,State& current,bool allow_switch_cooldown=false) {
    const auto control=reinterpret_cast<std::uintptr_t>(manager);
    std::uintptr_t scene{},owner{},root{},scene_root{},linked{},modal{},battle_system{},camera_controller{},camera_target{};
    std::int64_t spawning{};int camera_mode{},transition{};
    unsigned char locked{};
    if(!Read(base,Ps4RtCameraProbe::SceneGlobal,scene) || !Read(scene,0x40,owner) ||
       !Root(owner,scene_root) || !Root(control,root) || root!=scene_root ||
       !Read(root,0x18,linked) || linked!=control)return false;
    current.gate=2;
    if(!Read(control,0x48,current.phase) || current.phase!=2 ||
       !Read(control,0x1f0,spawning) || spawning!=0)return false;
    current.gate=3;
    if(!Read(root,0x1858c,current.battles) || current.battles!=0 ||
       !Read(root,0x18684,current.flags) || (current.flags&0x200) ||
       !Read(control,0x4ad,current.manager_flags) || (current.manager_flags&(allow_switch_cooldown?0x02:0x12)) ||
       !Read(base,0xb83ab8,modal) || modal || !Read(base,0xb83aa0,battle_system) || !battle_system)return false;
    current.gate=4;
    if(!Read(owner,0x28,camera_controller) || !camera_controller ||
       !Read(owner,0x70,camera_mode) || camera_mode!=0 ||
       !Read(owner,0x78,transition) || transition!=-1 ||
       !Read(owner,0x8c,locked) || locked || !Read(root,0x186a0,current.handle) || !current.handle ||
       !Read(camera_controller,0xa8,camera_target) || !camera_target)return false;
    current.gate=5;std::uint32_t target_handle{};
    if(!Read(camera_target,0x54,target_handle) || target_handle!=current.handle)return false;
    current.root=root;current.actor=camera_target;current.gate=0;
    return true;
}
bool PS4_SYSV_ABI PortalFire(void* portals,int index) {
    const auto address=reinterpret_cast<std::uintptr_t>(portals);
    if(index>=0 && index<128 && Ps4RtSettings::Get().exploration_leader_enabled &&
       guarded_portals.load(std::memory_order_acquire)==address) {
        // The separate script CChMa route is untouched. Even on this route,
        // let scripted cameras, battle and explicit control changes take over.
        std::uintptr_t manager{};State current;
        if(Read(address-0xaec0,0x18,manager) && Context(reinterpret_cast<void*>(manager),current,true) &&
           current.root==address-0xaec0 && current.actor==guarded_actor.load() && current.handle==guarded_handle.load()) {
            blocked_portals[index/64].fetch_or(std::uint64_t(1)<<(index%64),std::memory_order_relaxed);
            portal_refusals.fetch_add(1,std::memory_order_relaxed);
            return false; // Before native trigger flags, scripts or map requests are written.
        }
        ClearPortalGuard();
    }
    return portal_fire(portals,index);
}
void ClearSelection() {
    selection_active.store(false,std::memory_order_release);
}
bool Actor(const Candidate&,const std::array<std::int16_t,9>&,std::uint32_t&);
bool SelectedContext(void* manager,const Selection& chosen) {
    State current;std::array<std::int16_t,9> party{};std::uint32_t handle{},removing{},removed{},flags{};
    std::uintptr_t status{};std::int16_t id{};
    // Native switch cooldown bit 0x10 is expected just after our own transfer;
    // it is not a scripted control lock. Every other context guard still applies.
    return chosen.manager==reinterpret_cast<std::uintptr_t>(manager) && Context(manager,current,true) &&
        current.root==chosen.root && current.actor==chosen.actor && current.handle==chosen.handle &&
        Party(party) && party==chosen.party &&
        Read(chosen.actor,0x4670,removing) && !removing && Read(chosen.actor,0x4674,removed) && !removed &&
        Read(chosen.actor,0x64,flags) && !(flags&1) &&
        Read(chosen.actor,0x54,handle) && handle==chosen.handle &&
        Read(chosen.actor,0x5a00,status) && Read(status,8,id) && id==chosen.id;
}
bool ReadSelection(Selection& chosen) {
    if(!selection_active.load(std::memory_order_acquire) || !TryAcquireSRWLockShared(&selection_lock))return false;
    chosen=selection;const bool active=selection_active.load(std::memory_order_acquire);
    ReleaseSRWLockShared(&selection_lock);return active;
}
void PS4_SYSV_ABI AutomaticReturn(void* manager,std::uint32_t old_handle,std::uint32_t requested) {
    Measure measurement(return_timing);
    Selection chosen;
    if(!Ps4RtSettings::Get().exploration_leader_enabled)ClearSelection();
    if(ReadSelection(chosen)) {
        if(!SelectedContext(manager,chosen))ClearSelection();
        else if(old_handle==chosen.handle && requested==chosen.default_handle && requested!=chosen.handle) {
            suppressed_returns.fetch_add(1,std::memory_order_relaxed);
            return;
        }
    }
    // This wrapper belongs to one automatic-return call site only. All other
    // native switches, including scripts, battle and recovery, use their original path.
    change(manager,old_handle,requested);
}
bool KeepSelection(void* manager,const State& current,std::uint32_t default_handle) {
    if(!default_handle || !TryAcquireSRWLockExclusive(&selection_lock))return false;
    selection={reinterpret_cast<std::uintptr_t>(manager),current.root,current.actor,current.handle,default_handle,current.view.current,current.party};
    suppressed_returns=0;
    selection_active.store(current.handle!=default_handle,std::memory_order_release);
    ReleaseSRWLockExclusive(&selection_lock);return true;
}
bool Actor(const Candidate& entry,const std::array<std::int16_t,9>& party,std::uint32_t& handle) {
    if(!entry.actor || entry.id<0 || entry.id>5 || entry.restricted ||
       std::find(party.begin(),party.end(),entry.id)==party.end())return false;
    const auto actor=reinterpret_cast<std::uintptr_t>(entry.actor);
    std::uintptr_t status{},table{},blocked{};std::int16_t id{};
    if(!Read(actor,0x54,handle) || !handle || !Read(actor,0x5a00,status) ||
       !Read(status,8,id) || id!=entry.id || !Read(actor,0,table) || !Inside(table,0x3a8) ||
       !Read(table,0x3a0,blocked) || !Inside(blocked))return false;
    // Same native virtual eligibility check as 0x1a4070 with skip flags false.
    return !reinterpret_cast<BlockedFn>(blocked)(entry.actor,1);
}
State InspectNative(void* manager,std::array<Candidate,9>& entries) {
    State current;current.at=GetTickCount64();
    std::array<std::int16_t,9> party{};
    if(!Context(manager,current))return current;
    current.gate=6;if(!Party(party))return current;
    current.party=party;
    current.gate=7;
    int nearest=-1;
    native_queries.fetch_add(1,std::memory_order_relaxed);
    const int native_current=collect(manager,entries.data(),current.handle,&nearest,0);
    if(native_current<0 || native_current>=int(entries.size()) ||
       reinterpret_cast<std::uintptr_t>(entries[native_current].actor)!=current.actor)return current;
    unsigned counts[6]{};
    for(const auto& entry:entries)if(entry.id>=0 && entry.id<6 && entry.actor)++counts[entry.id];
    for(const auto& entry:entries) {
        std::uint32_t handle{};
        if(entry.id>=0 && entry.id<6 && counts[entry.id]==1 && Actor(entry,party,handle)) {
            current.view.available_mask|=1u<<entry.id;
            if(handle==current.handle && reinterpret_cast<std::uintptr_t>(entry.actor)==current.actor)
                current.view.current=entry.id;
        }
    }
    current.view.ready=current.view.current>=0 && (current.view.available_mask&(current.view.available_mask-1));
    current.gate=current.view.ready?0:8;
    return current;
}
void Publish(State current) {
    if(!TryAcquireSRWLockExclusive(&state_lock))return;
    state=current;
    ReleaseSRWLockExclusive(&state_lock);
}
void Finish(Result result,const Message& request,std::uint32_t after) {
    last_result.store(result,std::memory_order_release);
    pending.store(false,std::memory_order_release);
    std::printf("Exploration leader: character %d, handle %u -> %u: %s\n",request.id,request.handle,after,
        result==Result::Applied?"native switch applied":"request refused by current scene or native control");
}
void Process(void* manager,float delta) {
    if(!installed.load(std::memory_order_acquire) || !std::isfinite(delta) || delta<=0)return;
    if(!Ps4RtSettings::Get().exploration_leader_enabled) {
        ClearSelection();ClearPortalGuard();direction_message=0;
        if(pending.exchange(false))last_result=Result::None;
        return;
    }
    const auto now=GetTickCount64();
    const bool queued=pending.load(std::memory_order_acquire);
    const bool captured=ps4rtgpu_overlay_captures_input()!=0;
    const bool directional=direction_message.load(std::memory_order_acquire)!=0;
    const int preferred=Ps4RtSettings::Get().exploration_leader.load();
    const bool inspect=now<=interested_until.load();
    if((!queued && !directional && preferred<0 && !inspect) ||
       (now<next_inspection && !directional && (!queued || captured)))return;
    if(processing.test_and_set(std::memory_order_acquire))return;
    struct Release {~Release(){processing.clear(std::memory_order_release);}} release;
    Measure measurement(process_timing);
    next_inspection=now+100;
    Selection chosen;
    if(ReadSelection(chosen) && !SelectedContext(manager,chosen))ClearSelection();
    // Maintaining a leader must never call the native roster collector or
    // virtual eligibility methods every frame/inspection tick. They are needed
    // only for an actual switch or an open selector, not ordinary movement.
    State cheap;std::uintptr_t status{};std::int16_t current_id=-1;
    const bool free=Context(manager,cheap,true) && Party(cheap.party) &&
        Read(cheap.actor,0x5a00,status) && Read(status,8,current_id) && current_id>=0 && current_id<=5;
    if(free)UpdatePortalGuard(cheap,now);
    else ClearPortalGuard();
    const auto direction=direction_message.exchange(0,std::memory_order_acq_rel);
    const auto direction_at=direction>>2;
    const bool cycle=direction && !captured && free && now>=direction_at && now-direction_at<=250;
    const bool restore=!queued && !cycle && !captured && free && preferred>=0 && preferred<=5 &&
        current_id!=preferred && now>=next_restore &&
        std::find(cheap.party.begin(),cheap.party.end(),preferred)!=cheap.party.end();
    const bool rearm=!queued && !cycle && !captured && free && preferred==current_id && preferred>=0 &&
        preferred!=cheap.party[0] && !selection_active.load(std::memory_order_acquire) && now>=next_restore;
    if(restore || rearm)next_restore=now+1000;
    if(!queued && !cycle && !restore && !rearm && !inspect)return;
    if(queued && captured && !inspect)return;
    std::array<Candidate,9> entries{};
    auto current=InspectNative(manager,entries);
    // Bounded diagnostics for a rejected state, never a continuous frame log.
    static unsigned logged=0,last_gate=~0u,last_mask=~0u;
    if(logged<24 && (last_gate!=current.gate || last_mask!=current.view.available_mask)) {
        std::printf("Exploration leader: gate=%u, phase=%u, battles=%d, scene_flags=%x, control_flags=%x, candidates=%x, current=%d\n",
            current.gate,current.phase,current.battles,current.flags,unsigned(current.manager_flags),current.view.available_mask,current.view.current);
        ++logged;last_gate=current.gate;last_mask=current.view.available_mask;
    }
    Publish(current);
    if(rearm && current.view.ready) {
        std::uint32_t default_handle{};
        for(const auto& entry:entries)if(entry.id==current.party[0])
            Read(reinterpret_cast<std::uintptr_t>(entry.actor),0x54,default_handle);
        if(KeepSelection(manager,current,default_handle))ArmPortalGuard(current);
        return;
    }
    Message request;
    if(queued) {
        if(!TryAcquireSRWLockShared(&state_lock))return;
        request=message;ReleaseSRWLockShared(&state_lock);
    } else if(cycle || restore) {
        int target=preferred;
        if(cycle) {
            const int step=(direction&3)==1?-1:1;
            int slot=0;
            for(int i=0;i<9;++i)if(current.party[i]==current.view.current){slot=i;break;}
            target=-1;
            for(int n=1;n<=9;++n) {
                const int id=current.party[(slot+9+step*n)%9];
                if(id>=0 && id<=5 && id!=current.view.current && (current.view.available_mask&(1u<<id))) {target=id;break;}
            }
        }
        request={target,current.root,current.actor,current.handle,now};
    } else return;
    if(now<request.at || now-request.at>10000 || !current.view.ready || current.root!=request.root ||
       current.actor!=request.actor || current.handle!=request.handle ||
       request.id<0 || request.id>5 || !(current.view.available_mask&(1u<<request.id))) {
        if(queued || cycle)Finish(Result::Refused,request,current.handle);
        return;
    }
    // UI selection is a message, never an actor write on the render thread.
    // It is consumed once, after Resume, with fresh native eligibility checks.
    if(captured)return;
    std::uint32_t selected{},default_handle{};
    for(const auto& entry:entries) {
        if(entry.id==request.id)Read(reinterpret_cast<std::uintptr_t>(entry.actor),0x54,selected);
        if(entry.id==current.party[0])Read(reinterpret_cast<std::uintptr_t>(entry.actor),0x54,default_handle);
    }
    State verified;
    if(!selected || !default_handle || selected==current.handle || !Context(manager,verified) || verified.root!=current.root ||
       verified.handle!=current.handle || verified.actor!=current.actor) {
        if(!restore)Finish(Result::Refused,request,verified.handle);
        return;
    }
    // The shared battle/script routine updates the controlled actor, followers,
    // action controllers and camera. Only the identified automatic slot-zero
    // recall is suppressed for this selection, never story/battle/scene changes.
    ClearSelection();
    change(manager,current.handle,selected);
    std::uint32_t after{};
    const bool applied=Read(current.root,0x186a0,after) && after==selected;
    if(applied) {
        current.view.current=request.id;current.handle=selected;
        for(const auto& entry:entries)if(entry.id==request.id)current.actor=reinterpret_cast<std::uintptr_t>(entry.actor);
        if(!KeepSelection(manager,current,default_handle))
            std::puts("Exploration leader: automatic-return exemption unavailable; native control remains enabled");
        if(current.view.current!=current.party[0])ArmPortalGuard(current);
        else ClearPortalGuard();
        Publish(current);
        if(!restore)Prefer(request.id);
    }
    if(applied || !restore)Finish(applied?Result::Applied:Result::Refused,request,after);
}
void PS4_SYSV_ABI Update(void* manager,float delta) {
    simulation_frame.fetch_add(1,std::memory_order_relaxed);
    original(manager,delta);
    Process(manager,delta);
}
bool Matches(std::span<const unsigned char> bytes,std::string_view serial,std::string_view version) {
    using namespace Ps4RtCameraProbe;
    return ProfileMatches(bytes,serial,version) &&
        Signature(bytes,UpdateAddress,UpdateSignature) && Signature(bytes,CollectAddress,CollectSignature) &&
        Signature(bytes,0x29ae30,ReturnOwnerSignature) && Signature(bytes,0x29ba5d,ReturnContextSignature) &&
        Signature(bytes,ReturnCallAddress,ReturnCallSignature) &&
        Signature(bytes,PortalContactAddress,PortalContactSignature) && Signature(bytes,PortalFireAddress,PortalFireSignature) &&
        Signature(bytes,0x1be70e,PortalCountdownSignature) &&
        Signature(bytes,ControlSwitchAddress,ControlSwitchSignature);
}
}
bool Available(){return installed.load(std::memory_order_acquire);}
void Inspect(){interested_until=GetTickCount64()+1000;}
View GetView() {
    View result;
    if(!TryAcquireSRWLockShared(&state_lock))return result;
    result=state.view;
    result.result=last_result.load(std::memory_order_acquire);
    if(GetTickCount64()-state.at>1500)result.ready=false;
    ReleaseSRWLockShared(&state_lock);return result;
}
bool Request(int character) {
    if(!Available() || !Ps4RtSettings::Get().exploration_leader_enabled || character<0 || character>5 || !TryAcquireSRWLockExclusive(&state_lock))return false;
    const auto now=GetTickCount64();
    const bool accepted=state.view.ready && now-state.at<=1500 && character!=state.view.current &&
        (state.view.available_mask&(1u<<character)) && !pending.load(std::memory_order_acquire);
    if(accepted) {
        message={character,state.root,state.actor,state.handle,now};
        last_result.store(Result::Pending,std::memory_order_release);pending.store(true,std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&state_lock);return accepted;
}
void Input(std::uint32_t buttons,bool captured) {
    constexpr unsigned Left=0x80,Right=0x20;
    const unsigned held=captured || !Ps4RtSettings::Get().exploration_leader_enabled?0:buttons&(Left|Right);
    const unsigned before=input_buttons.exchange(held,std::memory_order_relaxed);
    if(captured){direction_message.store(0,std::memory_order_release);return;}
    if(!Available() || !held || held==(Left|Right) || (held&before))return;
    Cycle(held==Left?-1:1,false);
}
void Cycle(int direction,bool captured) {
    if(captured){direction_message.store(0,std::memory_order_release);return;}
    if(!Available()||!Ps4RtSettings::Get().exploration_leader_enabled||!direction)return;
    direction_message.store((GetTickCount64()<<2)|(direction<0?1:2),std::memory_order_release);
}
void FlushPreference() {
    const unsigned revision=preference_revision.load(std::memory_order_acquire);
    if(revision==saved_revision.load() || GetTickCount64()-preference_changed_at.load()<300)return;
    Ps4RtSettings::Save();saved_revision.store(revision,std::memory_order_release);
}
void Report() {
    // Final save also covers a normal exit inside the debounce interval.
    const auto revision=preference_revision.load(std::memory_order_acquire);
    if(revision!=saved_revision.load()) {Ps4RtSettings::Save();saved_revision=revision;}
    if(!measure)return;
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    const auto print=[&](const char* label,const Timing& value) {
        const double unit=1000.0/double(frequency.QuadPart);
        const auto count=value.calls.load();
        std::printf("Exploration leader performance: %s calls=%llu total_ms=%.3f mean_ms=%.6f max_ms=%.3f\n",
            label,count,value.ticks.load()*unit,count?value.ticks.load()*unit/count:0,value.maximum.load()*unit);
    };
    print("update",process_timing);print("automatic-return",return_timing);
    std::printf("Exploration leader performance: native_queries=%llu suppressed_returns=%u preferred=%d rejected_reads=%llu memory_validation=runtime-registry\n",
        native_queries.load(),suppressed_returns.load(),Ps4RtSettings::Get().exploration_leader.load(),rejected_reads.load());
    std::printf("Exploration leader portal guard: arms=%llu blocked_requests=%llu active=%d\n",
        portal_arms.load(),portal_refusals.load(),guarded_portals.load()!=0);
}
void Initialize(std::span<unsigned char> bytes,std::string_view serial,std::string_view version) {
    if(Available() || !Matches(bytes,serial,version))return;
    if(!hook.Install({bytes.data()+UpdateAddress,bytes.data()+PortalContactAddress,bytes.data()+PortalFireAddress},
                     {reinterpret_cast<const void*>(&Update),reinterpret_cast<const void*>(&PortalContact),reinterpret_cast<const void*>(&PortalFire)}))return;
    if(!return_hook.Install(bytes.data()+ReturnCallAddress,bytes.data()+Ps4RtCameraProbe::ControlSwitchAddress,
                           reinterpret_cast<const void*>(&AutomaticReturn))) {
        if(!hook.Remove())ExitProcess(26);
        return;
    }
    image=bytes;base=reinterpret_cast<std::uintptr_t>(bytes.data());
    original=reinterpret_cast<UpdateFn>(hook.Original(0));
    portal_contact=reinterpret_cast<PortalContactFn>(hook.Original(1));
    portal_fire=reinterpret_cast<PortalFireFn>(hook.Original(2));
    collect=reinterpret_cast<CollectFn>(base+CollectAddress);
    change=reinterpret_cast<SwitchFn>(base+Ps4RtCameraProbe::ControlSwitchAddress);
    measure=std::getenv("PS4RT_PERF_CAPTURE_DIR")!=nullptr;
    installed.store(true,std::memory_order_release);
    std::puts("Exploration leader: D-pad left/right or O/P enabled during exploration; preference persists across scenes and sessions");
    std::puts("Exploration leader: native MapLink overlap guard enabled after leader changes; scripted map requests remain native");
}
}
