// SPDX-License-Identifier: GPL-2.0-or-later
// Owned fixtures only: no game launch or game instructions are executed.
#include <windows.h>
static unsigned windows_memory_queries{};
static SIZE_T CountVirtualQuery(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T length) {
    ++windows_memory_queries;return ::VirtualQuery(address,info,length);
}
#define VirtualQuery CountVirtualQuery
#include "../gpu/shim/ps4rt_exploration_leader.cpp"
#include <cassert>
#include <thread>
#include <vector>

namespace {
using namespace Ps4RtLeader;
void* expected_manager{};
float expected_delta=1;
unsigned updates{},queries{},changes{},formations{};
bool refuse_formation{};
unsigned portal_contacts{},portal_fires{};
bool capture{},refuse{},duplicate{},restricted{},unknown{},no_current{};
std::array<std::vector<unsigned char>,6> actors,statuses;
std::uintptr_t owner_address{},controller_address{},native_table{};
std::vector<std::pair<std::uintptr_t,std::size_t>> mapped_ranges;
void Map(std::vector<unsigned char>& value) {
    mapped_ranges.emplace_back(reinterpret_cast<std::uintptr_t>(value.data()),value.size());
}
template<class T> void Store(std::uintptr_t object,std::size_t offset,T value) {
    assert(Readable(object+offset,sizeof value));
    std::memcpy(reinterpret_cast<void*>(object+offset),&value,sizeof value);
}
void PS4_SYSV_ABI NativeUpdate(void* object,float delta) {
    assert(object==expected_manager && delta==expected_delta);++updates;
}
int PS4_SYSV_ABI NativeCollect(void* manager,Candidate* entries,std::uint32_t handle,int* nearest,int flags) {
    assert(manager==expected_manager && nearest && flags==0);++queries;*nearest=-1;
    int current=-1;
    for(int id=0;id<6;++id) {
        entries[id].actor=actors[id].data();entries[id].id=id;
        if(handle==42u+id*2)current=id;
    }
    if(duplicate)entries[6]=entries[1];
    if(restricted)entries[1].restricted=1;
    if(unknown)entries[1].id=6;
    return no_current?-1:current;
}
void PS4_SYSV_ABI NativeChange(void* manager,std::uint32_t old_handle,std::uint32_t next) {
    assert(manager==expected_manager && !capture);
    std::uint32_t before{};assert(Read(owner_address,0x186a0,before) && old_handle==before);
    ++changes;
    if(!refuse) {
        // Match the real transfer: actor references and control ownership move,
        // independently of the general controller's persistent movement state.
        for(auto& value:actors) {
            const auto actor=reinterpret_cast<std::uintptr_t>(value.data());std::uint32_t target{};
            if(Read(actor,0x4608,target) && target==old_handle)Store(actor,0x4608,next);
        }
        const auto old_control=reinterpret_cast<std::uintptr_t>(actors[(old_handle-42)/2].data())+0x5b00;
        const auto new_control=reinterpret_cast<std::uintptr_t>(actors[(next-42)/2].data())+0x5b00;
        if(Readable(old_control+0x50,4))Store(old_control,0x50,int(0));
        if(Readable(new_control+0x50,4))Store(new_control,0x50,int(4));
        Store(owner_address,0x186a0,next);
        Store(controller_address,0xa8,reinterpret_cast<std::uintptr_t>(actors[(next-42)/2].data()));
    }
}
FormationVector* PS4_SYSV_ABI NativeFormation(FormationVector* out,void* root,int slot,void* actor,const FormationVector* input) {
    assert(root==reinterpret_cast<void*>(owner_address) && slot==0 && actor==actors[0].data());
    assert(input->x==0 && input->y==0 && input->z==0 && input->w==1);++formations;
    if(!refuse_formation) {
        const auto control=reinterpret_cast<std::uintptr_t>(actor)+0x5b00;
        std::uintptr_t general{};std::uint32_t leader{};
        assert(Read(control,0x48,general) && Read(owner_address,0x186a0,leader));
        Store(general,0x2d8,int(2));Store(general,0x170,leader);
    }
    *out=*input;return out;
}
void PS4_SYSV_ABI NativePortalContact(void*,int){++portal_contacts;}
bool PS4_SYSV_ABI NativePortalFire(void*,int){++portal_fires;return true;}
void Refresh() {next_inspection=0;Inspect();Update(expected_manager,expected_delta);}
void ResetCurrent() {
    Ps4RtSettings::Get().exploration_leader_enabled=true;ClearPortalGuard();
    follower_repair={};
    for(unsigned i=0;i<actors.size();++i)
        Store(reinterpret_cast<std::uintptr_t>(actors[i].data())+0x5b00,0x50,int(i==0?4:0));
    if(actors[0].size()>0x6000) {
        const auto general=reinterpret_cast<std::uintptr_t>(actors[0].data())+0x5c00;
        Store(general,0x2d8,int(0));Store(general,0x170,std::uint32_t(42));
        Store(reinterpret_cast<std::uintptr_t>(actors[0].data()),0x4608,std::uint32_t(42));
    }
    Ps4RtSettings::Get().exploration_leader=-1;ClearSelection();next_restore=0;direction_message=0;input_buttons=0;
    Store(owner_address,0x186a0,std::uint32_t(42));
    Store(controller_address,0xa8,reinterpret_cast<std::uintptr_t>(actors[0].data()));
    Refresh();assert(GetView().ready && GetView().current==0);
}
}
extern "C" int ps4rtgpu_overlay_captures_input(void){return capture;}
extern "C" int runtime_memory_is_mapped(std::uintptr_t address,std::uint64_t bytes) {
    for(const auto& [start,size]:mapped_ranges)
        if(address>=start && address-start<=size && bytes<=size-(address-start))return 1;
    return 0;
}

