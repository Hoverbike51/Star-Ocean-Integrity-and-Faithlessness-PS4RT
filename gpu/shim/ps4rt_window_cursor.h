// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <SDL3/SDL.h>

namespace Ps4RtWindow {
// Owned by the SDL window thread. Reassert after fullscreen/resize transitions,
// which can otherwise reset cursor visibility or keep a stale relative mode.
class MenuCursor {
    bool captured=false,previous_visible=false,previous_grab=false,previous_relative=false;
public:
    void Update(SDL_Window* window,bool menu) {
        if(menu) {
            if(!captured) {
                previous_visible=SDL_CursorVisible();previous_grab=SDL_GetWindowMouseGrab(window);
                previous_relative=SDL_GetWindowRelativeMouseMode(window);captured=true;
            }
            if(SDL_GetWindowRelativeMouseMode(window)) SDL_SetWindowRelativeMouseMode(window,false);
            if(SDL_GetWindowMouseGrab(window)) SDL_SetWindowMouseGrab(window,false);
            if(!SDL_CursorVisible()) SDL_ShowCursor();
        } else if(captured) {
            SDL_SetWindowMouseGrab(window,previous_grab);
            SDL_SetWindowRelativeMouseMode(window,previous_relative);
            if(previous_visible) SDL_ShowCursor();else SDL_HideCursor();
            captured=false;
        }
    }
};
}
