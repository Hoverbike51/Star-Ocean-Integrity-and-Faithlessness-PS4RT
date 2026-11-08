// SPDX-License-Identifier: GPL-2.0-or-later
// Native-call pass-through and bounded capture checks. No game is launched.
#include "../gpu/shim/ps4rt_camera_probe.cpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

namespace {
std::vector<std::pair<std::uintptr_t,std::size_t>> ranges;
std::atomic<unsigned> original_calls{};
std::atomic<bool> hold_original{}, entered_original{}, release_original{};
std::atomic<bool> wrong_arguments{};
void* expected_object{};
float expected_delta{};
std::atomic<unsigned> switch_calls{};
void* expected_manager{};
std::uint32_t expected_old=42,expected_new=84;
std::uintptr_t root_for_switch{};
void PS4_SYSV_ABI NativeSwitch(void* manager,std::uint32_t old_handle,std::uint32_t requested) {
    assert(manager==expected_manager && old_handle==expected_old && requested==expected_new);
    ++switch_calls;
    if(root_for_switch) std::memcpy(reinterpret_cast<void*>(root_for_switch+0x186a0),&requested,sizeof requested);
}
void PS4_SYSV_ABI NativeFollow(void* object,float delta) {
    if (object!=expected_object || std::memcmp(&delta,&expected_delta,sizeof(float)))
        wrong_arguments=true;
    ++original_calls;
    if (hold_original && !entered_original.exchange(true))
        while (!release_original) std::this_thread::yield();
}
template<class T> void Write(std::vector<unsigned char>& bytes,std::size_t offset,T value) {
    assert(offset<=bytes.size() && sizeof(value)<=bytes.size()-offset);
    std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
void Map(std::vector<unsigned char>& bytes) {
    ranges.emplace_back(reinterpret_cast<std::uintptr_t>(bytes.data()),bytes.size());
}
}
extern "C" int runtime_memory_is_mapped(std::uintptr_t address,std::uint64_t bytes) {
    for (const auto& [start,size]:ranges)
        if (address>=start && address-start<=size && bytes<=size-(address-start)) return 1;
    return 0;
}

int main(int argc,char** argv) {
    using namespace Ps4RtCameraProbe;
    std::vector<unsigned char> image(SceneGlobal+8);
    const auto signature=[&](std::size_t at,const auto& bytes) {
        std::copy(bytes.begin(),bytes.end(),image.begin()+at);
    };
    signature(FollowUpdateAddress,FollowSignature);
    signature(0x4fbdb0,LensSignature); signature(0x188e70,ManagerSignature);
    assert(ProfileMatches(image,"CUSA03219","01.00"));
    assert(!ProfileMatches(image,"CUSA03219","01.01"));
    assert(!ProfileMatches(image,"CUSA00000","01.00"));
    assert(!ProfileMatches(std::span(image).first(SceneGlobal+7),"CUSA03219","01.00"));
    for (const auto at:{FollowUpdateAddress,std::size_t(0x4fbdb0),std::size_t(0x188e70)}) {
        image[at]^=1; assert(!ProfileMatches(image,"CUSA03219","01.00")); image[at]^=1;
    }
    // With capture disabled Initialize must not touch executable bytes.
    _wputenv_s(L"PS4RT_PERF_CAPTURE_DIR",L"");
    const auto before=image;
    Initialize(image,"CUSA03219","01.00");
    assert(image==before && !enabled);

    std::vector<unsigned char> scene(0x80), owner(0x18700), controller(0x220);
    std::vector<unsigned char> renderer(0xc0), system(0x20), camera(0xeb8), lens(0x70), target(0x5a10);
    std::vector<unsigned char> status(0x20),party(0x22000);
    // Match the real ownership split: executable/BSS and CRT singletons are
    // absent from the guest-mmap registry, while guest actors are registered.
    for (auto* bytes:{&controller,&target}) Map(*bytes);
    const auto pointer=[](auto& bytes) {return reinterpret_cast<std::uintptr_t>(bytes.data());};
    Write(image,SceneGlobal,pointer(scene)); Write(scene,0x40,pointer(owner));
    Write(owner,0,pointer(image)+0x972000);
    Write(image,0x972000-24,std::int64_t(0));
    Write(owner,0x340,pointer(image)+0x972020);
    Write(image,0x972020-24,std::int64_t(-0x340));
    Write(owner,0x186a0,std::uint32_t(42));
    Write(target,0x54,std::uint32_t(42));Write(target,0x5a00,pointer(status));
    Write(status,8,std::int16_t(2));Write(image,0xb839f8,pointer(party));
    for(unsigned i=0;i<9;++i) Write(party,0x54+0x21f80+i*4,std::int16_t(i<4?i:-1));
    Write(owner,0x28,pointer(controller)); Write(owner,0x70,3); Write(owner,0x78,0);
    Write(controller,0,pointer(image)+0x972000); Write(controller,0xa8,pointer(target));
    Write(image,RendererGlobal,pointer(renderer)); Write(renderer,0xb8,pointer(system));
    Write(system,8,pointer(camera)); Write(camera,0xdf0,pointer(lens));
    Write(camera,0xeb0,32.0f/9); Write(lens,0xc,0.7853982f);
    const auto configured_image=image;
    base=pointer(image); loaded_image=image; original=&NativeFollow;
    expected_object=controller.data(); expected_delta=1.0f;
    Follow(expected_object,expected_delta);
    assert(original_calls==1 && count==0 && !wrong_arguments);
    started=GetTickCount64(); enabled=true;
    Follow(expected_object,expected_delta);
    assert(original_calls==2 && count==1 && !wrong_arguments);
    assert(snapshots[0].manager_matches && snapshots[0].mode==0 && snapshots[0].previous_mode==3);
    assert(snapshots[0].manager_valid && host_reads>0);
    assert(snapshots[0].manager_chain_level==3 && snapshots[0].camera_chain_level==5);
    assert(snapshots[0].lens_valid && snapshots[0].radians==snapshots[0].post_radians);
    assert(snapshots[0].native_delta==1.0f); // Native units are never converted by the probe.
    assert(snapshots[0].character_id==2 && snapshots[0].actor_handle==42);
    assert(snapshots[0].control_valid && snapshots[0].controlled_handle==42);
    assert(snapshots[0].party_valid && snapshots[0].party_ids[3]==3 && snapshots[0].party_ids[4]==-1);
    assert(image==configured_image);
    original_switch=&NativeSwitch;expected_manager=owner.data()+0x340;root_for_switch=pointer(owner);
    ControlSwitch(expected_manager,42,84);
    assert(switch_calls==1 && switch_count==1);
    assert(switches[0].scene_matches && switches[0].before_valid && switches[0].before==42);
    assert(switches[0].after_valid && switches[0].after==84);
    assert(switches[0].old_handle==42 && switches[0].requested_handle==84);
    Write(owner,0x186a0,std::uint32_t(42));root_for_switch=0;
    // A readable field must not cross into a guard/no-access/reserved page.
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(pages); DWORD previous{};
    assert(HostReadable(reinterpret_cast<std::uintptr_t>(pages)+4094,4));
    assert(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&previous));
    assert(!HostReadable(reinterpret_cast<std::uintptr_t>(pages)+4094,4));
    assert(VirtualProtect(pages+4096,4096,PAGE_READWRITE|PAGE_GUARD,&previous));
    assert(!HostReadable(reinterpret_cast<std::uintptr_t>(pages)+4094,4));
    assert(!HostReadable(0,4) && !HostReadable(std::numeric_limits<std::uintptr_t>::max()-1,4));
    assert(VirtualFree(pages,0,MEM_RELEASE));
    const auto scene_before=scene, owner_before=owner, controller_before=controller;
    const auto camera_before=camera, lens_before=lens;

