// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace So5Hud {
inline constexpr std::string_view MapId="so5-cusa03219-01.00-hud-research";
inline constexpr size_t GroupCount=31;
inline constexpr size_t PartySlots=7;
inline constexpr double PartySlotStep=252;
enum class Aspect { Standard, Wide, Superwide };
enum class Anchor { Original, TopLeft, TopRight, BottomLeft, BottomRight, Left, Right, Center, World };
struct Rect { double x=0,y=0,w=0,h=0; };
struct Spec { std::string_view id,label; Anchor preferred; bool centered=false,world=false,locked=false,hide=true; };
extern const std::array<Spec,GroupCount> Specs;
struct Setting {
    Anchor anchor=Anchor::Original;
    double x=0,y=0,scale=1;
    bool visible=true;
    bool operator==(const Setting&) const=default;
};
struct ElementSetting {
    std::string resource,object; // Empty object selects the complete GUI resource.
    Setting value;
    bool operator==(const ElementSetting&) const=default;
};
struct Preset {
    std::string name="Custom layout";
    Aspect aspect=Aspect::Standard;
    std::array<Setting,GroupCount> groups{};
    std::vector<ElementSetting> elements;
    // Optional authoring reference; runtime anchors still use the actual output.
    int reference_width=0,reference_height=0;
    bool operator==(const Preset&) const=default;
};
struct Object { std::string id,resource,parent,type,texture,part; Rect rect; };
struct Catalog {
    std::vector<Object> objects;
    std::array<std::vector<std::string>,GroupCount> roots,resources;
    std::array<std::optional<Rect>,GroupCount> reference_bounds;
    bool Descends(const Object&,std::string_view root) const;
    std::vector<const Object*> Members(size_t group) const;
    Rect Bounds(size_t group) const;
    bool Identified(size_t group) const {return !resources.at(group).empty();}
};
std::string_view AspectName(Aspect);
std::string_view AnchorName(Anchor);
Preset Base(Aspect);
void Validate(const Preset&);
void CheckJson(std::string_view,size_t limit);
Preset Parse(std::string_view);
std::string Serialize(const Preset&);
// Human-readable file export; live messages retain the compact representation.
std::string SerializeReadable(const Preset&);
Catalog ParseCatalog(std::string_view);
std::string Read(const std::filesystem::path&,size_t limit);
void Save(const std::filesystem::path&,const Preset&);
// Shared placement calculation. Canvas uses the actual output dimensions, not
// rounded aspect-family names. This function does not write guest memory.
Rect Place(Rect authored,const Setting&,double width,double height);
Rect Canvas(Aspect);
Rect Canvas(const Preset&);
Rect PartySlot(Rect first,size_t slot);
}
