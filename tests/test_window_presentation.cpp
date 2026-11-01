// SPDX-License-Identifier: GPL-2.0-or-later
// Real SDL window geometry, without the game or a Vulkan device.
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include "ps4rt_window_size.h"
#include "ps4rt_window_cursor.h"
#include "ps4rt_settings.h"
#include "ps4rt_display.h"

int main() {
    using namespace Ps4RtGraphics;
    Ps4RtWindow::SizeSelection selection;
    assert(selection.Changed(Windowed,2));
    assert(!selection.Changed(Windowed,2));
    assert(!selection.Changed(Borderless,2));
    assert(!selection.Changed(Fullscreen,3));
    assert(selection.Changed(Windowed,3));
    assert(!selection.Changed(Windowed,3));
    assert(selection.Changed(Windowed,1));
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("SKIP: SDL video unavailable: %s\n",SDL_GetError()); return 77; }
    SDL_Window* window=SDL_CreateWindow("SO5 window geometry test",640,360,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
    assert(window && (SDL_GetWindowFlags(window)&SDL_WINDOW_RESIZABLE));
    const auto monitor=static_cast<HMONITOR>(SDL_GetPointerProperty(SDL_GetDisplayProperties(SDL_GetDisplayForWindow(window)),
        SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER,nullptr));
    const auto display=Ps4RtDisplay::Inspect(monitor);
    assert(display.info.szDevice[0] && display.width>0 && display.height>0);
    assert(display.info.rcWork.right>display.info.rcWork.left && display.info.rcWork.bottom>display.info.rcWork.top);
    const int capped=Ps4RtOutput::Limit(3,display.width,display.height);
    assert(Ps4RtOutput::Fits(capped,display.width,display.height));
    const auto displays=Ps4RtDisplay::Enumerate(); assert(!displays.empty());
    for(const auto& screen:displays) {
        assert(!screen.id.empty() && !screen.name.empty() && screen.window_modes);
        assert(!(screen.fullscreen_modes & ~screen.window_modes));
    }
    int display_count=0; auto* sdl_displays=SDL_GetDisplays(&display_count);
    for(int i=0;i<display_count;++i) {
        const auto native=static_cast<HMONITOR>(SDL_GetPointerProperty(SDL_GetDisplayProperties(sdl_displays[i]),
            SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER,nullptr));
        const auto screen=Ps4RtDisplay::Inspect(native);
        for(int output=0;output<Ps4RtOutput::Count;++output) if(screen.fullscreen_modes&(1ull<<output)) {
            SDL_DisplayMode exact{};
            assert(SDL_GetClosestFullscreenDisplayMode(sdl_displays[i],Ps4RtOutput::Widths[output],Ps4RtOutput::Heights[output],0,true,&exact));
            assert(exact.w==Ps4RtOutput::Widths[output] && exact.h==Ps4RtOutput::Heights[output]);
        }
        assert(SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED_DISPLAY(sdl_displays[i]),SDL_WINDOWPOS_CENTERED_DISPLAY(sdl_displays[i])));
        assert(SDL_SyncWindow(window) && SDL_GetDisplayForWindow(window)==sdl_displays[i]);
        SDL_Rect usable{}; assert(SDL_GetDisplayUsableBounds(sdl_displays[i],&usable));
        for(int output=0;output<Ps4RtOutput::Count;++output) {
            const int w=Ps4RtOutput::Widths[output],h=Ps4RtOutput::Heights[output];
            if(w+64>usable.w || h+96>usable.h) continue;
            assert(Ps4RtWindow::ResizeClientPixels(window,w,h));
            assert(SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED_DISPLAY(sdl_displays[i]),SDL_WINDOWPOS_CENTERED_DISPLAY(sdl_displays[i])));
            assert(SDL_SyncWindow(window));
            int actual_w=0,actual_h=0; assert(SDL_GetWindowSizeInPixels(window,&actual_w,&actual_h));
            assert(actual_w==w && actual_h==h);
            std::printf("PASS: target %d, window output %dx%d\n",i+1,w,h);
        }
    }
    SDL_free(sdl_displays);
    std::printf("PASS: %d connected monitor(s), stable identities, exact fullscreen modes and SDL target placement\n",display_count);
    SDL_Rect bounds{}; assert(SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window),&bounds));
    int checked=0;
    for(int output=0;output<Ps4RtSettings::OutputCount;++output) {
        const int w=Ps4RtSettings::OutputWidths[output], h=Ps4RtSettings::OutputHeights[output];
        if(w+64>bounds.w || h+96>bounds.h) continue; // The desktop can constrain oversized windows.
        assert(Ps4RtWindow::ResizeClientPixels(window,w,h));
        int actual_w=0,actual_h=0; assert(SDL_GetWindowSizeInPixels(window,&actual_w,&actual_h));
        assert(actual_w==w && actual_h==h); ++checked;
        std::printf("PASS: drawable client %dx%d pixels\n",actual_w,actual_h);
    }
    assert(checked>0);
    // Emulate a mouse resize to an arbitrary non-16:9 shape. No setting changed.
    assert(SDL_SetWindowSize(window,936,624) && SDL_SyncWindow(window));
    assert(!selection.Changed(Windowed,1));
    int w=0,h=0; assert(SDL_GetWindowSize(window,&w,&h)); assert(w==936 && h==624);
    Ps4RtWindow::MenuCursor cursor;
    assert(SDL_HideCursor()); cursor.Update(window,true); assert(SDL_CursorVisible());
    assert(SDL_SetWindowSize(window,800,600) && SDL_SyncWindow(window));
    assert(SDL_HideCursor()); cursor.Update(window,true); assert(SDL_CursorVisible());
    cursor.Update(window,false); assert(!SDL_CursorVisible());
    assert(SDL_ShowCursor()); cursor.Update(window,true); cursor.Update(window,false); assert(SDL_CursorVisible());
    SDL_DestroyWindow(window); SDL_Quit();
    std::puts("PASS: resolution changes resize once; manual and non-16:9 sizes remain intact");
}
