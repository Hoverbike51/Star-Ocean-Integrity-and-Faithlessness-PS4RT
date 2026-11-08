// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud_layout.h"
#include <cstdint>
#include <cstdio>
#include <span>
#include <optional>
#include <memory>

namespace So5Hud {
// Size and hashes identify the native layout XML chunk, never its ISF container.
struct ResourceBinding {
    std::string_view resource;
    size_t group;
    std::uint32_t bytes;
    std::uint64_t english,french;
    Rect bounds;
};
extern const std::array<ResourceBinding,25> RuntimeResources;
// Minimap frame/reference bounds, shared with its separate procedural streams.
extern const ResourceBinding MinimapComposite;
extern const ResourceBinding BattleMenuBackground;
extern const ResourceBinding Subtitles;
extern const std::array<ResourceBinding,4> ProceduralResources;
bool RuntimeSupported(size_t group);
Rect GroupBounds(const ResourceBinding&);
const ResourceBinding* IdentifyResource(std::span<const unsigned char>);
struct NativeObject {std::string name,parent,type;Rect bounds;std::uint32_t key=0;size_t parent_index=size_t(-1);std::uint64_t name_hash=0;};
struct NativeResource {
    std::string name;ResourceBinding binding;
    std::vector<NativeObject> objects;
    std::uint32_t key=0;
};
class NativeCatalog {
    struct NameEntry {std::uint64_t hash;const NativeResource* resource;size_t object;};
    std::vector<NameEntry> name_index;
public:
    std::vector<std::unique_ptr<NativeResource>> resources;
    static std::shared_ptr<const NativeCatalog> Parse(std::string_view);
    static std::shared_ptr<const NativeCatalog> Builtin();
    void Index();
    const NativeResource* Find(std::string_view) const;
    const NativeResource* Identify(std::span<const unsigned char>) const;
    // Cached-layout clones have a new handle without a new XML load. Resolve
    // only a UNIQUE complete named hierarchy and verified native object type.
    const NativeResource* Resolve(int handle,std::span<const std::uint64_t> ancestors,std::string_view type,
        bool* ambiguous=nullptr,std::span<const std::uint64_t> complete_tree={}) const;
};
// Install once at startup, before any guest/live threads. No artwork or XML.
void SetNativeCatalog(std::shared_ptr<const NativeCatalog>);
const NativeCatalog& GetNativeCatalog();
struct CompiledElement {std::uint32_t key;Setting value;Rect bounds;};
std::vector<CompiledElement> CompileElements(const Preset&);
// The game's name key is FNV-1 of "%s_%09d" (multiply, then XOR).
std::uint64_t NativeName(std::string_view name,int handle);
struct Identity {
    const ResourceBinding* resource{};
    int handle{};
    std::array<std::uint64_t,7> roots{};
    std::uint64_t excluded{};
    const NativeResource* native{};
    std::vector<std::pair<std::uint64_t,std::uint32_t>> object_keys;
    bool Accepts(std::span<const std::uint64_t> ancestors) const;
};
Identity IdentifyHandle(const ResourceBinding*,int handle);
Identity IdentifyHandle(const NativeResource*,int handle);
struct Visual {float x,y,sx,sy;};
// Keys follow resource -> outer ancestors -> leaf. Lookup is precompiled off
// the render thread; no strings, parsing or allocations in this operation.
std::optional<Visual> ElementTransform(Visual,const Identity&,std::span<const std::uint64_t>,
    std::span<const CompiledElement>,Visual affine={0,0,1,1});
// Produces an affine for emitted UI vertices. Game-owned world transforms,
// component fields, parents, animation, input and gameplay state stay untouched.
std::optional<Visual> Transform(Visual,const ResourceBinding&,const Setting&,double width,double height);
bool RuntimeCompatible(const Preset&,Aspect actual);
}
