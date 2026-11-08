// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace Ps4RtLeader {
enum class Result { None, Pending, Applied, Refused };
struct View {
    unsigned available_mask{};
    int current=-1;
    bool ready=false;
    Result result=Result::None;
};
bool Available();
// The render/UI thread only exchanges bounded messages. All native actor work
// runs after the original control-manager update, on the game's own thread.
void Inspect();
View GetView();
bool Request(int character);
// Mapped PS4 directions, sampled after remapping. No SDL or guest calls here.
void Input(std::uint32_t buttons,bool captured);
// O/P key-down edges enqueue the same validated native exploration action.
// No synthetic PS4 D-pad event is injected into combat or game menus.
void Cycle(int direction,bool captured);
// Window thread: persist a changed preference outside the game update thread.
void FlushPreference();
void Report();
void Initialize(std::span<unsigned char> image,std::string_view serial,std::string_view version);
}
