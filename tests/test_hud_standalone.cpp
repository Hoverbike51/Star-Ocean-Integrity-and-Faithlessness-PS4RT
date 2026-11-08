// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud_runtime_model.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace So5Hud;
int main() {
    // No filesystem inputs and no authoring application or exported catalog.
    const auto& catalog=GetNativeCatalog();assert(catalog.resources.size()==156);
    for(const auto& resource:RuntimeResources) {
        const auto* native=catalog.Find(resource.resource);assert(native);
        assert(native->binding.group==resource.group && native->binding.english==resource.english);
        auto layout=Base(Aspect::Standard);layout.elements.push_back({std::string(resource.resource),"",{Anchor::Original,15,25,1.25,false}});
        assert(RuntimeCompatible(layout,Aspect::Standard));assert(CompileElements(layout).size()==1);
    }
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
