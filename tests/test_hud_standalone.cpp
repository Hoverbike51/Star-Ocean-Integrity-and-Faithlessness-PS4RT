// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud_runtime_model.h"
#include "../gpu/shim/ps4rt_screen_mask.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <cmath>
using namespace So5Hud;
int main(int argc,char** argv) {
    // Optional exported fixtures verify authoring/runtime interoperability.
    for(int i=1;i<argc;++i) {
        const auto p=Parse(Read(argv[i],65536));
        assert(RuntimeCompatible(p,p.aspect));
        assert(Parse(SerializeReadable(p))==p);
    }
    // No filesystem inputs and no authoring application or exported catalog.
    const auto& catalog=GetNativeCatalog();assert(catalog.resources.size()==156);
    for(const auto& resource:RuntimeResources) {
        const auto* native=catalog.Find(resource.resource);assert(native);
        assert(native->binding.group==resource.group && native->binding.english==resource.english);
        auto layout=Base(Aspect::Standard);layout.elements.push_back({std::string(resource.resource),"",{Anchor::Original,15,25,1.25,false}});
        assert(RuntimeCompatible(layout,Aspect::Standard));assert(CompileElements(layout).size()==1);
    }
    // Every supported ratio fills the complete background while the original
    // UI keeps its uniform native scale. Presets preserve their aspect family.
    for(auto aspect:{Aspect::FourThree,Aspect::SixteenTen,Aspect::Standard,Aspect::Wide,Aspect::Superwide}) {
        const auto canvas=Canvas(aspect);assert(OutputAspect(canvas.w,canvas.h)==aspect);
        auto p=Base(aspect);p.reference_width=int(canvas.w);p.reference_height=int(canvas.h);
        const auto parsed=Parse(SerializeReadable(p));assert(parsed==p&&RuntimeCompatible(parsed,aspect));
        const auto original=Place({0,0,1920,1080},{},canvas.w,canvas.h);
        assert(std::abs(original.w/original.h-16./9)<1e-8);
        const auto fill=BackgroundTransform(canvas.w,canvas.h);
        const double unit=original.w/1920;
        assert(std::abs(original.x+fill.x*unit)<.001&&std::abs(original.y+fill.y*unit)<.001);
        assert(std::abs(1920*fill.sx*unit-canvas.w)<.001&&std::abs(1080*fill.sy*unit-canvas.h)<.001);
    }
    // Real GPU GUI path: a full-frame solid-black fade must bypass the
    // aspect-preserving HUD projection, irrespective of its producer object.
    using Ps4RtScreenMask::Topology;
    using MaskVertex=Ps4RtScreenMask::Vertex;
    const std::array<MaskVertex,6> mask{{{{-1,-1,0,1},0x7f000000},{{1,-1,0,1},0x7f000000},
        {{1,1,0,1},0x7f000000},{{-1,-1,0,1},0x7f000000},{{1,1,0,1},0x7f000000},{{-1,1,0,1},0x7f000000}}};
    assert(Ps4RtScreenMask::SolidShader(0x59cb445a,0xf8591c05));
    assert(!Ps4RtScreenMask::SolidShader(0xc87f248e,0x4eab1bc3)); // textured UI
    assert(Ps4RtScreenMask::FullFrame(mask,Topology::Triangles));
    for(unsigned alpha:{0u,1u,127u,255u}) {
        auto fade=mask;for(auto& v:fade)v.colour=alpha<<24;
        assert(Ps4RtScreenMask::FullFrame(fade,Topology::Triangles));
        for(auto aspect:{Aspect::FourThree,Aspect::SixteenTen,Aspect::Standard,Aspect::Wide,Aspect::Superwide}) {
            const auto canvas=Canvas(aspect);
            for(const auto& v:fade) {
                const double x=(v.clip[0]+1)*canvas.w/2,y=(v.clip[1]+1)*canvas.h/2;
                assert(x==0||x==canvas.w);assert(y==0||y==canvas.h);
            }
        }
    }
    auto rejected_mask=mask;rejected_mask[0].colour|=1;assert(!Ps4RtScreenMask::FullFrame(rejected_mask,Topology::Triangles));
    rejected_mask=mask;rejected_mask[0].colour=0xff000000;assert(!Ps4RtScreenMask::FullFrame(rejected_mask,Topology::Triangles)); // gradient
    rejected_mask=mask;for(auto& v:rejected_mask)v.clip[0]*=.5f;assert(!Ps4RtScreenMask::FullFrame(rejected_mask,Topology::Triangles)); // panel
    rejected_mask=mask;rejected_mask[0].clip[2]=.5f;assert(!Ps4RtScreenMask::FullFrame(rejected_mask,Topology::Triangles)); // geometry
    rejected_mask=mask;rejected_mask[3]=mask[0];rejected_mask[4]=mask[1];rejected_mask[5]=mask[2];
    assert(!Ps4RtScreenMask::FullFrame(rejected_mask,Topology::Triangles)); // two copies of one triangle
    const std::array<MaskVertex,4> strip{mask[0],mask[1],mask[5],mask[2]};
    assert(Ps4RtScreenMask::FullFrame(strip,Topology::Strip));
    // Actual cached GUI pipeline: primitive 19 (QuadList), four control points.
    // Exercise both winding orders and every start corner, throughout the fade.
    const std::array<MaskVertex,4> quad{mask[0],mask[1],mask[2],mask[5]};
    for(unsigned alpha:{0u,1u,127u,255u}) for(unsigned start=0;start<4;++start) for(int step:{-1,1}) {
        auto fade=quad;
        for(int i=0;i<4;++i){fade[i]=quad[(int(start)+i*step+4)%4];fade[i].colour=alpha<<24;}
        assert(Ps4RtScreenMask::FullFrame(fade,Topology::Quads));
    }
    assert(!Ps4RtScreenMask::FullFrame(strip,Topology::Quads)); // crossed perimeter
    assert(!Ps4RtScreenMask::FullFrame(quad,Topology::Strip));
    assert(!Ps4RtScreenMask::FullFrame(quad,Topology::Triangles));
    auto panel=quad;for(auto& v:panel)v.clip[0]*=.5f;
    assert(!Ps4RtScreenMask::FullFrame(panel,Topology::Quads));
    panel=quad;panel[0].colour|=1;
    assert(!Ps4RtScreenMask::FullFrame(panel,Topology::Quads));

    // Sizing overrides round-trip through files and use the same runtime path
    // for all background groups; foreground groups reject this property.
    for(auto aspect:{Aspect::FourThree,Aspect::SixteenTen,Aspect::Standard,Aspect::Wide,Aspect::Superwide}) {
        auto p=Base(aspect);const auto canvas=Canvas(aspect);
        for(size_t group=0;group<GroupCount;++group)if(IsBackgroundGroup(group)) {
            for(auto mode:{BackgroundFit::Original,BackgroundFit::Extended}) {
                p.backgrounds[group]=mode;
                const auto parsed=Parse(SerializeReadable(p));assert(parsed==p&&RuntimeCompatible(parsed,aspect));
                const auto t=BackgroundTransform(parsed,group,canvas.w,canvas.h);
                const auto expected=mode==BackgroundFit::Original?Visual{0,0,1,1}:BackgroundTransform(canvas.w,canvas.h);
                assert(t.x==expected.x&&t.y==expected.y&&t.sx==expected.sx&&t.sy==expected.sy);
                assert(parsed.groups[0]==Setting{}); // No foreground stretch.
            }
        }
        p.reference_width=int(canvas.w);p.reference_height=int(canvas.h);
        assert(Parse(SerializeReadable(p))==p);
    }
    auto invalid=Base(Aspect::Standard);invalid.backgrounds[0]=BackgroundFit::Extended;
    bool rejected=false;try{Validate(invalid);}catch(...){rejected=true;}assert(rejected);
    auto invalid_json=Serialize(Base(Aspect::Standard));invalid_json.replace(invalid_json.find("\"schema_version\":1"),18,"\"schema_version\":4");
    rejected=false;try{Parse(invalid_json);}catch(...){rejected=true;}assert(rejected);
    const auto* pause=catalog.Find("pause");assert(pause&&pause->binding.group==31);
    const auto pause_id=IdentifyHandle(pause,987);
    const std::uint64_t bg[]={NativeName("BG",987),NativeName("INOUT_BASE",987),NativeName("BASE",987)};
    const std::uint64_t caption[]={NativeName("TXT_MAPNAME",987),NativeName("INOUT_BASE",987),NativeName("BASE",987)};
    assert(pause_id.Accepts(bg)&&!pause_id.Accepts(caption));
    const std::uint64_t guide[]={NativeName("TXT",987),NativeName("GUIDE",987),NativeName("INOUT_BASE",987),NativeName("BASE",987)};
    const std::uint64_t skip[]={NativeName("TXT_SKIP",987),NativeName("SKIP_BUTTON",987),NativeName("BASE",987)};
    assert(IsPauseControls(pause_id,guide)&&IsPauseControls(pause_id,skip));
    assert(!IsPauseControls(pause_id,bg)&&!IsPauseControls(pause_id,caption));
    assert(RuntimeSupported(32));
    for(auto aspect:{Aspect::FourThree,Aspect::SixteenTen,Aspect::Standard,Aspect::Wide,Aspect::Superwide}) {
        auto p=Base(aspect);p.groups[32]={Anchor::BottomRight,-100,-80,.75,false};
        const auto loaded=Parse(SerializeReadable(p));assert(loaded==p&&RuntimeCompatible(loaded,aspect));
        const auto canvas=Canvas(aspect);const auto moved=Transform({1597,935,1,1},PauseControls,p.groups[32],canvas.w,canvas.h);
        assert(moved&&moved->sx==.75f&&moved->sy==.75f);
    }
    const auto* creation=catalog.Find("fm_itemcreation");assert(creation);
    const auto creation_id=IdentifyHandle(creation,987);
    const std::uint64_t screen[]={NativeName("ALL_SCREEN",987)};
    const std::uint64_t child[]={NativeName("UI_TEXT",987),NativeName("ALL_SCREEN",987)};
    assert(creation_id.Accepts(screen)&&!creation_id.Accepts(child));
    const std::uint64_t unnamed[]={0,NativeName("ALL_SCREEN",987)};
    assert(!creation_id.Accepts(unnamed)); // Empty root slots never identify unnamed UI.
    // Older exported layouts omit the new pause group: it defaults to visible.
    auto old=Serialize(Base(Aspect::Standard));const auto last=old.rfind("{\"id\":\"pause_background\"");
    assert(last!=std::string::npos);const auto end=old.find('}',last);old.erase(last-1,end-last+2);
    assert(Parse(old).groups[31].visible);
    const auto* native=catalog.Find("nowloading");assert(native);
    auto preset=Base(Aspect::Superwide);
    preset.elements.push_back({"nowloading","NOWLOADING_SET",{Anchor::Original,12,34,1.25,true}});
    auto edits=CompileElements(Parse(SerializeReadable(preset)));assert(edits.size()==1);
    const int handle=987;std::vector<std::uint64_t> chain={NativeName("NOWLOADING_SET",handle)};
    const auto object=std::ranges::find(native->objects,edits[0].key,&NativeObject::key);assert(object!=native->objects.end());
    auto parent=object->parent_index;
    while(parent!=size_t(-1)) {
        const auto& o=native->objects.at(parent);auto hash=o.name_hash;
        for(unsigned char c:std::string("_000000987"))hash=(hash*0x100000001b3)^c;
        chain.push_back(hash);parent=o.parent_index;
    }
    const auto* recovered=catalog.Resolve(handle,chain,object->type);assert(recovered==native);
    auto identity=IdentifyHandle(native,handle);auto transformed=ElementTransform({0,0,1,1},identity,chain,edits);
    assert(transformed && transformed->sx==1.25f);
    preset.elements[0].value.visible=false;edits=CompileElements(preset);
    assert(!ElementTransform({0,0,1,1},identity,chain,edits));
    preset.elements[0].object="unknown-object";assert(!RuntimeCompatible(preset,Aspect::Superwide));
    assert(!catalog.Resolve(handle,chain,"unverified type"));
    std::cout<<"PASS: standalone HUD group/object overrides, cached-clone identity and invalid-object rejection\n";
}