    // A second guest thread must still call native code while observation is busy.
    hold_original=true;
    std::thread worker([&] {Follow(expected_object,expected_delta);});
    while (!entered_original) std::this_thread::yield();
    const auto calls_before=original_calls.load();
    Follow(expected_object,expected_delta);
    assert(original_calls==calls_before+1 && skipped>=1);
    release_original=true; worker.join(); hold_original=false;
    assert(scene==scene_before && owner==owner_before && controller==controller_before);
    assert(camera==camera_before && lens==lens_before && !wrong_arguments);

    Write(lens,0xc,std::numeric_limits<float>::infinity()); next_sample=0;
    Follow(expected_object,expected_delta);
    assert(!snapshots[count-1].lens_valid);
    Write(lens,0xc,0.7853982f);
    Write(controller,0x140,std::numeric_limits<float>::quiet_NaN()); next_sample=0;
    Follow(expected_object,expected_delta);
    assert(!snapshots[count-1].follow_before_valid && !snapshots[count-1].follow_after_valid);
    Write(controller,0x140,0.0f);
    const auto invalid_before=invalid_delta.load();
    expected_delta=std::numeric_limits<float>::quiet_NaN(); next_sample=0;
    Follow(expected_object,expected_delta);
    assert(invalid_delta==invalid_before+1 && snapshots[count-1].native_delta==0);
    expected_delta=1.0f;
    // Capture storage is fixed; saturation never skips the real native update.
    for (std::size_t i=0;i<snapshots.size()+8;++i) {next_sample=0; Follow(expected_object,expected_delta);}
    assert(count==snapshots.size() && !wrong_arguments);

