// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud_runtime_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <boost/json.hpp>
#include <set>
#include <charconv>

namespace So5Hud {
// Exact native layout XML fingerprints (not the enclosing ISF container).
// The game passes only this chunk to the GUI loader. Checked for ENG/FRE.
// These contain no game artwork, scripts or binary payloads.
const std::array<ResourceBinding,25> RuntimeResources={{
    {"commonmemberinfo",0,23308,0x12771e80a03a92da,0x12771e80a03a92da,{-40,855,380,196}},
    {"targetinfo",2,12217,0xa516ac976fb0b7bc,0xa516ac976fb0b7bc,{-10,141,513,189}},
    {"result",3,11347,0x64a3e4814e469b4f,0x64a3e4814e469b4f,{1338,288,582,381}},
    {"hitcombo",4,2928,0xc5c590acbb2c0456,0xc5c590acbb2c0456,{1467,338,294,130}},
    {"bm_infoarea",5,3404,0x9bea00c1aab38904,0x9bea00c1aab38904,{-10,113,1940,76}},
    {"ft_infoarea",6,2884,0x51e50ff1038c2c2c,0x51e50ff1038c2c2c,{-10,113,1940,76}},
    {"bm_topmenu",7,15945,0x209dbca39b6ee76f,0x209dbca39b6ee76f,{673,316,511,448}},
    {"characterchange",9,4123,0x63c365b05310bf07,0x63c365b05310bf07,{647,522,626,110}},
    {"mapname",10,6339,0xac4791b6a027eaca,0xac4791b6a027eaca,{280,316,1360,90}},
    {"battleinout",11,2430,0x1a12b98ff8ec64ca,0x1a12b98ff8ec64ca,{383,396,1155,85}},
    {"messagewindow",12,1769,0xba8f4f070ff9b563,0xba8f4f070ff9b563,{0,0,760,180}},
    {"spinfo",15,4820,0x743e2546120b2522,0x743e2546120b2522,{393,44,1134,121}},
    {"battlecollection",16,2906,0x234175d41aef6159,0x234175d41aef6159,{0,387,493,40}},
    {"levelup",17,2882,0xa7e66ac8a6d9947a,0xa7e66ac8a6d9947a,{777,380,366,125}},
    {"keyguide",18,1814,0xd87360f732ba91d8,0xd87360f732ba91d8,{0,-13,768,58}},
    {"target",13,2982,0x0b472d313bdccc6e,0x0b472d313bdccc6e,{60,16,157,128}},
    {"nowloading",19,13742,0xda37c4585ed39522,0xda37c4585ed39522,{1416,947,414,106}},
    {"fm_background",20,1247,0x2edeca6c4c929f86,0x2edeca6c4c929f86,{0,0,1920,1080}},
    {"fm_background2",21,2505,0xf72046b5736e8171,0xf72046b5736e8171,{0,0,1920,1080}},
    {"bm_pcselect",8,6901,0x4a651cf5c8f6dd24,0x4a651cf5c8f6dd24,{-44,456,1148,438}},
    {"column",3,2342,0xc54bf9080b45ff72,0xc54bf9080b45ff72,{1338,288,582,381}},
    {"privateaction",27,7391,0x5e6feda89ff4f781,0x5e6feda89ff4f781,{1252,943,670,54}},
    {"shop",29,10422,0x0ae0874be8584167,0x0ae0874be8584167,{-10,0,1940,1080}},
    {"shopmenu",29,8198,0xa9e921eb9de20325,0xa9e921eb9de20325,{-10,0,1940,1080}},
    {"fm_quest",30,8470,0x748e6e2e76518312,0x748e6e2e76518312,{-10,0,1940,1080}}
}};
const ResourceBinding MinimapComposite{"native_minimap_composite",1,0,0,0,{1584,4,332,332}};
const ResourceBinding Subtitles{"native_caption",28,0,0,0,{200,820,1520,160}};
const ResourceBinding BattleMenuBackground{"bm_topmenu_background",22,0,0,0,{-10,-10,1940,1100}};
const std::array<ResourceBinding,4> ProceduralResources={{
    {"native_damage_white",14,0,0,0,{900,480,120,45}},
    {"native_damage_red",24,0,0,0,{900,480,120,45}},
    {"native_damage_green",25,0,0,0,{900,480,120,45}},
    {"native_all_ui",26,0,0,0,{0,0,1920,1080}}
}};
bool RuntimeSupported(size_t group) {
    return group==1||group==22||group==28||std::ranges::any_of(ProceduralResources,[&](const auto& r){return r.group==group;})||
        std::ranges::any_of(RuntimeResources,[&](const auto& r){return r.group==group;});
}
const ResourceBinding* IdentifyResource(std::span<const unsigned char> data) {
    if(data.size()<5||std::memcmp(data.data(),"<?xml",5)||!std::ranges::any_of(RuntimeResources,[&](const auto& r){return r.bytes==data.size();}))return nullptr;
    std::uint64_t hash=14695981039346656037ull;
    for(auto c:data)hash=(hash^c)*1099511628211ull;
    for(const auto& r:RuntimeResources)if(r.bytes==data.size()&&(hash==r.english||hash==r.french))return &r;
    return nullptr;
}
std::uint64_t NativeName(std::string_view name,int handle) {
    char suffix[32];std::snprintf(suffix,sizeof suffix,"_%09d",handle);
    std::uint64_t h=0xcbf29ce484222325;
    for(unsigned char c:name)h=(h*0x100000001b3)^c;
    for(const char* c=suffix;*c;++c)h=(h*0x100000001b3)^static_cast<unsigned char>(*c);
    return h;
}
Identity IdentifyHandle(const ResourceBinding* r,int handle) {
    Identity id{.resource=r,.handle=handle};if(!r)return id;
    if(r->group==0) {
        const char* names[]={"BASE","POS_02","POS_03","POS_04","POS_05","POS_06","POS_07"};
        for(size_t n=0;n<7;++n)id.roots[n]=NativeName(names[n],handle);
        id.excluded=NativeName("FMBASE",handle);
    }else if(r->group==29||r->group==30)id.roots[0]=NativeName("BACK",handle);
    else if(r->group==7)id.roots[0]=NativeName("BM_BASE2",handle);
    else if(r->group==19){id.roots[0]=NativeName("NOWLOADING_SET",handle);id.roots[1]=NativeName("MASK",handle);}
    else if(r->group==2||r->group==3||r->group==4||r->group==5||r->group==6||r->group==9)
        id.roots[0]=NativeName("BASE",handle);
    return id;
}
bool Identity::Accepts(std::span<const std::uint64_t> ancestors) const {
    if(!resource||ancestors.empty())return false;
    if(excluded&&std::ranges::find(ancestors,excluded)!=ancestors.end())return false;
    if(!roots[0])return true; // whole, unambiguous centered resource
    for(auto root:roots)if(root&&std::ranges::find(ancestors,root)!=ancestors.end())return true;
    return false;
}
std::optional<Visual> Transform(Visual v,const ResourceBinding& r,const Setting& s,double width,double height) {
    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.sx)||!std::isfinite(v.sy))return {};
    const auto bounds=GroupBounds(r);
    const auto wanted=Place(bounds,s,width,height);
    const double unit=std::min(width/1920.,height/1080.),left=(width/unit-1920)/2,top=(height/unit-1080)/2;
    v.x=float((v.x-bounds.x)*s.scale+wanted.x/unit-left);
    v.y=float((v.y-bounds.y)*s.scale+wanted.y/unit-top);
    v.sx*=float(s.scale);v.sy*=float(s.scale);
    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.sx)||!std::isfinite(v.sy))return {};
    return v;
}
Rect GroupBounds(const ResourceBinding& r){auto bounds=r.bounds;if(r.group==0)bounds.w+=PartySlotStep*(PartySlots-1);return bounds;}
bool RuntimeCompatible(const Preset& p,Aspect actual) {
    Validate(p);if(p.aspect!=actual)return false;
    const auto base=Base(actual);
    for(size_t n=0;n<GroupCount;++n)if(!RuntimeSupported(n)&&p.groups[n]!=base.groups[n]) {
        // Historical custom presets carried inert preferred anchors for
        // unbound groups. Keep accepting those, but never apply or expose them.
        if(actual==Aspect::Standard||p.groups[n]!=Setting{Specs[n].preferred,0,0,1,true})return false;
    }
    try{(void)CompileElements(p);}catch(const std::exception&){return false;}
    return true;
}
namespace {
std::shared_ptr<const NativeCatalog> native_catalog=std::make_shared<const NativeCatalog>();
}
void SetNativeCatalog(std::shared_ptr<const NativeCatalog> c){native_catalog=c?std::move(c):std::make_shared<const NativeCatalog>();}
const NativeCatalog& GetNativeCatalog(){return *native_catalog;}
std::shared_ptr<const NativeCatalog> NativeCatalog::Parse(std::string_view text) {
    CheckJson(text,4*1024*1024);auto doc=boost::json::parse(text);const auto& root=doc.as_object();
    if(!root.at("private").as_bool()||root.at("title_id").as_string()!="CUSA03219"||root.at("app_ver").as_string()!="01.00")
        throw std::runtime_error("Incompatible private native UI catalog.");
    auto out=std::make_shared<NativeCatalog>();std::set<std::string> names;std::set<std::pair<unsigned,std::uint64_t>> fingerprints;
    std::uint32_t next=1;
    for(const auto& row:root.at("resources").as_array()) {
        if(out->resources.size()>=512)throw std::runtime_error("Native UI resource limit exceeded.");
        const auto& r=row.as_object();auto entry=std::make_unique<NativeResource>();entry->name=std::string(r.at("resource").as_string());
        if(entry->name.empty()||entry->name.size()>96||!names.insert(entry->name).second)throw std::runtime_error("Invalid native UI resource identity.");
        const auto size=r.at("bytes").to_number<unsigned>();const auto hash_text=r.at("hash").as_string();std::uint64_t hash=0;
        const auto parsed=std::from_chars(hash_text.data(),hash_text.data()+hash_text.size(),hash,16);
        if(!size||size>1048576||hash_text.size()!=16||parsed.ec!=std::errc{}||!fingerprints.emplace(size,hash).second)
            throw std::runtime_error("Ambiguous native UI fingerprint.");
        entry->key=next++;std::set<std::string> objects;bool first=true;Rect bounds{};
        for(const auto& obj:r.at("objects").as_array()) {
            if(next>32768)throw std::runtime_error("Native UI object limit exceeded.");const auto& o=obj.as_object();NativeObject item;
            item.name=std::string(o.at("name").as_string());item.parent=std::string(o.at("parent").as_string());item.type=std::string(o.at("type").as_string());
            if(item.name.empty()||item.name.size()>96||!objects.insert(item.name).second)throw std::runtime_error("Duplicate native UI object identity.");
            const auto& a=o.at("rect").as_array();if(a.size()!=4)throw std::runtime_error("Invalid native UI bounds.");
            item.bounds={a[0].to_number<double>(),a[1].to_number<double>(),a[2].to_number<double>(),a[3].to_number<double>()};
            for(auto v:{item.bounds.x,item.bounds.y,item.bounds.w,item.bounds.h})if(!std::isfinite(v)||std::abs(v)>32768)throw std::runtime_error("Native UI bounds exceed limits.");
            if(item.bounds.w<0||item.bounds.h<0)throw std::runtime_error("Negative native UI bounds.");
            if(item.bounds.w>0&&item.bounds.h>0){if(first){bounds=item.bounds;first=false;}else{const auto right=std::max(bounds.x+bounds.w,item.bounds.x+item.bounds.w),bottom=std::max(bounds.y+bounds.h,item.bounds.y+item.bounds.h);bounds.x=std::min(bounds.x,item.bounds.x);bounds.y=std::min(bounds.y,item.bounds.y);bounds.w=right-bounds.x;bounds.h=bottom-bounds.y;}}
            item.key=next++;entry->objects.push_back(std::move(item));
        }
        for(const auto& object:entry->objects){std::set<std::string> visited;auto parent=object.parent;
            while(!parent.empty()) {if(!visited.insert(parent).second)throw std::runtime_error("Cyclic native UI hierarchy.");
                auto it=std::ranges::find(entry->objects,parent,&NativeObject::name);if(it==entry->objects.end())throw std::runtime_error("Missing native UI parent.");parent=it->parent;}}
        for(auto& object:entry->objects)if(!object.parent.empty())object.parent_index=size_t(std::ranges::find(entry->objects,object.parent,&NativeObject::name)-entry->objects.begin());
        entry->binding={entry->name,GroupCount,size,hash,hash,bounds};
        for(const auto& known:RuntimeResources)if(known.resource==entry->name){if(known.bytes!=size||known.english!=hash)throw std::runtime_error("Known UI fingerprint mismatch.");entry->binding.group=known.group;entry->binding.bounds=known.bounds;}
        out->resources.push_back(std::move(entry));
    }
    for(const auto& resource:out->resources)for(size_t n=0;n<resource->objects.size();++n) {
        std::uint64_t hash=0xcbf29ce484222325;for(unsigned char c:resource->objects[n].name)hash=(hash*0x100000001b3)^c;
        out->name_index.push_back({hash,resource.get(),n});
    }
    std::ranges::sort(out->name_index,{},&NameEntry::hash);
    return out;
}
const NativeResource* NativeCatalog::Find(std::string_view name) const {for(const auto& r:resources)if(r->name==name)return r.get();return nullptr;}
const NativeResource* NativeCatalog::Identify(std::span<const unsigned char> bytes) const {
    if(bytes.size()<5||std::memcmp(bytes.data(),"<?xml",5)||!std::ranges::any_of(resources,[&](const auto& r){return r->binding.bytes==bytes.size();}))return nullptr;
    std::uint64_t hash=0xcbf29ce484222325;for(auto c:bytes)hash=(hash^c)*0x100000001b3;
    for(const auto& r:resources)if(r->binding.bytes==bytes.size()&&r->binding.english==hash)return r.get();return nullptr;
}
const NativeResource* NativeCatalog::Resolve(int handle,std::span<const std::uint64_t> chain,std::string_view type,bool* ambiguous,std::span<const std::uint64_t> tree) const {
    if(ambiguous)*ambiguous=false;
    if(handle<=0||chain.empty()||chain.size()>32||type.empty()||!chain[0])return nullptr;
    char suffix[32];const int count=std::snprintf(suffix,sizeof suffix,"_%09d",handle);
    // Invert just the known handle suffix of the game's FNV-1 name key. The
    // immutable name index avoids scanning/re-hashing all XML objects per draw.
    auto hash=chain[0];for(int n=count-1;n>=0;--n)hash=(hash^static_cast<unsigned char>(suffix[n]))*0xce965057aff6957bull;
    const NativeResource* match=nullptr;
    for(auto it=std::ranges::lower_bound(name_index,hash,{},&NameEntry::hash);it!=name_index.end()&&it->hash==hash;++it) {
        const auto& objects=it->resource->objects;size_t object=it->object,depth=0;
        if(objects[object].type!=type)continue;
        while(depth<chain.size()&&object!=size_t(-1)&&NativeName(objects[object].name,handle)==chain[depth]) {
            object=objects[object].parent_index;++depth;
        }
        if(depth!=chain.size()||object!=size_t(-1))continue;
        if(!tree.empty()) {
            if(tree.size()!=objects.size())continue;bool same=true;
            for(const auto& o:objects)if(std::ranges::find(tree,NativeName(o.name,handle))==tree.end()){same=false;break;}
            if(!same)continue;
        }
        if(match&&match!=it->resource){if(ambiguous)*ambiguous=true;return nullptr;}
        match=it->resource;
    }
    return match;
}
Identity IdentifyHandle(const NativeResource* r,int handle) {
    auto id=IdentifyHandle(&r->binding,handle);id.native=r;id.object_keys.reserve(r->objects.size());
    for(const auto& o:r->objects)id.object_keys.emplace_back(NativeName(o.name,handle),o.key);
    std::ranges::sort(id.object_keys);return id;
}
std::vector<CompiledElement> CompileElements(const Preset& p) {
    std::vector<CompiledElement> out;out.reserve(p.elements.size());
    for(const auto& e:p.elements) {
        const auto* r=GetNativeCatalog().Find(e.resource);if(!r)throw std::runtime_error("UI override resource is absent from the verified native catalog.");
        auto key=r->key;auto bounds=r->binding.bounds;
        if(!e.object.empty()){const auto it=std::ranges::find(r->objects,e.object,&NativeObject::name);if(it==r->objects.end())throw std::runtime_error("UI override object is absent from the verified native catalog.");key=it->key;bounds=it->bounds;}
        out.push_back({key,e.value,bounds});
    }
    std::ranges::sort(out,{},&CompiledElement::key);return out;
}
std::optional<Visual> ElementTransform(Visual v,const Identity& id,std::span<const std::uint64_t> chain,std::span<const CompiledElement> edits,Visual affine) {
    auto apply=[&](std::uint32_t key) {
        auto it=std::ranges::lower_bound(edits,key,{},&CompiledElement::key);if(it==edits.end()||it->key!=key)return true;
        if(!it->value.visible)return false;
        const auto& s=it->value;const double px=it->bounds.x*affine.sx+affine.x,py=it->bounds.y*affine.sy+affine.y;
        affine.x=float(affine.x*s.scale+px*(1-s.scale)+s.x);affine.y=float(affine.y*s.scale+py*(1-s.scale)+s.y);
        affine.sx*=float(s.scale);affine.sy*=float(s.scale);return true;
    };
    if(id.native&&!edits.empty()) {
        if(!apply(id.native->key))return {};
        for(auto it=chain.rbegin();it!=chain.rend();++it){auto obj=std::ranges::lower_bound(id.object_keys,*it,{},&std::pair<std::uint64_t,std::uint32_t>::first);
            if(obj!=id.object_keys.end()&&obj->first==*it&&!apply(obj->second))return {};}
    }
    v={v.x*affine.sx+affine.x,v.y*affine.sy+affine.y,v.sx*affine.sx,v.sy*affine.sy};
    for(float x:{v.x,v.y,v.sx,v.sy})if(!std::isfinite(x))return {};return v;
}

}
