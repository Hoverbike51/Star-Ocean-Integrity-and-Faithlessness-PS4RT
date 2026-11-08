// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the exact native RIP-relative load and live data publication, without
// executing a game image or modifying any game file.
#include "../gpu/shim/ps4rt_camera_comfort.cpp"
#include "common/types.h"
#include <cassert>
#include <cmath>
#include <thread>
#include <vector>

int main() {
    using namespace Ps4RtCameraComfort;
    using namespace Ps4RtCameraProbe;
    std::vector<unsigned char> fixture(SceneGlobal+8);
    const auto signature=[&](auto at,const auto& bytes) {
        std::memcpy(fixture.data()+at,bytes.data(),bytes.size());
    };
    signature(FollowUpdateAddress,FollowSignature);
    signature(0x4fbdb0,LensSignature); signature(0x188e70,ManagerSignature);
    signature(site_offset,instruction); signature(0x1b69b0,consumer);
    const float native=0.2f;
    std::memcpy(fixture.data()+original_constant,&native,4);
    assert(Matches(fixture,"CUSA03219","01.00"));
    const auto pristine=fixture;
    assert(!Initialize(fixture,"CUSA03219","01.01",Original));
    assert(!Initialize(fixture,"CUSA00000","01.00",Original));
    assert(!Initialize(std::span(fixture).first(SceneGlobal+7),"CUSA03219","01.00",Original));
    assert(!Initialize(fixture,"CUSA03219","01.00",Count));
    for (const auto offset:{site_offset,std::size_t(0x1b69b0),original_constant}) {
        fixture[offset]^=1;
        assert(!Initialize(fixture,"CUSA03219","01.00",Light));
        fixture[offset]^=1;
    }
    assert(fixture==pristine && !Available() && !Select(Light));

    auto* executable=static_cast<unsigned char*>(VirtualAlloc(nullptr,fixture.size(),
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(executable);
    std::memcpy(executable,fixture.data(),fixture.size());
    // Real native seven-byte LEA, followed by a fixture-only load and return.
    constexpr unsigned char tail[]{0xc5,0xfa,0x10,0x00,0xc3};
    std::memcpy(executable+site_offset+7,tail,sizeof tail);
    DWORD previous{};
    assert(VirtualProtect(executable,fixture.size(),PAGE_EXECUTE_READ,&previous));
    FlushInstructionCache(GetCurrentProcess(),executable,fixture.size());
    using ReadNative=float(PS4_SYSV_ABI *)();
    const auto read=reinterpret_cast<ReadNative>(executable+site_offset);
    assert(std::bit_cast<std::uint32_t>(read())==std::bit_cast<std::uint32_t>(native));
    const std::vector<unsigned char> before(executable,executable+fixture.size());
    assert(Initialize({executable,fixture.size()},"CUSA03219","01.00",Original));
    assert(Available() && read()==native);
    MEMORY_BASIC_INFORMATION code{},data{};
    assert(VirtualQuery(executable+site_offset,&code,sizeof code));
    assert(code.Protect==PAGE_EXECUTE_READ);
    assert(VirtualQuery(coefficient.load(),&data,sizeof data));
    assert(data.Protect==PAGE_READWRITE);
    const std::vector<unsigned char> installed(executable,executable+fixture.size());
    for(std::size_t i=0;i<before.size();++i)
        if(i<site_offset+3 || i>=site_offset+7) assert(before[i]==installed[i]);
    // Live selections never rewrite code or the original shared constant.
    for(int mode=Original;mode<Count;++mode) {assert(Select(mode));assert(read()==Coefficient(mode));}
    assert(!Select(-1) && !Select(Count) && read()==Coefficient(Damped));
    std::atomic<bool> done{},invalid{};
    std::thread writer([&] {for(int i=0;i<20000;++i) Select(i%Count);done=true;});
    while(!done) {const float v=read();if(v!=Coefficient(Original)&&v!=Coefficient(Light)&&v!=Coefficient(Damped))invalid=true;}
    writer.join(); assert(!invalid);
    assert(Select(Original) && read()==native);
    assert(!std::memcmp(executable,installed.data(),installed.size()));
    // Native time substeps are retained: two one-tick updates equal one
    // two-tick update (e.g. 60 Hz versus 30 Hz native scheduling).
    for (int mode=Original;mode<Count;++mode) {
        const auto advance=[&](float value,float ticks) {
            while(ticks>0) {const float step=std::min(ticks,1.0f);value+=(100-value)*step*Coefficient(mode);ticks-=step;}
            return value;
        };
        assert(std::abs(advance(advance(0,1),1)-advance(0,2))<1e-5f);
        assert(advance(0,0)==0);
        assert(advance(0,2)>0 && advance(0,2)<100);
    }
    assert(VirtualProtect(executable+site_offset,7,PAGE_READWRITE,&previous));
    std::memcpy(executable+site_offset,instruction.data(),instruction.size());
    auto* storage=coefficient.exchange(nullptr);
    assert(VirtualFree(storage,0,MEM_RELEASE));
    assert(VirtualFree(executable,0,MEM_RELEASE));
    std::puts("PASS: guarded native follow coefficient, original restoration, code protections and atomic live selection");
}
