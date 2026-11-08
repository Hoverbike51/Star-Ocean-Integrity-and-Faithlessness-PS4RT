// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <SDL3/SDL.h>
namespace Ps4RtSpeed {
// Window thread owns input state. Guest/audio/presentation read atomic factor.
void Handle(const SDL_Event& event,bool captured);
void Reset();
}
