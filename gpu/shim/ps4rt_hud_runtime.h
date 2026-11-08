// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <span>
#include <string_view>
#include <cstdint>

namespace Ps4RtHud {
// Signature-guarded native HUD bridge. The private catalog is optional.
// With no custom preset all HUD coordinates are original; full-screen loading
// and identified menu backgrounds retain their wide-output coverage correction.
bool Active();
void Initialize(std::span<unsigned char> image,std::string_view serial,std::string_view version,int output);
void Report();
void FrameBoundary();
// These four programs are the game's native GUI renderer, not Scaleform movies
// or the 3D scene. Identity of individual widgets is established on the CPU.
constexpr bool VertexProgram(std::uint64_t hash) {
    return hash==0xc87f248e || hash==0x1932ead9 || hash==0x59cb445a || hash==0xd3ecde9e;
}
}