    report_path=std::filesystem::temp_directory_path()/
        ("so5-camera-probe-test-"+std::to_string(GetCurrentProcessId())+".json");
    entered_original=false; release_original=false; hold_original=true;
    std::thread finishing([&] {Follow(expected_object,expected_delta);});
    while (!entered_original) std::this_thread::yield();
    Report();
    assert(!enabled && !release_original); // Report never waits for the guest update.
    release_original=true; finishing.join(); hold_original=false;
    std::ifstream file(report_path,std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)),{}); file.close();
    assert(text.starts_with("{\n") && text.ends_with("\n}\n"));
    assert(text.find("\"manager_matches\": true")!=std::string::npos);
    assert(text.find("Unverified; do not assume seconds")!=std::string::npos);
    assert(text.find("\"native_control_switches\": [")!=std::string::npos);
    assert(text.find("\"scene_matches\": true")!=std::string::npos);
    assert(text.find("\"party_valid\": true")!=std::string::npos);
    assert(text.find(std::to_string(pointer(controller)))==std::string::npos);
    assert(text.find(": inf")==std::string::npos && text.find(": nan")==std::string::npos);
    assert(!enabled); Follow(expected_object,expected_delta);
    if (argc==2) std::filesystem::copy_file(report_path,argv[1],std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(report_path);

    // Execute an owned System V fixture through the real detour/trampoline.
    // Its body stores XMM0 and increments a counter through RDI. No game code.
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(code);
    constexpr unsigned char fixture[]={0x55,0x48,0x89,0xe5,0x41,0x57,
        0xf3,0x0f,0x11,0x07,0xff,0x47,0x04,0x41,0x5f,0x5d,0xc3};
    std::memcpy(code,fixture,sizeof fixture);
    DWORD protection{}; assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&protection));
    FlushInstructionCache(GetCurrentProcess(),code,sizeof fixture);
    Ps4RtHud::HookSet<1> relay;
    assert(relay.Install({code},{reinterpret_cast<const void*>(&Follow)}));
    original=reinterpret_cast<FollowFn>(relay.Original(0));
    struct {float delta=0; unsigned calls=0;} arguments;
    const auto invoke=reinterpret_cast<FollowFn>(code);
    invoke(&arguments,17.25f);
    assert(arguments.delta==17.25f && arguments.calls==1);
    enabled=true; invoke(&arguments,0.5f); enabled=false;
    assert(arguments.delta==0.5f && arguments.calls==2);
    assert(relay.Remove());
    invoke(&arguments,2.0f);
    assert(arguments.delta==2.0f && arguments.calls==3);
    assert(VirtualFree(code,0,MEM_RELEASE));
    // Real relay preserves the native manager, old handle and requested handle.
    code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    constexpr unsigned char swap_fixture[]={0x55,0x48,0x89,0xe5,0x41,0x57,
        0x89,0x37,0x89,0x57,0x04,0xff,0x47,0x08,0x41,0x5f,0x5d,0xc3};
    std::memcpy(code,swap_fixture,sizeof swap_fixture);
    assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&protection));
    FlushInstructionCache(GetCurrentProcess(),code,sizeof swap_fixture);
    Ps4RtHud::HookSet<1> swap_relay;
    assert(swap_relay.Install({code},{reinterpret_cast<const void*>(&ControlSwitch)}));
    original_switch=reinterpret_cast<SwitchFn>(swap_relay.Original(0));
    struct {std::uint32_t old_handle{},next{},calls{};} switch_arguments;
    const auto invoke_switch=reinterpret_cast<SwitchFn>(code);
    invoke_switch(&switch_arguments,123,456);
    assert(switch_arguments.old_handle==123 && switch_arguments.next==456 && switch_arguments.calls==1);
    enabled=true;invoke_switch(&switch_arguments,789,321);enabled=false;
    assert(switch_arguments.old_handle==789 && switch_arguments.next==321 && switch_arguments.calls==2);
    assert(!switches[switch_count-1].scene_matches);
    assert(swap_relay.Remove());assert(VirtualFree(code,0,MEM_RELEASE));
    std::puts("Camera probe: identity guards, pass-through, concurrency, finite JSON and bounded capture passed");
}
