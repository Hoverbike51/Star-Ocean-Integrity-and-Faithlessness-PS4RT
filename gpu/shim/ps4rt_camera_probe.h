// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <span>
#include <string_view>

namespace Ps4RtCameraProbe {
// Opt-in, bounded native-follow observations for a performance-capture session.
// Does not change the camera, actor, lens, controls or party composition.
void Initialize(std::span<unsigned char> image, std::string_view serial,
                std::string_view version);
void Report();
}