int main() {
    using namespace Ps4RtLeader;
    constexpr std::size_t bytes=0xb83ac0;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(memory);image={memory,bytes};base=reinterpret_cast<std::uintptr_t>(memory);
    const auto sig=[&](std::size_t at,const auto& values){std::memcpy(memory+at,values.data(),values.size());};
    using namespace Ps4RtCameraProbe;
    sig(FollowUpdateAddress,FollowSignature);sig(0x4fbdb0,LensSignature);sig(0x188e70,ManagerSignature);
    sig(UpdateAddress,UpdateSignature);sig(CollectAddress,CollectSignature);sig(ControlSwitchAddress,ControlSwitchSignature);
    sig(0x29ae30,ReturnOwnerSignature);sig(0x29ba5d,ReturnContextSignature);sig(ReturnCallAddress,ReturnCallSignature);
    sig(FormationAddress,FormationSignature);sig(0x18f2d4,FormationGuardsSignature);sig(0x29f280,FollowMovementSignature);
    sig(0x18bf1b,FollowInitializationSignature);sig(0x25cdc0,FollowSetterSignature);
    sig(PortalContactAddress,PortalContactSignature);sig(PortalFireAddress,PortalFireSignature);
    sig(0x1be70e,PortalCountdownSignature);
    assert(Matches(image,"CUSA03219","01.00"));
    assert(!Matches(image,"CUSA00000","01.00") && !Matches(image,"CUSA03219","01.01"));
    assert(!Matches(image.first(100),"CUSA03219","01.00"));
    for(auto at:{UpdateAddress,CollectAddress,ControlSwitchAddress,std::size_t(0x29ae30),std::size_t(0x29ba5d),ReturnCallAddress,PortalContactAddress,PortalFireAddress,std::size_t(0x1be70e),std::size_t(0x18bf1b),std::size_t(0x25cdc0),FormationAddress,std::size_t(0x18f2d4),std::size_t(0x29f280)}) {
        memory[at]^=1;assert(!Matches(image,"CUSA03219","01.00"));memory[at]^=1;
    }
    Initialize(image,"CUSA03219","01.01");assert(!Available());
    // Two tiny eligibility fixtures return false/true; real game code is not executed.
    const unsigned char allowed[]={0x31,0xc0,0xc3},blocked[]={0xb0,0x01,0xc3};
    std::memcpy(memory+0x1000,allowed,sizeof allowed);std::memcpy(memory+0x1010,blocked,sizeof blocked);
    // Owned getter fixture returns actor's embedded control object (SysV RDI).
    const unsigned char control_getter[]={0x48,0x8d,0x87,0x00,0x5b,0,0,0xc3};
    std::memcpy(memory+0x1020,control_getter,sizeof control_getter);
    DWORD old{};assert(VirtualProtect(memory+0x1000,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),memory+0x1000,4096);
    std::vector<unsigned char> scene(0x80),owner(0x18800),controller(0x200),party(0x22000);
    for(auto* value:{&scene,&owner,&controller,&party})Map(*value);
    const auto ptr=[](auto& v){return reinterpret_cast<std::uintptr_t>(v.data());};
    owner_address=ptr(owner);controller_address=ptr(controller);expected_manager=owner.data()+0x340;
    native_table=base+0x950100;
    Store(base,SceneGlobal,ptr(scene));Store(ptr(scene),0x40,ptr(owner));
    Store(ptr(owner),0,base+0x950000);Store(base,0x950000-24,std::int64_t(0));
    Store(ptr(owner),0x18,reinterpret_cast<std::uintptr_t>(expected_manager));
    Store(ptr(owner),0x340,base+0x950040);Store(base,0x950040-24,std::int64_t(-0x340));
    Store(ptr(owner),0x340+0x48,unsigned(2));Store(ptr(owner),0x28,ptr(controller));
    Store(ptr(owner),0x78,int(-1));Store(base,0xb83aa0,ptr(scene));Store(base,0xb839f8,ptr(party));
    Store(ptr(owner),0x186a8,ptr(scene));
    Store(native_table,0x3a0,base+0x1000);Store(native_table,0x388,base+0x1020);
    for(int i=0;i<9;++i)Store(ptr(party),0x54+0x21f80+i*4,std::int16_t(i<6?i:-1));
    for(int id=0;id<6;++id) {
        actors[id].resize(0x6100);statuses[id].resize(0x20);
        Map(actors[id]);Map(statuses[id]);
        Store(ptr(actors[id]),0,native_table);Store(ptr(actors[id]),0x54,std::uint32_t(42+id*2));
        Store(ptr(actors[id]),0x5a00,ptr(statuses[id]));Store(ptr(statuses[id]),8,std::int16_t(id));
        const auto control=ptr(actors[id])+0x5b00,general=ptr(actors[id])+0x5c00;
        Store(control,0x38,ptr(actors[id]));Store(control,0x48,general);Store(general,8,control);
    }
    original=&NativeUpdate;collect=&NativeCollect;change=&NativeChange;formation=&NativeFormation;installed=true;
    portal_contact=&NativePortalContact;portal_fire=&NativePortalFire;
    ResetCurrent();assert(GetView().available_mask==63 && updates==1);
    // No inspection or native query work during ordinary gameplay without a request.
    interested_until=0;const auto queried=queries;Update(expected_manager,1);assert(queries==queried);
    assert(!Request(-1) && !Request(6) && !Request(0));
    capture=true;assert(Request(1));assert(!Request(2));Refresh();
    assert(changes==0 && GetView().result==Result::Pending);
    capture=false;Refresh();assert(changes==1 && GetView().current==1 && GetView().result==Result::Applied);
    Refresh();assert(changes==1); // No repeated native transfer when already selected.
    assert(Ps4RtSettings::Get().exploration_leader==1);
    ResetCurrent();assert(Request(2));refuse=true;Refresh();refuse=false;
    assert(changes==2 && GetView().result==Result::Refused && GetView().current==0);
    ResetCurrent();capture=true;assert(Request(1));
    message.at=GetTickCount64()-10001;capture=false;Refresh();assert(changes==2 && !pending);
    // Scene, actor, current control and party changes invalidate queued selections.
    for(int kind=0;kind<4;++kind) {
        ResetCurrent();capture=true;assert(Request(1));
        if(kind==0)message.root+=8;
        if(kind==1)message.actor+=8;
        if(kind==2)message.handle+=2;
        if(kind==3)restricted=true;
        capture=false;Refresh();restricted=false;
        assert(changes==2 && !pending && GetView().result==Result::Refused);
    }
    // Native state, scene lock, camera ownership, battle and loading guards.
    struct Guard {std::uintptr_t object;std::size_t offset;std::uint64_t bad;std::size_t size;};
    const auto m=reinterpret_cast<std::uintptr_t>(expected_manager);
    const Guard guards[]={
        {m,0x48,1,4},{m,0x1f0,1,8},{m,0x4ad,2,1},{m,0x4ad,0x10,1},
        {owner_address,0x1858c,1,4},{owner_address,0x18684,0x200,4},
        {owner_address,0x70,1,4},{owner_address,0x78,0,4},{owner_address,0x8c,1,1},
        {base,0xb83ab8,owner_address,8},{base,0xb83aa0,0,8},{owner_address,0x18,0,8}};
    for(auto g:guards) {
        std::uint64_t saved{};std::memcpy(&saved,reinterpret_cast<void*>(g.object+g.offset),g.size);
        std::memcpy(reinterpret_cast<void*>(g.object+g.offset),&g.bad,g.size);
        Refresh();assert(!GetView().ready && !Request(1));
        std::memcpy(reinterpret_cast<void*>(g.object+g.offset),&saved,g.size);
    }
    duplicate=true;Refresh();assert(!(GetView().available_mask&2));duplicate=false;
    restricted=true;Refresh();assert(!(GetView().available_mask&2));restricted=false;
    unknown=true;Refresh();assert(!(GetView().available_mask&2));unknown=false;
    no_current=true;Refresh();assert(!GetView().ready);no_current=false;
    Store(native_table,0x3a0,base+0x1010);Refresh();assert(!GetView().ready);
    Store(native_table,0x3a0,base+0x1000);
    Store(ptr(statuses[1]),8,std::int16_t(6));Refresh();assert(!(GetView().available_mask&2));
    Store(ptr(statuses[1]),8,std::int16_t(1));ResetCurrent();
    state.at=GetTickCount64()-1600;assert(!GetView().ready && !Request(1));
    // Lock contention never blocks a game update or UI request.
    AcquireSRWLockExclusive(&state_lock);
    std::thread reader([&]{assert(!GetView().ready && !Request(1));});reader.join();
    ReleaseSRWLockExclusive(&state_lock);
    // Regression from the owner capture: native selection succeeds, then the
    // actor update calls the slot-zero return twice (cooldown, then success).
    // Suppress that one call site only, without repeatedly reapplying a leader.
    ResetCurrent();assert(Request(3));Refresh();
    const auto switched=changes;
    assert(selection_active && GetView().current==3);
    Store(m,0x4ad,static_cast<unsigned char>(0x10));
    AutomaticReturn(expected_manager,48,42);assert(changes==switched && selection_active);
    Store(m,0x4ad,static_cast<unsigned char>(0));
    for(unsigned i=0;i<100;++i)AutomaticReturn(expected_manager,48,42);
    assert(changes==switched && suppressed_returns==101 && GetView().current==3);
    // Other callers still reach the native routine, including explicit story
    // switches. Their control change invalidates our exemption on the next tick.
    NativeChange(expected_manager,48,42);Refresh();assert(selection_active && GetView().current==3);
    assert(Request(2));Refresh();assert(selection_active);
    assert(Request(0));Refresh();assert(!selection_active && GetView().current==0);
    AutomaticReturn(expected_manager,42,44);assert(!selection_active);
    ResetCurrent();
    // Battle, script camera, loading, roster changes and actor removal must
    // immediately restore the original return behavior, not fight it.
    const Guard return_guards[]={
        {m,0x48,1,4},{m,0x1f0,1,8},{m,0x4ad,2,1},
        {owner_address,0x1858c,1,4},{owner_address,0x18684,0x200,4},
        {owner_address,0x70,1,4},{owner_address,0x78,0,4},{owner_address,0x8c,1,1},
        {base,0xb83ab8,owner_address,8},{owner_address,0x18,0,8},
        {ptr(party),0x54+0x21f80+4,std::uint64_t(-1),2},
        {ptr(actors[3]),0x4670,1,4},{ptr(actors[3]),0x4674,1,4},{ptr(actors[3]),0x64,1,4},
        {ptr(statuses[3]),8,6,2}};
    for(auto g:return_guards) {
        ResetCurrent();assert(Request(3));Refresh();assert(selection_active);
        const auto before=changes;
        std::uint64_t saved{};std::memcpy(&saved,reinterpret_cast<void*>(g.object+g.offset),g.size);
        std::memcpy(reinterpret_cast<void*>(g.object+g.offset),&g.bad,g.size);
        AutomaticReturn(expected_manager,48,42);assert(changes==before+1 && !selection_active);
        std::memcpy(reinterpret_cast<void*>(g.object+g.offset),&saved,g.size);
    }
    ResetCurrent();assert(Request(3));Refresh();
    // Eligibility is queried at transfer time, never called from the hot
    // automatic-return path. An existing valid selection needs only identity checks.
    const auto hot_queries=queries,hot_changes=changes;
    Store(native_table,0x3a0,base+0x1010);AutomaticReturn(expected_manager,48,42);assert(selection_active);
    assert(queries==hot_queries && changes==hot_changes);
    Store(native_table,0x3a0,base+0x1000);ResetCurrent();
    // Regression: the switch already updates actor.follow_target, but Fidel's
    // movement controller may still be idle or targeting his own handle.
    ResetCurrent();assert(Request(3));Refresh();
    const auto fidel=ptr(actors[0]),fidel_control=fidel+0x5b00,fidel_general=fidel+0x5c00;
    Store(fidel,0x4608,std::uint32_t(48));const auto repairs=follower_repairs.load(),formation_before=formations;
    Refresh();std::uint32_t follow{};int movement{};
    assert(Read(fidel_general,0x170,follow) && follow==48);
    assert(Read(fidel_general,0x2d8,movement) && movement==2);
    assert(follower_repairs==repairs+1 && formations==formation_before+1);
    const auto stable_memory_queries=windows_memory_queries;
    for(int i=0;i<100;++i)Refresh();
    assert(follower_repairs==repairs+1 && formations==formation_before+1 && windows_memory_queries==stable_memory_queries);
    // A second leader handover must retarget the movement controller as well.
    assert(Request(2));Refresh();Refresh();assert(Read(fidel_general,0x170,follow) && follow==46);
    assert(Request(3));Refresh();Refresh();assert(Read(fidel_general,0x170,follow) && follow==48);
    // Self-targeting follow state is repaired by native initialization too.
    Store(fidel_general,0x170,std::uint32_t(42));follower_repair.next_attempt=0;Refresh();
    assert(Read(fidel_general,0x170,follow) && follow==48);
    // Preserve script/control ownership, explicit movement and actor retirement.
    Store(fidel_general,0x2d8,int(0));
    const Guard follower_guards[]={
        {fidel_control,0x50,4,4},{fidel_control,0x50,5,4},
        {fidel_general,0x1a0,1,1},{fidel_general,0x1a3,1,1},{fidel_general,0x2e4,0x20,1},
        {fidel_general,0x2d8,1,4},{fidel_general,0x2d8,3,4},
        {fidel_control,0x38,0,8},{fidel_general,8,0,8},
        {fidel,0x4670,1,4},{fidel,0x4674,1,4},{fidel,0x64,1,4}};
    for(const auto& g:follower_guards) {
        std::uint64_t saved{};std::memcpy(&saved,reinterpret_cast<void*>(g.object+g.offset),g.size);
        std::memcpy(reinterpret_cast<void*>(g.object+g.offset),&g.bad,g.size);
        const auto calls=formations;follower_repair.next_attempt=0;Refresh();assert(formations==calls);
        std::memcpy(reinterpret_cast<void*>(g.object+g.offset),&saved,g.size);
    }
    Store(fidel_general,0x2d8,int(2));Store(fidel_general,0x170,std::uint32_t(50));
    auto calls=formations;Refresh();assert(formations==calls);
    Store(fidel_general,0x2d8,int(0));Store(fidel,0x4608,std::uint32_t(50));
    Refresh();assert(formations==calls && Read(fidel,0x4608,follow) && follow==50);
    Store(fidel,0x4608,std::uint32_t(42));Store(owner_address,0x1858c,unsigned(1));
    Refresh();assert(formations==calls);Store(owner_address,0x1858c,unsigned(0));
    ResetCurrent();assert(Request(3));Refresh();refuse_formation=true;calls=formations;
    for(int i=0;i<20;++i)Refresh();assert(formations==calls+1);
    for(int i=0;i<5;++i){follower_repair.next_attempt=0;Refresh();}
    assert(formations==calls+3);refuse_formation=false;
    ResetCurrent();assert(Request(3));Refresh();Refresh();
    assert(Read(fidel_general,0x2d8,movement) && movement==2);
    ResetCurrent();calls=formations;Refresh();assert(formations==calls); // Fidel remains player.
    // One mapped direction edge switches once, wraps in party order, skips
    // unavailable entries and never consumes/modifies native battle input.
    interested_until=0;
    const auto step=[&]{next_inspection=0;Update(expected_manager,1);};
    Input(0x20,false);step();assert(GetView().current==1);
    auto count=changes;Input(0x20,false);step();assert(changes==count);
    Input(0,false);Input(0x80,false);step();assert(GetView().current==0);
    Input(0,false);Input(0x80,false);step();assert(GetView().current==5);
    Input(0,false);Input(0xa0,false);step();assert(GetView().current==5);
    Input(0,true);Input(0x20,false);capture=true;step();capture=false;step();assert(GetView().current==5);
    Input(0,false);Input(0x20,false);Store(owner_address,0x1858c,unsigned(1));count=changes;
    step();assert(changes==count && Ps4RtSettings::Get().exploration_leader==5);
    Store(owner_address,0x1858c,unsigned(0));step();assert(changes==count);
    // O/P routes to the same native queue without modifying sampled gamepad
    // buttons. Capture/focus loss cancels a queued keyboard action.
    // L2/R2 protect both D-pad and O/P, including an already queued edge.
    // Releasing a trigger with D-pad still held must not manufacture a new edge.
    for(auto trigger:{0x100u,0x200u,0x300u}) {
        ResetCurrent();count=changes;
        Input(trigger|0x20,false);Cycle(1,false);assert(!Request(1));step();assert(changes==count);
        Input(0x20,false);step();assert(changes==count);
        Input(0,false);Input(0x20,false);Input(trigger,false);step();assert(changes==count);
        Input(0,false);Input(0x20,false);step();assert(GetView().current==1);
        Input(0,false);Refresh();assert(Request(2));Input(trigger,false);step();assert(!pending && changes==count+1);
        Input(0,false);step();assert(changes==count+1);
    }
    ResetCurrent();Cycle(1,false);step();assert(GetView().current==1);
    Cycle(-1,false);step();assert(GetView().current==0);
    Cycle(-1,false);Cycle(0,true);count=changes;step();assert(changes==count);
    Ps4RtSettings::Get().exploration_leader_enabled=false;
    Cycle(1,false);step();assert(changes==count);
    Ps4RtSettings::Get().exploration_leader_enabled=true;
    ResetCurrent();assert(Request(5));Refresh();count=changes;
    // Refresh intentionally requests an inspector census. End that window
    // before measuring steady gameplay, as in the original direction tests.
    interested_until=0;
    // A stale edge cannot fire after a lengthy transition.
    direction_message=((GetTickCount64()-1000)<<2)|2;step();assert(changes==count);
    // Steady gameplay and repeated recall suppression perform ZERO native
    // roster queries or switches. This catches the performance regression.
    const auto steady_queries=queries;
    const auto steady_windows_queries=windows_memory_queries;
    measure=true;
    for(unsigned i=0;i<1000;++i) {step();AutomaticReturn(expected_manager,52,42);}
    measure=false;
    assert(queries==steady_queries && changes==count && selection_active);
    assert(windows_memory_queries==steady_windows_queries);
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    std::printf("Leader fixture: 1000 steady updates and returns, zero native queries/transfers; update mean %.6f ms, return mean %.6f ms\n",
        process_timing.ticks.load()*1000.0/frequency.QuadPart/process_timing.calls.load(),
        return_timing.ticks.load()*1000.0/frequency.QuadPart/return_timing.calls.load());
    // Preference survives battle/script/loading, but invalid actors are never
    // forced. Once free exploration resumes, reacquire and restore once.
    Store(owner_address,0x1858c,unsigned(1));NativeChange(expected_manager,52,42);step();
    assert(!selection_active && Ps4RtSettings::Get().exploration_leader==5);
    count=changes;step();assert(changes==count);
    Store(owner_address,0x1858c,unsigned(0));next_restore=0;step();assert(GetView().current==5 && changes==count+1);
    NativeChange(expected_manager,52,42);
    Store(ptr(party),0x54+0x21f80+5*4,std::int16_t(-1));next_restore=0;count=changes;step();
    assert(changes==count && Ps4RtSettings::Get().exploration_leader==5);
    Store(ptr(party),0x54+0x21f80+5*4,std::int16_t(5));step();assert(GetView().current==5 && changes==count+1);
    // Model a newly loaded actor/root handle: the old lease is not reused.
    NativeChange(expected_manager,52,42);Store(owner_address,0x70,int(1));count=changes;step();assert(changes==count);
    Store(owner_address,0x70,int(0));next_restore=0;step();assert(GetView().current==5 && changes==count+1);
    // Registry invalidation takes effect immediately: do not retain a host
    // readability cache across unmap/remap, and never read a gap/overflow.
    assert(!Readable(0,8) && !Readable(UINTPTR_MAX-3,8));
    assert(!Readable(reinterpret_cast<std::uintptr_t>(scene.data())+scene.size()-2,8));
    const auto actor_range=std::find_if(mapped_ranges.begin(),mapped_ranges.end(),[&](const auto& range){
        return range.first==reinterpret_cast<std::uintptr_t>(actors[5].data());
    });assert(actor_range!=mapped_ranges.end());
    const auto saved_range=*actor_range;mapped_ranges.erase(actor_range);
    const auto before_unmap=changes;
    AutomaticReturn(expected_manager,52,42);assert(!selection_active && changes==before_unmap+1);
    mapped_ranges.push_back(saved_range);
    // Native portal overlap notifications, not a wall-time timeout, hold the
    // transition gate. The simulation and original collision handler still run.
    ResetCurrent();assert(Request(3));Refresh();
    auto* portals=reinterpret_cast<void*>(owner_address+0xaec0);
    assert(guarded_portals==reinterpret_cast<std::uintptr_t>(portals));
    portal_guard.at=GetTickCount64()-1500;
    interested_until=0;
    auto fires=portal_fires;auto contacts=portal_contacts;
    Refresh(); // Finish one-time initialization, then close selector diagnostics.
    interested_until=0;
    const auto before_guard_queries=queries,before_guard_windows=windows_memory_queries;
    for(unsigned i=0;i<600;++i) {
        PortalContact(portals,5);step();assert(!PortalFire(portals,5));
    }
    assert(portal_contacts==contacts+600 && portal_fires==fires && guarded_portals!=0);
    // Idle outside notification gaps cannot rearm a blocked transition.
    for(unsigned i=0;i<20;++i)step();
    assert(!PortalFire(portals,5));
    // Movement while still overlapping also cannot rearm it.
    Store(ptr(actors[3]),0x7c,100.f);
    PortalContact(portals,5);step();assert(!PortalFire(portals,5));
    Store(owner_address+0xaec0,0x7c+5*0xd0,int(10));
    step();step();assert(guarded_portals!=0); // Pending native timer cannot escape the guard.
    Store(owner_address+0xaec0,0x7c+5*0xd0,int(0));step();assert(guarded_portals==0);
    assert(PortalFire(portals,5) && portal_fires==fires+1);
    assert(queries==before_guard_queries && windows_memory_queries==before_guard_windows);
    // Scripted camera/control takes precedence; other managers never blocked.
    State selected;assert(Context(expected_manager,selected,true));ArmPortalGuard(selected);
    assert(PortalFire(reinterpret_cast<void*>(owner_address+0xaed0),5));
    Store(owner_address,0x70,int(1));assert(PortalFire(portals,5) && guarded_portals==0);
    Store(owner_address,0x70,int(0));ArmPortalGuard(selected);
    Store(owner_address,0x186a8,ptr(party));UpdatePortalGuard(selected,GetTickCount64());assert(guarded_portals==0);
    Store(owner_address,0x186a8,ptr(scene));
    // Disable is functional: cancels queued choices, clears both exemptions,
    // preserves preference, and gives native control back on its next request.
    Refresh();assert(Request(1));const int preferred_before=Ps4RtSettings::Get().exploration_leader;
    Ps4RtSettings::Get().exploration_leader_enabled=false;
    count=changes;Input(0,false);Input(0x20,false);step();
    assert(changes==count && !pending && !selection_active && guarded_portals==0 && !Request(1));
    assert(Ps4RtSettings::Get().exploration_leader==preferred_before);
    AutomaticReturn(expected_manager,48,42);assert(changes==count+1);
    Ps4RtSettings::Get().exploration_leader_enabled=true;next_restore=0;step();
    assert(GetView().current==preferred_before && changes==count+2);
    ResetCurrent();
    // Test the actual five-byte call redirect on an owned SysV fixture. Native
    // and custom calls share their target, but only the redirected call filters.
    auto* call_code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(call_code);
    const unsigned char call_fixture[]={0x55,0x48,0x89,0xe5,0x48,0x83,0xec,0x10,0xe8,0x33,0,0,0,0xc9,0xc3};
    std::memcpy(call_code,call_fixture,sizeof call_fixture);
    const unsigned char jump[]={0xff,0x25,0,0,0,0};std::memcpy(call_code+64,jump,6);
    auto native_change=&NativeChange;std::memcpy(call_code+70,&native_change,8);
    assert(VirtualProtect(call_code,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),call_code,4096);
    ReturnCallHook call_relay;
    assert(!call_relay.Install(call_code+8,call_code+65,reinterpret_cast<void*>(&AutomaticReturn)));
    assert(call_relay.Install(call_code+8,call_code+64,reinterpret_cast<void*>(&AutomaticReturn)));
    MEMORY_BASIC_INFORMATION call_page{};assert(VirtualQuery(call_code,&call_page,sizeof call_page));
    assert(call_page.Protect==PAGE_EXECUTE_READ);
    assert(Request(3));Refresh();const auto calls_before=changes;
    reinterpret_cast<SwitchFn>(call_code)(expected_manager,48,42);assert(changes==calls_before);
    assert(call_relay.Remove());assert(std::memcmp(call_code,call_fixture,sizeof call_fixture)==0);
    reinterpret_cast<SwitchFn>(call_code)(expected_manager,48,42);assert(changes==calls_before+1);
    assert(VirtualFree(call_code,0,MEM_RELEASE));ClearSelection();
    installed=false;
    // Actual startup relay preserves the System V manager and XMM0 time.
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(code);
    const unsigned char fixture[]={0x55,0x48,0x89,0xe5,0x41,0x57,
        0xf3,0x0f,0x11,0x07,0xff,0x47,0x04,0x41,0x5f,0x5d,0xc3};
    std::memcpy(code,fixture,sizeof fixture);assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),code,sizeof fixture);
    Ps4RtHud::HookSet<1> relay;assert(relay.Install({code},{reinterpret_cast<const void*>(&Update)}));
    original=reinterpret_cast<UpdateFn>(relay.Original(0));
    struct {float delta{};unsigned calls{};} args;
    reinterpret_cast<UpdateFn>(code)(&args,2.5f);assert(args.delta==2.5f && args.calls==1);
    assert(relay.Remove());assert(VirtualFree(code,0,MEM_RELEASE));assert(VirtualFree(memory,0,MEM_RELEASE));
    std::puts("Exploration leader: native-thread queue, eligibility, scene guards, refusal, expiry and ABI relay passed");
}
