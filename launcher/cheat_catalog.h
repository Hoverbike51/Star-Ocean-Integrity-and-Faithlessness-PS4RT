// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <string_view>
namespace So5Cheats {
// Shared presentation metadata. Runtime metadata IDs come from verified native definitions,
// never from a profile's display name. Unknown imported text retains its author's wording.
struct Definition { int id; const char* name; const wchar_t* title; const wchar_t* detail; const wchar_t* warning=nullptr; };
// ID 3 is retired: its broad inventory hook also duplicated story/key items.
// Keep all remaining IDs stable rather than renumbering saved/runtime metadata.
inline constexpr std::array<Definition,8> Definitions{{
 {1,"Infinite HP",L"Infinite HP",L"Restores party members' HP to maximum when HP changes."},
 {2,"Infinite MP",L"Infinite MP",L"Restores party members' MP to maximum when MP changes."},
 {4,"Quick Level Up",L"Fast level-up",L"Changes the experience calculation when experience is updated."},
 {5,"Max Stats",L"Maximum stats",L"Applies the code's values to the targeted stats at the next level-up."},
 {6,"Max Money",L"Maximum money",L"Applies the code's maximum money value after a purchase."},
 {7,"Max SP",L"Maximum SP",L"Applies the code's maximum skill-point value after gaining SP."},
 {8,"One-Hit Kills",L"One-hit kills",L"Defeats enemies in one hit. Can block scripted battles, including Der-Suul.",L"Disable for story battles."},
 {9,"Movement Speed",L"Movement speed",L"Speeds up party movement: ×1.5 or ×2. Enemies and NPCs keep their normal speed."}
}};
inline const Definition* Catalog(int id) {for(const auto& definition:Definitions)if(definition.id==id)return &definition;return nullptr;}
}
