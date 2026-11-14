// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4rt_hud_runtime.h"
#include "ps4rt_hud_hook.h"
#include "ps4rt_ultrawide.h"
#include "hud_runtime_model.h"
#include "hud_live.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "common/types.h"

extern "C" int runtime_memory_is_mapped(uintptr_t address,uint64_t bytes);
namespace Ps4RtHud {
namespace {
using LoadFn=int(PS4_SYSV_ABI *)(void*,const void*,unsigned,float);
using RenderFn=void(PS4_SYSV_ABI *)(void*,void*,void*);
LoadFn load_original{};RenderFn render_original{};std::array<RenderFn,5> specialized_original{};HookSet<18> hooks;
std::vector<So5Hud::CompiledElement> startup_elements;
bool Mapped(const void*,size_t);
struct NativeBounds {float left=0,top=0,right=0,bottom=0;unsigned vertices=0;
    void Add(float x,float y){if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>32768||std::abs(y)>32768)return;
        if(!vertices){left=right=x;top=bottom=y;}else{left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}++vertices;}
};
thread_local NativeBounds* census_bounds=nullptr;
struct CensusRecord {std::atomic<std::uintptr_t> object{0};std::atomic<unsigned> state{0};std::uintptr_t vtable=0,caller=0;int handle=0;std::uint64_t name=0;NativeBounds bounds;bool minimap=false;
    std::array<std::pair<int,std::uint64_t>,8> ancestors{};size_t depth=0;};
std::array<CensusRecord,512> census;
std::uintptr_t game_base=0;
std::filesystem::path mapping_report;
using MinimapVisibleFn=std::uintptr_t(PS4_SYSV_ABI *)(void*,int);
MinimapVisibleFn minimap_visible_original{};
using MinimapUpdateFn=std::uintptr_t(PS4_SYSV_ABI *)(void*);
MinimapUpdateFn minimap_update_original{};
using MinimapDrawFn=void(PS4_SYSV_ABI *)(void*,float);
using SubmitFn=void(PS4_SYSV_ABI *)(void*,int,unsigned,unsigned);
MinimapDrawFn minimap_draw_original{},minimap_mask_original{};SubmitFn submit_original{};
std::atomic<std::uintptr_t> minimap_root{0};std::atomic<int> minimap_visible{-1};
std::atomic<int> minimap_handle{0};std::atomic<std::uint64_t> minimap_generation{0};
std::array<std::atomic<unsigned>,2> minimap_states{};
// Fixed-size, first successful draw only. Unknown procedural UI stays unchanged;
// its emitted bounds and native identity provide evidence for the next binding.
void ObserveUnknown(void* object,void* a,void* b,RenderFn original,std::uintptr_t caller) {
    const auto key=reinterpret_cast<std::uintptr_t>(object);
    auto& row=census[key==minimap_root.load()?0:1+(key>>4)%(census.size()-1)];
    auto empty=std::uintptr_t(0);row.object.compare_exchange_strong(empty,key);
    unsigned ready=0;if(row.object.load()!=key||!row.state.compare_exchange_strong(ready,1)){original(object,a,b);return;}
    NativeBounds bounds;auto* previous=census_bounds;census_bounds=&bounds;original(object,a,b);census_bounds=previous;
    if(!bounds.vertices){row.state.store(0);return;}
    row.vtable=0;std::memcpy(&row.vtable,object,sizeof row.vtable);row.caller=caller;row.minimap=key==minimap_root.load();
    std::memcpy(&row.handle,static_cast<unsigned char*>(object)+0x3c,4);std::memcpy(&row.name,static_cast<unsigned char*>(object)+0x68,8);
    void* node=object;std::array<void*,8> visited{};row.depth=0;
    while(node&&row.depth<visited.size()&&Mapped(node,0x70)&&std::find(visited.begin(),visited.begin()+row.depth,node)==visited.begin()+row.depth){
        visited[row.depth]=node;int handle;std::uint64_t name;
        std::memcpy(&handle,static_cast<unsigned char*>(node)+0x3c,4);std::memcpy(&name,static_cast<unsigned char*>(node)+0x68,8);
        row.ancestors[row.depth++]={handle,name};std::memcpy(&node,static_cast<unsigned char*>(node)+0x10,sizeof node);
    }
    row.bounds=bounds;row.state.store(2,std::memory_order_release);
}
struct DrawScope {bool enabled=false;So5Hud::Visual affine{0,0,1,1};};
thread_local DrawScope draw_scope;
std::atomic<std::uint64_t> native_frame=0;
std::atomic<std::uint64_t> visibility_epoch=0;
std::atomic<std::uint64_t> forced_draws=0;
std::array<std::atomic<std::uint64_t>,4096> handle_generations{};
// Alpha-only presentation history. Never retain a guest object, component,
// texture pointer, command buffer or draw callback for later execution. Only
// objects still submitted by the game's own UI queue can be held visible.
struct VisibilityHistory {
    std::uintptr_t key=0,vtable=0;int handle=0,kind=0;std::uint64_t name=0,generation=0,frame=0;
    size_t count=0;bool held=false;std::uint64_t epoch=0;std::array<unsigned char,256> alpha{};
};
thread_local std::array<VisibilityHistory,512> visibility_history{};
struct VisibilityScope {VisibilityHistory* history=nullptr;size_t at=0;bool replay=false;};
thread_local VisibilityScope visibility_scope;
unsigned PreviewColour(unsigned colour) {
    auto& s=visibility_scope;if(!s.history)return colour;
    if(s.at<s.history->alpha.size()) {
        if(s.replay){if(s.at<s.history->count)colour=(colour&0xffffffu)|(unsigned(s.history->alpha[s.at])<<24);}
        else s.history->alpha[s.at]=static_cast<unsigned char>(colour>>24);
    }
    ++s.at;return colour;
}
using VertexFn=void(PS4_SYSV_ABI *)(void*,const float*,const void*,unsigned);
using VertexExFn=void(PS4_SYSV_ABI *)(void*,const float*,const void*,const void*,unsigned,float,float);
VertexFn vertex_original{},batch_vertex_original{};VertexExFn extended_vertex_original{},extended_batch_original{};
std::atomic<std::uint64_t> transformed_vertices=0,vertex_rejected=0,backdrop_draws=0;
// Native UI emits positions from the CURRENT thread/fiber stack. These stacks
// are Windows allocations, not necessarily in the guest GPU mapping registry.
// Fiber switching maintains the TEB bounds. Read them afresh (never cache across
// a switch), require the source above our active frame, then fall back to the
// existing guest-range check for heap positions. No speculative pointer reads.
bool PositionReadable(const float* p) {
    const auto address=reinterpret_cast<std::uintptr_t>(p);
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto limit=reinterpret_cast<std::uintptr_t>(tib->StackLimit),base=reinterpret_cast<std::uintptr_t>(tib->StackBase);
    unsigned char active_frame;
    const auto current=reinterpret_cast<std::uintptr_t>(&active_frame);
    if(current>=limit&&current<base&&address>=current&&address<base&&base-address>=16)return true;
    return Mapped(p,16);
}
// Copies preserve depth/W, UV, colour, and game-owned vertex buffers. The scope
// is restricted to a recognized native UI object's synchronous draw call.
bool VertexPosition(const float* p,float* next) {
    if(!draw_scope.enabled&&!census_bounds)return false;
    if(!PositionReadable(p)){vertex_rejected.fetch_add(1,std::memory_order_relaxed);return false;}
    if(census_bounds)census_bounds->Add(p[0],p[1]);
    if(!draw_scope.enabled)return false;
    std::memcpy(next,p,16);next[0]=p[0]*draw_scope.affine.sx+draw_scope.affine.x;next[1]=p[1]*draw_scope.affine.sy+draw_scope.affine.y;
    if(!std::isfinite(next[0])||!std::isfinite(next[1]))return false;
    transformed_vertices.fetch_add(1,std::memory_order_relaxed);return true;
}
void PS4_SYSV_ABI Vertex(void* context,const float* p,const void* uv,unsigned colour) {
    alignas(16) float next[4];vertex_original(context,VertexPosition(p,next)?next:p,uv,PreviewColour(colour));
}
void PS4_SYSV_ABI BatchVertex(void* context,const float* p,const void* uv,unsigned colour) {
    alignas(16) float next[4];batch_vertex_original(context,VertexPosition(p,next)?next:p,uv,PreviewColour(colour));
}
void PS4_SYSV_ABI ExtendedVertex(void* context,const float* p,const void* uv,const void* data,unsigned colour,float u,float v) {
    alignas(16) float next[4];extended_vertex_original(context,VertexPosition(p,next)?next:p,uv,data,PreviewColour(colour),u,v);
}
void PS4_SYSV_ABI ExtendedBatchVertex(void* context,const float* p,const void* uv,const void* data,unsigned colour,float u,float v) {
    alignas(16) float next[4];extended_batch_original(context,VertexPosition(p,next)?next:p,uv,data,PreviewColour(colour),u,v);
}
std::atomic<bool> active=false;
So5Hud::Preset preset;
So5Hud::LiveState live_state;
std::unique_ptr<So5Hud::LiveServer> live_server;
double output_width=1920,output_height=1080;
std::array<std::atomic<const So5Hud::Identity*>,4096> handles{};
std::vector<std::unique_ptr<const So5Hud::Identity>> identities;
std::mutex load_mutex;
std::array<std::atomic<std::uint64_t>,So5Hud::GroupCount> rendered{};
std::atomic<unsigned> recognized=0,unknown=0,refused=0;
std::atomic<unsigned> resolved_instances=0;
std::atomic<std::uint64_t> render_calls=0,mapped_objects=0,specialized_draws=0,element_draws=0;
size_t Slot(int handle){return static_cast<unsigned>(handle)%handles.size();}
const So5Hud::Identity* Find(int handle) {
    if(handle<=0)return nullptr;
    const auto* id=handles[Slot(handle)].load(std::memory_order_acquire);
    return id&&id->handle==handle?id:nullptr;
}
using AllocateFn=int(PS4_SYSV_ABI *)(void*);
AllocateFn allocate_original{};
int PS4_SYSV_ABI Allocate(void* manager) {
    const int handle=allocate_original(manager);
    if(handle>0){handles[Slot(handle)].store(nullptr,std::memory_order_release);++handle_generations[Slot(handle)];}
    return handle;
}

template<class T> T Field(const void* p,size_t offset) {
    T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+offset,sizeof v);return v;
}
void ObserveMinimapRoot(void* root,int requested) {
    if(Mapped(root,0xe6)&&Field<std::uintptr_t>(root,0)==game_base+0x971218) {
            const int visible=Field<unsigned char>(root,0xe5)!=0;
            const auto address=reinterpret_cast<std::uintptr_t>(root);
            // Refresh lifetime identity only through the verified minimap owner,
            // never merely because an unrelated draw reuses the same address.
            if(requested!=-2){const int handle=Field<int>(root,0x3c);
                minimap_handle.store(handle,std::memory_order_relaxed);
                minimap_generation.store(handle_generations[Slot(handle)].load(std::memory_order_relaxed),std::memory_order_relaxed);}
            const auto previous_root=minimap_root.exchange(address);const auto previous_visible=minimap_visible.exchange(visible);
            const bool changed=previous_root!=address||previous_visible!=visible;
            if(changed){minimap_states[visible].fetch_add(1,std::memory_order_relaxed);
                std::printf("HUD layout minimap: native visibility=%d requested=%d handle=%d name=%016llx type=%llx; original behavior retained\n",visible,requested,Field<int>(root,0x3c),
                    static_cast<unsigned long long>(Field<std::uint64_t>(root,0x68)),static_cast<unsigned long long>(Field<std::uintptr_t>(root,0)-game_base));}
    }
}
void ObserveMinimap(void* instance,int requested) {
    if(Mapped(instance,0x6cf)&&Field<std::uintptr_t>(instance,0)==game_base+0x96fb68) {
        auto* root=Field<void*>(instance,0x6c0);
        if(Mapped(root,0xe6)&&Field<std::uintptr_t>(root,0)==game_base+0x971218)ObserveMinimapRoot(root,requested);
        else {minimap_root.store(0,std::memory_order_release);minimap_handle.store(0);}
    }
}
bool IsMinimapComposite(void* object,int handle) {
    return handle>0&&reinterpret_cast<std::uintptr_t>(object)==minimap_root.load(std::memory_order_acquire)&&
        handle==minimap_handle.load(std::memory_order_relaxed)&&Field<std::uintptr_t>(object,0)==game_base+0x971218&&
        !Field<std::uint64_t>(object,0x68)&&!Field<void*>(object,0x10)&&
        handle_generations[Slot(handle)].load(std::memory_order_relaxed)==minimap_generation.load(std::memory_order_relaxed);
}
// The frame is a GUI Image, but map/cone/markers are separate procedural
// streams. Scope their native submission to the verified owner/context only.
struct MinimapStreamScope {void* owner=nullptr;void* context=nullptr;So5Hud::Visual affine{0,0,1,1};bool hidden=false,mask=false;};
thread_local MinimapStreamScope minimap_stream;
std::atomic<std::uint64_t> minimap_stream_vertices=0,minimap_stream_refused=0;
std::atomic<std::uint64_t> minimap_mask_vertices=0;
// Exact read-only equivalent of native f15e0 -> 130e40 -> 6f9a0, slot zero.
// The circle is a separate four-vertex stencil pass, not the frame or map
// stream. Follow the current native rotating buffer, never retain its pointer.
unsigned char* MinimapMaskBuffer(void* context) {
    if(!Mapped(context,0x334)||Field<unsigned char>(context,0x333)<1)return nullptr;
    auto* slots=Field<void*>(context,0x70);if(!Mapped(slots,8))return nullptr;
    auto* slot=Field<void*>(slots,0);if(!Mapped(slot,0x58))return nullptr;
    auto* format=Field<void*>(slot,0x18);if(!Mapped(format,0x1a)||Field<unsigned short>(format,0x18)!=52)return nullptr;
    auto* buffer=Field<void*>(slot,0x50);if(!Mapped(buffer,0x4e))return nullptr;
    const auto index=Field<unsigned char>(buffer,0x4d)?Field<unsigned char>(buffer,0x4c):0;
    if(index>2)return nullptr;
    auto* data=Field<unsigned char*>(buffer,0x20+index*8);
    return Mapped(data,4*52)?data:nullptr;
}
bool TransformMinimapMask(void* context,unsigned count,const So5Hud::Visual& affine) {
    if(count!=4)return false;auto* data=MinimapMaskBuffer(context);if(!data)return false;
    for(unsigned n=0;n<4;++n){const float x=Field<float>(data+n*52,0),y=Field<float>(data+n*52,4);
        if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>32768||std::abs(y)>32768||
           !std::isfinite(x*affine.sx+affine.x)||!std::isfinite(y*affine.sy+affine.y))return false;}
    for(unsigned n=0;n<4;++n){auto* p=data+n*52;const float x=Field<float>(p,0)*affine.sx+affine.x,y=Field<float>(p,4)*affine.sy+affine.y;
        std::memcpy(p,&x,4);std::memcpy(p+4,&y,4);}
    minimap_mask_vertices.fetch_add(4,std::memory_order_relaxed);return true;
}
bool TransformMinimapStream(void* owner,unsigned count,So5Hud::Visual affine) {
    if(!count||count>516||!Mapped(owner,0x708)||Field<int>(owner,0x6f0)!=52)return false;
    const int written=Field<int>(owner,0x700);
    if(written<=0||unsigned(written)>count)return false;
    const auto end=reinterpret_cast<std::uintptr_t>(Field<void*>(owner,0x6f8));const auto bytes=size_t(written)*52;
    if(end<bytes)return false;auto* start=reinterpret_cast<unsigned char*>(end-bytes);
    if(!Mapped(start,size_t(count)*52))return false;
    // Validate the complete range first. Preserve UVs, colour, depth, W and
    // every non-position byte. The marker batch submits 516 vertices, but only
    // rewrites its visible prefix when the marker count is unchanged. The
    // retained transparent padding must never be transformed a second time.
    for(int n=0;n<written;++n){const auto x=Field<float>(start+n*52,0),y=Field<float>(start+n*52,4);
        if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>32768||std::abs(y)>32768||
           !std::isfinite(x*affine.sx+affine.x)||!std::isfinite(y*affine.sy+affine.y))return false;}
    for(int n=0;n<written;++n){auto* p=start+n*52;const float x=Field<float>(p,0)*affine.sx+affine.x,y=Field<float>(p,4)*affine.sy+affine.y;
        std::memcpy(p,&x,4);std::memcpy(p+4,&y,4);}
    minimap_stream_vertices.fetch_add(written,std::memory_order_relaxed);return true;
}
void PS4_SYSV_ABI Submit(void* context,int slot,unsigned count,unsigned mode) {
    const auto& s=minimap_stream;
    if(s.owner&&s.context==context&&slot>=0&&slot<=4) {
        if(s.hidden){submit_original(context,slot,0,mode);return;}
        if(s.affine.x||s.affine.y||s.affine.sx!=1||s.affine.sy!=1){
            const bool ok=s.mask?(slot==0&&TransformMinimapMask(context,count,s.affine)):TransformMinimapStream(s.owner,count,s.affine);
            if(!ok)++minimap_stream_refused;
        }
    }
    submit_original(context,slot,count,mode);
}
void DrawMinimapPass(void* owner,float alpha,MinimapDrawFn original,bool mask) {
    if(!Mapped(owner,0x714)||Field<std::uintptr_t>(owner,0)!=game_base+0x96fb68){original(owner,alpha);return;}
    ObserveMinimap(owner,-1);auto* frame=Field<void*>(owner,0x6c0);
    if(!Mapped(frame,0xe6)||!IsMinimapComposite(frame,Field<int>(frame,0x3c))){original(owner,alpha);return;}
    auto snapshot=live_state.Current();const auto& layout=snapshot?snapshot->layout:preset;
    const auto affine=So5Hud::Transform({0,0,1,1},So5Hud::MinimapComposite,layout.groups[1],output_width,output_height);
    if(!affine){original(owner,alpha);return;}
    struct Restore {MinimapStreamScope old;~Restore(){minimap_stream=old;}} restore{minimap_stream};
    minimap_stream={owner,Field<void*>(owner,mask?0x6d8:0x6e8),*affine,!layout.groups[1].visible||!layout.groups[26].visible,mask};
    original(owner,alpha);
}
void PS4_SYSV_ABI MinimapDraw(void* owner,float alpha){DrawMinimapPass(owner,alpha,minimap_draw_original,false);}
void PS4_SYSV_ABI MinimapMask(void* owner,float alpha){DrawMinimapPass(owner,alpha,minimap_mask_original,true);}
// Registry entries associate both exact metadata and native ATD pointers. A
// shared Number renderer or a matching colour alone is never a HUD identity.
void* NativeAtlas(unsigned index) {
    if(index>5)return nullptr;auto* global=reinterpret_cast<void*>(game_base+0xb83a70);
    if(!Mapped(global,8))return nullptr;auto* manager=Field<void*>(global,0);
    if(!Mapped(manager,16+index*8))return nullptr;auto* entry=Field<void*>(manager,8+index*8);
    if(!Mapped(entry,0x38)||Field<int>(entry,0x30)!=int(index)||
       Field<std::uintptr_t>(entry,0x28)!=game_base+0xa03360+index*48)return nullptr;
    return Field<void*>(entry,0x20);
}
const So5Hud::Identity* DamageIdentity(void* object) {
    if(!Mapped(object,0x12c)||Field<std::uintptr_t>(object,0)!=game_base+0x971148)return nullptr;
    auto* atlas=NativeAtlas(1);if(!atlas||Field<void*>(object,0xf8)!=atlas||!Mapped(atlas,0x18)||Field<unsigned>(atlas,0x14)!=11)return nullptr;
    const auto colour=Field<unsigned>(object,0xdc)&0xffffffu;
    const unsigned r=colour&255,g=(colour>>8)&255,b=(colour>>16)&255;
    const unsigned category=r>g&&r>b?1:g>r&&g>b?2:0;
    static const std::array<So5Hud::Identity,3> ids={So5Hud::Identity{.resource=&So5Hud::ProceduralResources[0]},
        So5Hud::Identity{.resource=&So5Hud::ProceduralResources[1]},So5Hud::Identity{.resource=&So5Hud::ProceduralResources[2]}};
    return &ids[category];
}
const So5Hud::Identity* SubtitleIdentity(void* object) {
    // The caption path assigns CAPTIONFONT20 through 1fd940(index 0) and
    // 207f10. Match the live font descriptor, never text or screen position.
    if(!Mapped(object,0x100)||Field<std::uintptr_t>(object,0)!=game_base+0x971078)return nullptr;
    auto* global=reinterpret_cast<void*>(game_base+0xb83a08);
    auto* file_id=reinterpret_cast<void*>(game_base+0xa35a28);
    if(!Mapped(global,8)||!Mapped(file_id,4))return nullptr;
    auto* manager=Field<void*>(global,0);
    if(!Mapped(manager,0x70)||Field<std::uintptr_t>(manager,0)!=game_base+0x970948)return nullptr;
    auto* descriptor=Field<void*>(manager,0x68);const int expected=Field<int>(file_id,0);
    if(expected<=0||!Mapped(descriptor,0x20)||Field<int>(descriptor,0)!=expected)return nullptr;
    auto* font=Field<void*>(descriptor,0x10);auto* renderer=Field<void*>(object,0xf8);
    if(!font||!Mapped(renderer,0x68)||Field<void*>(renderer,0x60)!=font)return nullptr;
    static const So5Hud::Identity identity{.resource=&So5Hud::Subtitles};return &identity;
}
std::uintptr_t PS4_SYSV_ABI MinimapVisibility(void* instance,int requested) {
    const auto result=minimap_visible_original(instance,requested);ObserveMinimap(instance,requested);return result;
}
std::uintptr_t PS4_SYSV_ABI MinimapUpdate(void* instance) {
    const auto result=minimap_update_original(instance);ObserveMinimap(instance,-1);return result;
}
bool Mapped(const void* p,size_t bytes) {
    return p&&runtime_memory_is_mapped(reinterpret_cast<uintptr_t>(p),bytes);
}
void Store(void* p,size_t offset,float v) {std::memcpy(static_cast<unsigned char*>(p)+offset,&v,4);}
bool FullBlackMask(void* object,int kind) {
    // Only the verified native Rect draw implementation, a full-screen quad,
    // and FOUR black corners. Text, images, movies and partial black panels do
    // not qualify. Preserve the game's alpha/fade progression.
    if(kind!=4||!Mapped(object,0x110))return false;
    const float x=Field<float>(object,0x80),y=Field<float>(object,0x84);
    const float w=Field<float>(object,0xb0),h=Field<float>(object,0xb4);
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w)||!std::isfinite(h)||
       std::abs(x)>1.1f||std::abs(y)>1.1f||std::abs(w-1920)>2.1f||std::abs(h-1080)>2.1f)return false;
    for(unsigned i=0;i<4;++i)if(Field<unsigned>(object,0x100+i*4)&0xffffff)return false;
    return true;
}
bool FullBlackBackdrop(void* object,int kind) {return std::abs(output_width/output_height-16./9)>.001&&FullBlackMask(object,kind);}
int PS4_SYSV_ABI Load(void* controller,const void* raw,unsigned bytes,float f) {
    const So5Hud::ResourceBinding* resource=nullptr;const So5Hud::NativeResource* native=nullptr;
    if(bytes<=1048576&&Mapped(raw,bytes)) {
        const auto data=std::span(static_cast<const unsigned char*>(raw),bytes);native=So5Hud::GetNativeCatalog().Identify(data);
        resource=native?&native->binding:So5Hud::IdentifyResource(data);
    }
    const int handle=load_original(controller,raw,bytes,f);
    if(handle<=0)return handle;
    std::scoped_lock lock(load_mutex);
    // Every load clears reused handles, including unknown resource revisions.
    handles[Slot(handle)].store(nullptr,std::memory_order_release);
    if(!resource||identities.size()>=65536) {++unknown;return handle;}
    auto next=std::make_unique<const So5Hud::Identity>(native?So5Hud::IdentifyHandle(native,handle):So5Hud::IdentifyHandle(resource,handle));
    identities.push_back(std::move(next));handles[Slot(handle)].store(identities.back().get(),std::memory_order_release);
    ++recognized;
    const auto label=resource->group<So5Hud::GroupCount?So5Hud::Specs[resource->group].id:std::string_view("object editor");
    std::printf("HUD layout: bound %.*s (handle %d, group %.*s)\n",int(resource->resource.size()),resource->resource.data(),handle,int(label.size()),label.data());
    return handle;
}
size_t TreeNames(void* root,int handle,std::array<std::uint64_t,256>& names) {
    // Native UiObject: first child +18, next sibling +28. Complete bounded
    // identity only; no geometry/visibility guesses and no game-owned writes.
    std::array<void*,256> pending{},visited{};size_t waiting=1,seen=0,count=0,edges=0;pending[0]=root;
    while(waiting) {
        auto* node=pending[--waiting];
        if(!Mapped(node,0x70)||seen==visited.size()||std::find(visited.begin(),visited.begin()+seen,node)!=visited.begin()+seen)return 0;
        visited[seen++]=node;if(Field<int>(node,0x3c)!=handle)continue;
        const auto name=Field<std::uint64_t>(node,0x68);
        if(!name||count==names.size()||std::find(names.begin(),names.begin()+count,name)!=names.begin()+count)return 0;
        names[count++]=name;
        for(auto* child=Field<void*>(node,0x18);child;) {
            if(!Mapped(child,0x70)||waiting==pending.size()||++edges>256)return 0;
            pending[waiting++]=child;child=Field<void*>(child,0x28);
        }
    }
    return count;
}
// Child GUI resources (notably BM_Icon) have their own handle. Inherit a group
// only from a fully readable, bounded parent chain with a VERIFIED group root.
// The child's own identity still supplies its independent per-object edits.
const So5Hud::Identity pause_controls{.resource=&So5Hud::PauseControls};
const So5Hud::Identity* AttachedGroup(void* object) {
    std::array<void*,32> visited{};std::array<std::uint64_t,32> names{};
    size_t seen=0,count=0;int previous=0;const So5Hud::Identity* candidate=nullptr;
    for(auto* node=object;node;node=Field<void*>(node,0x10)) {
        if(seen==visited.size()||!Mapped(node,0x70)||std::find(visited.begin(),visited.begin()+seen,node)!=visited.begin()+seen)return nullptr;
        visited[seen++]=node;const int handle=Field<int>(node,0x3c);
        if(handle!=previous){
            if(candidate&&So5Hud::IsPauseControls(*candidate,{names.data(),count}))return &pause_controls;
            if(candidate&&candidate->Accepts({names.data(),count}))return candidate;
            candidate=Find(handle);if(candidate&&candidate->resource->group>=So5Hud::GroupCount)candidate=nullptr;
            count=0;previous=handle;
        }
        names[count++]=Field<std::uint64_t>(node,0x68);
    }
    if(candidate&&So5Hud::IsPauseControls(*candidate,{names.data(),count}))return &pause_controls;
    return candidate&&candidate->Accepts({names.data(),count})?candidate:nullptr;
}
// Result item rows are cloned COLUMN.GUI instances, owned by the native result
// controller rather than parented below RESULT.GUI. Resolve those handles from
// its four bounded active/pending item lists; neither atlas names nor screen coordinates
// establish ownership. Recheck live pointers so recycled handles cannot linger.
const So5Hud::Identity* ResultItemsGroup(int handle) {
    if(handle<=0||!Mapped(reinterpret_cast<void*>(game_base+0xb83ad0),8))return nullptr;
    auto* controller=Field<void*>(reinterpret_cast<void*>(game_base+0xb83ad0),0);
    if(!Mapped(controller,0x218)||Field<std::uintptr_t>(controller,0)!=game_base+0x9700c8)return nullptr;
    static const So5Hud::Identity identity{.resource=&So5Hud::RuntimeResources[2]};
    for(size_t offset:{0x80u,0xc8u,0x120u,0x168u}) {
        if(Field<std::uintptr_t>(controller,offset)!=game_base+0xa03d08)continue;
        auto* rows=Field<void*>(controller,offset+8);const auto count=Field<std::int64_t>(controller,offset+0x18);
        if(count<=0||count>28||!Mapped(rows,size_t(count)*8))continue;
        for(std::int64_t n=0;n<count;++n) {
            auto* row=Field<void*>(rows,size_t(n)*8);
            if(!Mapped(row,0x48)||Field<std::uintptr_t>(row,0)!=game_base+0xa03cb8)continue;
            const auto start=reinterpret_cast<std::uintptr_t>(Field<void*>(row,0x10));
            const auto end=reinterpret_cast<std::uintptr_t>(Field<void*>(row,0x18));
            if(!start||end<start+8||end-start>64||!Mapped(reinterpret_cast<void*>(start),8))continue;
            auto* instance=Field<void*>(reinterpret_cast<void*>(start),0);
            if(Mapped(instance,0x94)&&Field<std::uintptr_t>(instance,0)==game_base+0x971780&&Field<int>(instance,0x90)==handle)return &identity;
        }
    }
    return nullptr;
}
using LockedChestUpdateFn=void(PS4_SYSV_ABI *)(void*);
using ModelTintFn=void(PS4_SYSV_ABI *)(void*,const float*);
LockedChestUpdateFn locked_chest_original{};ModelTintFn model_tint{};
std::atomic<std::uint64_t> locked_chest_observed=0,locked_chest_hidden=0;
// KagiIcon.aif / DisableMark is a world billboard, not a GUI Image. Preserve
// its native update, lifetime, chest state and projection. Only the verified
// model's presentation tint is changed, and native colour/fade is restored on
// the next update when visibility is re-enabled. No guest pointer is retained.
void ApplyLockedChestVisibility(void* owner) {
    if(!Mapped(owner,0x88)||Field<std::uintptr_t>(owner,0)!=game_base+0x9835d0||
       Field<unsigned char>(owner,0x80)!=1||Field<std::uint64_t>(owner,0x18)>10)return;
    auto* model=Field<void*>(owner,0x40);
    if(!Mapped(model,0x6c0)||Field<std::uintptr_t>(model,0)!=game_base+0x962388||!model_tint)return;
    alignas(16) float colour[4];std::memcpy(colour,static_cast<unsigned char*>(owner)+0x70,16);
    for(float value:colour)if(!std::isfinite(value)||value<0||value>1)return;
    ++locked_chest_observed;
    const auto snapshot=live_state.Current();const auto& layout=snapshot?snapshot->layout:preset;
    if(!layout.groups[13].visible||!layout.groups[26].visible){colour[3]=0;++locked_chest_hidden;}
    if(std::memcmp(colour,static_cast<unsigned char*>(model)+0x530,16))model_tint(model,colour);
}
void PS4_SYSV_ABI LockedChestUpdate(void* owner) {
    locked_chest_original(owner);
    if(active.load(std::memory_order_acquire))ApplyLockedChestVisibility(owner);
}
struct ScopedVisibility {
    void* object;unsigned char original;VisibilityScope previous;bool changed=false;
    ScopedVisibility(void* p,const So5Hud::Identity* group,int kind,bool force):object(p),original(Field<unsigned char>(p,0xe5)),previous(visibility_scope) {
        visibility_scope={};if(!group)return;
        const auto key=reinterpret_cast<std::uintptr_t>(p);
        auto& row=visibility_history[((key>>4)^unsigned(kind+1))%visibility_history.size()];
        const int handle=Field<int>(p,0x3c);const auto name=Field<std::uint64_t>(p,0x68),vtable=Field<std::uintptr_t>(p,0);
        const auto generation=handle_generations[Slot(handle)].load(std::memory_order_relaxed),frame=native_frame.load(std::memory_order_relaxed);
        if(row.key!=key||row.handle!=handle||row.kind!=kind||row.name!=name||row.vtable!=vtable||row.generation!=generation)
            row={.key=key,.vtable=vtable,.handle=handle,.kind=kind,.name=name,.generation=generation};
        const auto epoch=visibility_epoch.load(std::memory_order_relaxed);
        if(!force||row.epoch!=epoch)row.held=false;row.epoch=epoch;
        if(force&&(original||(row.count&&frame>=row.frame&&frame-row.frame<=1)))row.held=true;
        if(force&&row.held&&row.count) {
            // Restore this one presentation byte before returning, even when a
            // nested native draw throws. No visibility/update/input hook patch.
            if(!original){static_cast<unsigned char*>(p)[0xe5]=1;changed=true;}
            visibility_scope={&row,0,true};
            forced_draws.fetch_add(1,std::memory_order_relaxed);
        }else if(original)visibility_scope={&row,0,false};
    }
    ~ScopedVisibility(){auto& s=visibility_scope;
        if(s.history&&!s.replay){s.history->count=s.at<=s.history->alpha.size()?s.at:0;s.history->frame=native_frame.load(std::memory_order_relaxed);}
        if(changed)static_cast<unsigned char*>(object)[0xe5]=original;visibility_scope=previous;
    }
};
void DrawObject(void* object,void* a,void* b,RenderFn original,int kind,std::uintptr_t caller) {
    render_calls.fetch_add(1,std::memory_order_relaxed);
    if(!Mapped(object,0xe6)) {original(object,a,b);return;}
    mapped_objects.fetch_add(1,std::memory_order_relaxed);
    thread_local std::uint64_t seen_generation=~std::uint64_t(0);
    thread_local std::shared_ptr<const So5Hud::LiveSnapshot> snapshot;
    const auto generation=live_state.Generation();
    if(generation!=seen_generation){snapshot=live_state.Current();seen_generation=generation;}
    const auto& layout=snapshot?snapshot->layout:preset;
    // Global native UI visibility leaves the verified loading mask intact.
    // It never changes GUI input/update state or the custom developer overlay.
    if(!layout.groups[26].visible&&!FullBlackMask(object,kind))return;
    // Preserve the native hidden-object fast path during ordinary play.
    if(!Field<unsigned char>(object,0xe5)&&(!snapshot||!snapshot->force_groups)){original(object,a,b);return;}
    if(reinterpret_cast<std::uintptr_t>(object)==minimap_root.load(std::memory_order_relaxed))ObserveMinimapRoot(object,-2);
    // +0x3c is the resource handle used by the native name hash.
    // +0x40 is a separate visibility-group index, not this handle.
    const int handle=Field<int>(object,0x3c);
    const auto* identity=Find(handle);
    // The frame is a GUI Image. MinimapDraw/Submit handle the separate native
    // map, cone and marker streams with the same group transform.
    static const So5Hud::Identity minimap_identity{.resource=&So5Hud::MinimapComposite};
    const bool minimap_composite=IsMinimapComposite(object,handle);
    if(minimap_composite)identity=&minimap_identity;
    if(const auto* damage=DamageIdentity(object))identity=damage;
    if(const auto* subtitle=SubtitleIdentity(object))identity=subtitle;
    const bool backdrop=FullBlackBackdrop(object,kind);
    if(backdrop&&!identity) {
        const auto fill=So5Hud::BackgroundTransform(output_width,output_height);
        struct Restore {DrawScope old;~Restore(){draw_scope=old;}} restore{draw_scope};
        draw_scope={true,fill};
        backdrop_draws.fetch_add(1,std::memory_order_relaxed);original(object,a,b);return;
    }
    const auto* attached=identity&&identity->resource->group<So5Hud::GroupCount?nullptr:AttachedGroup(object);
    if(!attached&&(!identity||identity->resource->group>=So5Hud::GroupCount))attached=ResultItemsGroup(handle);
    if(!identity&&!attached&&(handle<=0||!Field<std::uint64_t>(object,0x68)||So5Hud::GetNativeCatalog().resources.empty())) {
        ObserveUnknown(object,a,b,original,caller);return;
    }
    std::array<std::uint64_t,32> chain{};size_t count=0;
    auto* parent=object;void* tree_root=object;bool ancestry_valid=true;
    while(parent&&count<chain.size()) {
        if(!Mapped(parent,0x70)) {ancestry_valid=false;break;}
        if(Field<int>(parent,0x3c)!=handle)break; // boundary of this GUI resource
        tree_root=parent;chain[count++]=Field<std::uint64_t>(parent,0x68);
        parent=Field<void*>(parent,0x10);
        if(parent==object) {count=0;break;}
    }
    // A truncated ancestry could conceal FMBASE. Refuse instead of moving a menu.
    if(!ancestry_valid||count==chain.size()) {original(object,a,b);return;}
    if(!identity&&handle>0&&count) {
        const auto vtable=Field<std::uintptr_t>(object,0);
        const auto relative=vtable>=game_base?vtable-game_base:0;
        const std::string_view type=relative==0x971218?"Image":relative==0x971078?"Label":relative==0x970b20?"Div3":relative==0x971148?"Number":"";
        bool ambiguous=false;auto* native=So5Hud::GetNativeCatalog().Resolve(handle,{chain.data(),count},type,&ambiguous);
        if(!native&&ambiguous){std::array<std::uint64_t,256> names{};const auto size=TreeNames(tree_root,handle,names);
            if(size)native=So5Hud::GetNativeCatalog().Resolve(handle,{chain.data(),count},type,nullptr,{names.data(),size});}
        if(native) {
            // First instance only; never wait for another loader/render thread.
            std::unique_lock lock(load_mutex,std::try_to_lock);
            if(lock.owns_lock()) {
                identity=Find(handle);
                if(!identity&&identities.size()<65536) {
                    auto next=std::make_unique<const So5Hud::Identity>(So5Hud::IdentifyHandle(native,handle));
                    identity=next.get();identities.push_back(std::move(next));handles[Slot(handle)].store(identity,std::memory_order_release);
                    resolved_instances.fetch_add(1,std::memory_order_relaxed);
                    std::printf("HUD layout: bound cached instance %s (handle %d, complete native hierarchy)\n",native->name.c_str(),handle);
                }
            }
        }
    }
    if(!identity&&!attached){ObserveUnknown(object,a,b,original,caller);return;}
    const auto& edits=snapshot?snapshot->elements:startup_elements;
    const auto* group_identity=identity&&identity->resource->group<So5Hud::GroupCount&&identity->Accepts({chain.data(),count})?identity:attached;
    if(identity&&So5Hud::IsPauseControls(*identity,{chain.data(),count}))group_identity=&pause_controls;
    static const So5Hud::Identity radial_background{.resource=&So5Hud::BattleMenuBackground};
    if(identity&&identity->resource->group==7&&std::find(chain.begin(),chain.begin()+count,So5Hud::NativeName("IMG_BGBLUE",handle))!=chain.begin()+count)group_identity=&radial_background;
    const bool grouped=group_identity!=nullptr;const auto group=grouped?group_identity->resource->group:So5Hud::GroupCount;
    const bool force=grouped&&snapshot&&(snapshot->force_groups&(std::uint64_t{1}<<group))&&layout.groups[group].visible;
    ScopedVisibility visibility(object,group_identity,kind,force);
    if(!Field<unsigned char>(object,0xe5)){original(object,a,b);return;}
    const bool background=grouped&&So5Hud::IsBackgroundGroup(group);
    So5Hud::Visual affine=backdrop?So5Hud::BackgroundTransform(output_width,output_height):
        background?So5Hud::BackgroundTransform(layout,group,output_width,output_height):So5Hud::Visual{0,0,1,1};
    if(grouped) {
        ++rendered[group];if(!layout.groups[group].visible)return;
        auto t=So5Hud::Transform(affine,*group_identity->resource,layout.groups[group],output_width,output_height);
        if(!t){++refused;original(object,a,b);return;}affine=*t;
    }
    if(!grouped&&!backdrop&&(!identity->native||edits.empty())){ObserveUnknown(object,a,b,original,caller);return;}
    // Compose once, then transform emitted vertices for every native UI type.
    // Number, Div3, Win and Rect do not share the image component's field layout.
    // This path also scales glyph spacing, fixed borders and generated parts.
    const auto next=identity?So5Hud::ElementTransform({0,0,1,1},*identity,{chain.data(),count},edits,affine):std::optional<So5Hud::Visual>(affine);
    if(!next)return;
    // A neutral safety base is an exact native draw, not millions of needless
    // vertex copies. Do not inherit this fast path from an outer active scope.
    if(!backdrop&&!draw_scope.enabled&&next->x==0&&next->y==0&&next->sx==1&&next->sy==1){
        if(minimap_composite)ObserveUnknown(object,a,b,original,caller);else original(object,a,b);return;}
    if(!edits.empty())++element_draws;if(kind>=0)++specialized_draws;
    if(backdrop)backdrop_draws.fetch_add(1,std::memory_order_relaxed);
    struct Restore {DrawScope old;~Restore(){draw_scope=old;}} restore{draw_scope};
    draw_scope={true,*next};if(minimap_composite)ObserveUnknown(object,a,b,original,caller);else original(object,a,b);
}
void PS4_SYSV_ABI Render(void* object,void* a,void* b){DrawObject(object,a,b,render_original,-1,reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));}
template<int Kind> void PS4_SYSV_ABI Specialized(void* object,void* a,void* b){DrawObject(object,a,b,specialized_original[Kind],Kind,reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));}

