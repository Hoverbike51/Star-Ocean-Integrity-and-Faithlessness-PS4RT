// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cmath>
#include "ps4rt_graphics.h"

namespace Ps4RtWindow {
// Track choices, not resize events: dragging a window must not reset its size.
struct SizeSelection {
    int mode = -1, output = -1;
    bool Changed(int next_mode, int next_output) {
        const bool resize = next_mode == Ps4RtGraphics::Windowed &&
                            (mode != next_mode || output != next_output);
        mode = next_mode; output = next_output;
        return resize;
    }
};

inline bool ResizeClientPixels(SDL_Window* window, int width, int height) {
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) {
        if (!SDL_RestoreWindow(window) || !SDL_SyncWindow(window)) return false;
    }
    const float density = SDL_GetWindowPixelDensity(window);
    if (!(density > 0.0f) || !std::isfinite(density)) return false;
    // SDL window units and drawable pixels may differ on a high-density display.
    const int w = std::max(1, int(std::lround(width / density)));
    const int h = std::max(1, int(std::lround(height / density)));
    return SDL_SetWindowSize(window, w, h) && SDL_SyncWindow(window);
}
} // namespace Ps4RtWindow
