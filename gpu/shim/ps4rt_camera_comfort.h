// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <span>
#include <string_view>

namespace Ps4RtCameraComfort {
// Tune only the verified native positional follow filter. Initialization runs
// before guest execution; selecting a mode changes data, never running code.
enum Mode { Original, Light, Damped, Count };
constexpr float Coefficient(int mode) {
    return mode==Light ? 0.16f : mode==Damped ? 0.12f : 0.2f;
}
bool Initialize(std::span<unsigned char> image, std::string_view serial,
                std::string_view version, int mode);
bool Available();
bool Select(int mode);
}