bool Signature(std::span<const unsigned char> image,size_t address,std::initializer_list<unsigned char> code) {
    return address<=image.size()&&code.size()<=image.size()-address&&!std::memcmp(image.data()+address,code.begin(),code.size());
}
}
bool Active(){return active.load(std::memory_order_acquire);}
void Initialize(std::span<unsigned char> image,std::string_view serial,std::string_view version,int output) {
    if(Active())return;
    const char* config=std::getenv("PS4RT_CONFIG");
    const auto path=(config&&*config?std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(config))):std::filesystem::path("ps4rt.ini")).parent_path()/L"hud-layout.json";
    try {
        const auto catalog_path=path.parent_path()/L"hud-private-bindings.json";
        // Private development installation can keep its neutral bridge/live
        // channel and full-width loading backdrop without an active preset.
        // A neutral, signature-guarded bridge must also accept a user preset
        // selected later from the in-game library, without a private catalog.
        if(serial!="CUSA03219"||version!="01.00"||image.size()<0xb83a08)
            throw std::runtime_error("unsupported executable revision");
        if(std::filesystem::is_regular_file(catalog_path))So5Hud::SetNativeCatalog(So5Hud::NativeCatalog::Parse(So5Hud::Read(catalog_path,4*1024*1024)));
        const auto size=Ps4RtUltrawide::Content(output);
        const auto aspect=So5Hud::OutputAspect(size.width,size.height);
        // A preset from another output ratio must not disable the guarded
        // bridge/live channel. Preserve its file and start neutral, so the
        // Preset library can apply a matching preset, including 16:9.
        preset=So5Hud::Base(aspect);
        if(std::filesystem::exists(path)) {
            try {
                auto candidate=So5Hud::Parse(So5Hud::Read(path,1048576));
                if(!So5Hud::RuntimeCompatible(candidate,aspect))
                    throw std::runtime_error("aspect mismatch or an unbound HUD group was customized");
                preset=std::move(candidate);
            }catch(const std::exception& e) {
                std::printf("HUD preset: startup layout ignored (%s); original %.*s HUD and live editing retained; file unchanged\n",
                    e.what(),int(So5Hud::AspectName(aspect).size()),So5Hud::AspectName(aspect).data());
            }
        }
        startup_elements=So5Hud::CompileElements(preset);game_base=reinterpret_cast<std::uintptr_t>(image.data());
        mapping_report=path.parent_path()/L"hud-last-census.json";
        if(!Signature(image,0x20a900,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x48,0x8d,0x05,0xef,0x90,0x97,0x00,0x48,0x89,0xfb,0xc5,0xfa,0x11,0x45,0xe4,0x41,0x89,0xd6,0x49,0x89,0xf7,0x48,0x8b,0x38})||
           !Signature(image,0x203410,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x80,0xbf,0xe5,0x00,0x00,0x00,0x00,0x49,0x89,0xd6,0x49,0x89,0xf7,0x74,0x33,0x48,0x8b,0x5f,0x30,0x48,0x85,0xdb,0x74,0x2a,0x48,0x8b,0x03})||
           !Signature(image,0x209860,{0xc5,0xfa,0x10,0x06,0xc5,0xfa,0x11,0x47,0x2c,0xc5,0xfa,0x10,0x46,0x04,0xc5,0xfa,0x11,0x47,0x30,0xc5,0xfa,0x10,0x46,0x08,0xc5,0xfa,0x11,0x47,0x28,0xc3})||
           !Signature(image,0x2098d0,{0xc5,0xfa,0x10,0x06,0xc5,0xfa,0x11,0x47,0x3c,0xc5,0xfa,0x10,0x46,0x04,0xc5,0xfa,0x11,0x47,0x40,0xc3})||
           !Signature(image,0x2067d9,{0x41,0x8b,0x4f,0x3c})||
           !Signature(image,0x874848,{'%','s','_','%','0','9','d',0})||
           !Signature(image,0x206a10,{0x8b,0x8f,0x3c,0x01,0,0,0xb8,0x10,0x27,0,0,0xff,0xc1,0x0f,0x45,0xc1,0x89,0x87,0x3c,0x01,0,0}))
            throw std::runtime_error("GUI loader/render/layout signatures do not match");
        output_width=size.width;output_height=size.height;
        // Guest threads have not started. Both trampolines exist before the
        // first guest call; failure restores all installed bytes.
        // All ten registered XML object types use these six draw implementations.
        // Each original entry has the same verified six-byte complete prologue.
        constexpr std::array<size_t,5> draws={0x208840,0x1fed70,0x1ff2a0,0x1ffbc0,0x3840b0};
        for(auto address:draws)if(!Signature(image,address,{0x55,0x48,0x89,0xe5,0x41,0x57}))throw std::runtime_error("specialized GUI draw signature mismatch");
        if(!Signature(image,0xf1440,{0x4c,0x8b,0x87,0x38,0x03,0,0,0x4d,0x85,0xc0})||
           !Signature(image,0xf1500,{0x48,0x83,0xbf,0x38,0x03,0,0,0,0x0f,0x84})||
           !Signature(image,0x203ec0,{0x48,0x63,0x87,0x1c,0x20,0,0,0x48,0x85,0xc0})||
           !Signature(image,0x203e30,{0x48,0x63,0x87,0x1c,0x20,0,0,0x48,0x85,0xc0}))
            throw std::runtime_error("native UI vertex-writer signature mismatch");
        if(!Signature(image,0x1f2c30,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x41,0x89,0xf6,0x48,0x89,0xfb})||
           !Signature(image,0x1f2ca5,{0x48,0x8b,0x8b,0xc0,0x06,0,0,0x48,0x85,0xc9,0x74,0x06,0x88,0x81,0xe5,0,0,0})||
           !Signature(image,0x1ed970,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x48,0x89,0xfb}))
            throw std::runtime_error("native minimap visibility observation signature mismatch");
        if(!Signature(image,0x1ef590,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18})||
           !Signature(image,0xf15e0,{0xf,0xb6,0x87,0x33,0x3,0x0,0x0,0x39,0xf0,0x7e,0x23,0x48,0x8b,0x4f,0x70,0x48,0x63,0xc6,0x48,0x85,0xd2,0x48,0x8b,0x3c,0xc1,0x74,0xa,0x48,0x8b,0x47,0x18,0xf,0xb7,0x40,0x18,0x89,0x2,0x48,0x83,0xc7,0x40,0xe9,0x32,0xf8,0x3,0x0,0x31,0xc0})||
           !Signature(image,0x130e40,{0x48,0x8b,0x7f,0x10,0x48,0x85,0xff,0x74,0x7,0x31,0xf6,0xe9,0x50,0xeb,0xf3,0xff,0x31,0xc0,0xc3})||
           !Signature(image,0x6f9a0,{0x31,0xc0,0x80,0x7f,0x4d,0x0,0x74,0x25,0xf,0xb6,0x47,0x4c,0xba,0xab,0xaa,0xaa,0xaa,0x8d,0x48,0x2,0x48,0xf,0xaf,0xd1,0x48,0xc1,0xea,0x21,0x8d,0xc,0x52,0xf7,0xd9,0x8d,0x4c,0x8,0x2,0x85,0xf6,0xf,0x44,0xc8,0xf,0xb6,0xc1,0x48,0x8b,0x44,0xc7,0x20,0xc3})||
           !Signature(image,0x1ef7d0,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x48,0x89,0xfb})||
           !Signature(image,0xf1380,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18})||
           !Signature(image,0xf1640,{0xb8,0x34,0,0,0,0xc3})||
           !Signature(image,0xa03390,{'D','a','m','a','g','e','N','u','m',0}))
            throw std::runtime_error("procedural HUD stream/atlas signatures do not match");
        if(!Signature(image,0x207f10,{0x48,0x85,0xf6,0x74,0xb,0x48,0x8b,0x87,0xf8,0x0,0x0,0x0,0x48,0x89,0x70,0x60,0xc3})||
           !Signature(image,0x1fd940,{0x89,0xf0,0x48,0x8b,0x4c,0xc7,0x68,0x31,0xc0,0x48,0x85,0xc9,0x74,0x4,0x48,0x8b,0x41,0x10,0xc3,0x66,0x66,0x66,0x66,0x2e,0xf,0x1f,0x84,0x0,0x0,0x0})||
           !Signature(image,0x21cdf0,{0xe8,0x4b,0xb,0xfe,0xff,0x48,0x85,0xc0,0x74,0xf,0x48,0x8b,0xbb,0xb0,0x0,0x0,0x0,0x48,0x89,0xc6})||
           !Signature(image,0x874180,{0x43,0x41,0x50,0x54,0x49,0x4f,0x4e,0x46,0x4f,0x4e,0x54,0x32,0x30,0x2e,0x46,0x4e,0x54,0x0}))
            throw std::runtime_error("native caption font assignment signature mismatch");
        if(!Signature(image,0x3277f0,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x58})||
           !Signature(image,0x5724d0,{0xc5,0xf8,0x10,0x06,0xc5,0xf8,0x11,0x87,0x30,0x05,0,0})||
           !Signature(image,0x87f640,{'K','a','g','i','I','c','o','n','.','a','i','f',0}))
            throw std::runtime_error("locked-chest billboard presentation signatures do not match");
        if(!hooks.Install({image.data()+0x20a900,image.data()+0x203410,image.data()+draws[0],image.data()+draws[1],image.data()+draws[2],image.data()+draws[3],image.data()+draws[4],image.data()+0xf1440,image.data()+0xf1500,image.data()+0x203ec0,image.data()+0x1f2c30,image.data()+0x203e30,image.data()+0x1ed970,image.data()+0x206a10,image.data()+0x1ef7d0,image.data()+0xf1380,image.data()+0x3277f0,image.data()+0x1ef590},
            {reinterpret_cast<const void*>(&Load),reinterpret_cast<const void*>(&Render),reinterpret_cast<const void*>(&Specialized<0>),reinterpret_cast<const void*>(&Specialized<1>),reinterpret_cast<const void*>(&Specialized<2>),reinterpret_cast<const void*>(&Specialized<3>),reinterpret_cast<const void*>(&Specialized<4>),reinterpret_cast<const void*>(&Vertex),reinterpret_cast<const void*>(&ExtendedVertex),reinterpret_cast<const void*>(&BatchVertex),reinterpret_cast<const void*>(&MinimapVisibility),reinterpret_cast<const void*>(&ExtendedBatchVertex),reinterpret_cast<const void*>(&MinimapUpdate),reinterpret_cast<const void*>(&Allocate),reinterpret_cast<const void*>(&MinimapDraw),reinterpret_cast<const void*>(&Submit),reinterpret_cast<const void*>(&LockedChestUpdate),reinterpret_cast<const void*>(&MinimapMask)},
            {6,6,6,6,6,6,6,7,8,7,6,7,6,6,6,6,6,6}))
            throw std::runtime_error("cannot install the complete GUI draw bridge safely");
        load_original=reinterpret_cast<LoadFn>(hooks.Original(0));render_original=reinterpret_cast<RenderFn>(hooks.Original(1));
        for(size_t n=0;n<5;++n)specialized_original[n]=reinterpret_cast<RenderFn>(hooks.Original(n+2));
        vertex_original=reinterpret_cast<VertexFn>(hooks.Original(7));extended_vertex_original=reinterpret_cast<VertexExFn>(hooks.Original(8));batch_vertex_original=reinterpret_cast<VertexFn>(hooks.Original(9));
        minimap_visible_original=reinterpret_cast<MinimapVisibleFn>(hooks.Original(10));
        extended_batch_original=reinterpret_cast<VertexExFn>(hooks.Original(11));minimap_update_original=reinterpret_cast<MinimapUpdateFn>(hooks.Original(12));
        allocate_original=reinterpret_cast<AllocateFn>(hooks.Original(13));
        minimap_draw_original=reinterpret_cast<MinimapDrawFn>(hooks.Original(14));submit_original=reinterpret_cast<SubmitFn>(hooks.Original(15));
        locked_chest_original=reinterpret_cast<LockedChestUpdateFn>(hooks.Original(16));
        minimap_mask_original=reinterpret_cast<MinimapDrawFn>(hooks.Original(17));
        model_tint=reinterpret_cast<ModelTintFn>(image.data()+0x5724d0);
        active.store(true,std::memory_order_release);
        live_state.Reset(preset);
        try{live_server=std::make_unique<So5Hud::LiveServer>(path.parent_path(),live_state,preset,[]{
            std::uint64_t count=0;for(const auto& group:rendered)count+=group.load(std::memory_order_relaxed);
            auto text=std::to_string(recognized.load())+" GUI resources matched; "+std::to_string(count)+" mapped draws.";
            const auto snapshot=live_state.Current();if(snapshot&&snapshot->force_groups)text+=" Dev hold: "+std::to_string(forced_draws.load())+" submitted draws; unloaded HUD unavailable.";
            return text;
        });}
        catch(const std::exception& e){std::printf("HUD layout: live channel unavailable (%s); startup layout remains active\n",e.what());}
        std::printf("HUD layout: test layout '%s', %.*s; live same-aspect updates; native GUI and scoped minimap streams enabled\n",preset.name.c_str(),int(So5Hud::AspectName(aspect).size()),So5Hud::AspectName(aspect).data());
    }catch(const std::exception& e){std::printf("HUD layout: layout refused (%s); original HUD retained\n",e.what());}
}
void FrameBoundary(){if(Active()){
    ++native_frame;const auto before=live_state.Current();
    if(live_state.Boundary()){const auto after=live_state.Current();if((before?before->force_groups:0)!=(after?after->force_groups:0))++visibility_epoch;}
}}
void Report() {
    static std::atomic<bool> reported=false;
    if(!Active()||reported.exchange(true))return;
    std::printf("HUD layout: resources recognized=%u, unknown=%u, invalid visual objects=%u\n",recognized.load(),unknown.load(),refused.load());
    std::printf("HUD layout: GUI render calls=%llu, mapped objects=%llu\n",static_cast<unsigned long long>(render_calls.load()),static_cast<unsigned long long>(mapped_objects.load()));
    std::printf("HUD layout: transformed UI vertices=%llu\n",static_cast<unsigned long long>(transformed_vertices.load()));
    std::printf("HUD layout: cached instances resolved=%u, applied layout revision=%llu\n",resolved_instances.load(),static_cast<unsigned long long>(live_state.Applied()));
    std::printf("HUD layout: rejected vertex ranges=%llu, full-width black backdrop draws=%llu\n",static_cast<unsigned long long>(vertex_rejected.load()),static_cast<unsigned long long>(backdrop_draws.load()));
    std::printf("HUD layout minimap: observed states off=%u on=%u, final=%d\n",minimap_states[0].load(),minimap_states[1].load(),minimap_visible.load());
    std::printf("HUD layout minimap: transformed stream vertices=%llu, refused submissions=%llu\n",static_cast<unsigned long long>(minimap_stream_vertices.load()),static_cast<unsigned long long>(minimap_stream_refused.load()));
    std::printf("HUD layout minimap: transformed circular mask vertices=%llu\n",static_cast<unsigned long long>(minimap_mask_vertices.load()));
    std::printf("HUD layout: specialized draws=%llu, object-override draws=%llu\n",static_cast<unsigned long long>(specialized_draws.load()),static_cast<unsigned long long>(element_draws.load()));
    std::printf("HUD layout: development visibility draws=%llu (native submission only)\n",static_cast<unsigned long long>(forced_draws.load()));
    for(size_t n=0;n<rendered.size();++n)if(const auto count=rendered[n].load())
        std::printf("HUD layout: %.*s rendered=%llu\n",int(So5Hud::Specs[n].id.size()),So5Hud::Specs[n].id.data(),static_cast<unsigned long long>(count));
    std::printf("HUD layout: locked chest billboard updates=%llu, hidden=%llu\n",static_cast<unsigned long long>(locked_chest_observed.load()),static_cast<unsigned long long>(locked_chest_hidden.load()));
    unsigned samples=0;
    std::ostringstream saved;saved<<std::setprecision(9)<<"{\"private\":true,\"title_id\":\"CUSA03219\",\"app_ver\":\"01.00\",\"output\":["<<output_width<<','<<output_height<<"],\"minimap_off\":"<<minimap_states[0].load()<<",\"minimap_on\":"<<minimap_states[1].load()<<",\"transformed_vertices\":"<<transformed_vertices.load()<<",\"rejected_vertex_ranges\":"<<vertex_rejected.load()<<",\"draws\":[";
    for(const auto& row:census)if(row.state.load(std::memory_order_acquire)==2){++samples;const auto& b=row.bounds;
        auto relative=[](std::uintptr_t address){return address>=game_base&&address-game_base<0xc00000?address-game_base:std::uintptr_t(0);};
        std::printf("HUD layout census: %s handle=%d name=%016llx type=%llx caller=%llx rect=%.2f,%.2f,%.2f,%.2f vertices=%u\n",row.minimap?"MINIMAP":"unbound",row.handle,
            static_cast<unsigned long long>(row.name),static_cast<unsigned long long>(relative(row.vtable)),static_cast<unsigned long long>(relative(row.caller)),b.left,b.top,b.right-b.left,b.bottom-b.top,b.vertices);
        if(samples>1)saved<<',';
        saved<<"{\"minimap\":"<<(row.minimap?"true":"false")<<",\"handle\":"<<row.handle<<",\"name\":\""<<std::hex<<row.name<<"\",\"vtable\":\""<<relative(row.vtable)<<"\",\"caller\":\""<<relative(row.caller)<<std::dec<<"\",\"rect\":["<<b.left<<','<<b.top<<','<<b.right-b.left<<','<<b.bottom-b.top<<"],\"vertices\":"<<b.vertices<<",\"ancestors\":[";
        for(size_t n=0;n<row.depth;++n){if(n)saved<<',';saved<<"{\"handle\":"<<row.ancestors[n].first<<",\"name\":\""<<std::hex<<row.ancestors[n].second<<std::dec<<"\"}";}saved<<"]}";
    }
    saved<<"],\"cached_instances_resolved\":"<<resolved_instances.load()<<",\"applied_revision\":"<<live_state.Applied()<<",\"group_draws\":[";
    for(size_t n=0;n<rendered.size();++n){if(n)saved<<',';saved<<rendered[n].load();}
    saved<<"],\"applied_groups\":[";const auto final_snapshot=live_state.Current();const auto& final_layout=final_snapshot?final_snapshot->layout:preset;
    for(size_t n=0;n<So5Hud::GroupCount;++n){if(n)saved<<',';const auto& s=final_layout.groups[n];
        saved<<"{\"id\":\""<<So5Hud::Specs[n].id<<"\",\"anchor\":"<<int(s.anchor)<<",\"x\":"<<s.x<<",\"y\":"<<s.y<<",\"scale\":"<<s.scale<<",\"visible\":"<<(s.visible?"true":"false")<<'}';}
    saved<<"],\"minimap_stream_vertices\":"<<minimap_stream_vertices.load()<<",\"minimap_stream_refused\":"<<minimap_stream_refused.load();
    saved<<",\"minimap_mask_vertices\":"<<minimap_mask_vertices.load();
    saved<<",\"locked_chest_updates\":"<<locked_chest_observed.load()<<",\"locked_chest_hidden\":"<<locked_chest_hidden.load();
    saved<<",\"development_visibility_draws\":"<<forced_draws.load()<<",\"force_groups\":"<<(final_snapshot?final_snapshot->force_groups:0)<<"}\n";
    // Private authoring session only. Retain a durable bounded census even if
    // the owner forgets to export the console. No per-frame disk activity.
    if(!mapping_report.empty())try{const auto next=mapping_report.wstring()+L".tmp";
        {std::ofstream file(next.c_str(),std::ios::binary|std::ios::trunc);file<<saved.str();file.close();if(!file)throw std::runtime_error("cannot write mapping census");}
        if(!MoveFileExW(next.c_str(),mapping_report.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("cannot replace mapping census");
        std::puts("HUD layout: private mapping census saved beside the prepared layout.");
    }catch(const std::exception& e){std::printf("HUD layout: census save failed (%s)\n",e.what());}
    std::printf("HUD layout: procedural census samples=%u (unmodified)\n",samples);
}
}
