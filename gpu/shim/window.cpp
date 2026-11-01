// SDL3 window for the native Windows Vulkan swapchain.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "sdl_window.h"
#include "ps4rt_overlay.h"
#include "ps4rt_graphics.h"
#include "ps4rt_settings.h"
#include "ps4rt_display.h"

extern "C" void runtime_pad_refresh_keyboard(void);

namespace Frontend {

namespace {
SDL_DisplayID LaunchDisplay() {
    SDL_DisplayID selected=SDL_GetPrimaryDisplay();
    const char* wanted=std::getenv("PS4RT_DISPLAY_DEVICE");
    int count=0; SDL_DisplayID* displays=SDL_GetDisplays(&count);
    for(int i=0;displays && i<count && wanted;++i) {
        auto monitor=static_cast<HMONITOR>(SDL_GetPointerProperty(SDL_GetDisplayProperties(displays[i]),
            SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER,nullptr));
        const auto target=Ps4RtDisplay::Inspect(monitor);
        std::string device; for(wchar_t c:target.info.szDevice) { if(!c) break; device+=char(c); }
        if(device==wanted) { selected=displays[i]; break; }
    }
    SDL_free(displays); return selected;
}
void ApplyDisplayLimits(SDL_DisplayID display) {
    const auto monitor=static_cast<HMONITOR>(SDL_GetPointerProperty(SDL_GetDisplayProperties(display),
        SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER,nullptr));
    const auto target=Ps4RtDisplay::Inspect(monitor);
    auto& settings=Ps4RtSettings::Get();
    settings.display_max_width=target.width; settings.display_max_height=target.height;
    settings.display_window_modes=target.window_modes;
    settings.display_fullscreen_modes=target.fullscreen_modes;
    auto modes=settings.display_mode==Ps4RtGraphics::Fullscreen?target.fullscreen_modes:target.window_modes;
    if(!modes) {
        settings.display_mode=Ps4RtGraphics::Borderless; modes=target.window_modes;
        std::printf("Display: no supported exclusive output; using borderless\n");
    }
    if(!Ps4RtOutput::Filter(modes,settings.output_aspect)) settings.output_aspect=Ps4RtOutput::All;
    const int requested=settings.output_res;
    const int limited=Ps4RtOutput::Select(requested,modes,settings.output_aspect);
    settings.output_res=limited>=0?limited:Ps4RtSettings::OutputDefault;
    if(requested!=settings.output_res)
        std::printf("Display: output limited to %s for monitor %dx%d\n",
            Ps4RtOutput::Arguments[settings.output_res],target.width,target.height);
    if(!Ps4RtOutput::TemporalAllowed(settings.output_res) && settings.upscaler!=Ps4RtSettings::UpscalerOff)
        Ps4RtSettings::SetSceneUpscaler(Ps4RtSettings::UpscalerOff,false);
}
}

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    // Gamepads are sampled by runtime_pad.c; their events are pumped here with the window's.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        UNREACHABLE_MSG("Failed to initialize SDL video: {}", SDL_GetError());
    }
    runtime_pad_refresh_keyboard();
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    const auto display=LaunchDisplay(); ApplyDisplayLimits(display);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    const int output = std::clamp(Ps4RtSettings::Get().output_res.load(),0,Ps4RtSettings::OutputCount-1);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, Ps4RtSettings::OutputWidths[output]);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, Ps4RtSettings::OutputHeights[output]);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    base_title = title;
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    ASSERT_MSG(window, "Failed to create window: {}", SDL_GetError());
    // Optional BMP window icon supplied by the launch environment.
    if (const char* icon_path = std::getenv("PS4RT_WINDOW_ICON")) {
        if (SDL_Surface* icon = SDL_LoadBMP(icon_path)) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }

    const char* driver = SDL_GetCurrentVideoDriver();
    const SDL_PropertiesID wp = SDL_GetWindowProperties(window);
    if (driver && !std::strcmp(driver, "windows")) {
        window_info.type = WindowSystemType::Windows;
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    } else {
        UNREACHABLE_MSG("Unsupported SDL video driver {}", driver ? driver : "(none)");
    }
    // ps4rt: PS4RT_FULLSCREEN=1 (older launchers) still asks for fullscreen.
    if (const char* fullscreen = std::getenv("PS4RT_FULLSCREEN"); fullscreen && fullscreen[0] == '1' &&
        Ps4RtSettings::Get().display_mode == Ps4RtGraphics::Windowed) {
        Ps4RtSettings::Get().display_mode = Ps4RtGraphics::Borderless;
    }
    ApplyDisplayMode(Ps4RtSettings::Get().display_mode);
    ApplyWindowSize();
    // ps4rt: the game has no mouse; the in-game menu draws its own cursor (Ps4RtOverlay::SetOpen).
    SDL_HideCursor();
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    LOG_INFO(Frontend, "Window {}x{} on {}", w, h, driver);
}

// Windowed, borderless desktop or an exact supported exclusive mode. SDL window thread only.
void WindowSDL::ApplyDisplayMode(int mode) {
    ApplyDisplayLimits(SDL_GetDisplayForWindow(window));
    mode=Ps4RtSettings::Get().display_mode;
    display_mode = mode;
    if (mode == Ps4RtGraphics::Windowed) {
        SDL_SetWindowFullscreen(window, false);
    } else {
        const auto display=SDL_GetDisplayForWindow(window);
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
        SDL_DisplayMode selected{};
        const int output=Ps4RtSettings::Get().output_res;
        bool exclusive=mode==Ps4RtGraphics::Fullscreen;
        if(exclusive && (!SDL_GetClosestFullscreenDisplayMode(display,Ps4RtOutput::Widths[output],
                Ps4RtOutput::Heights[output],desktop?desktop->refresh_rate:0,true,&selected) ||
                selected.w!=Ps4RtOutput::Widths[output] || selected.h!=Ps4RtOutput::Heights[output])) {
            exclusive=false; display_mode=Ps4RtSettings::Get().display_mode=Ps4RtGraphics::Borderless;
            std::printf("Display: exact SDL fullscreen mode unavailable; using borderless\n");
        }
        if (!SDL_SetWindowFullscreenMode(window, exclusive ? &selected : nullptr)) {
            LOG_WARNING(Frontend, "Fullscreen mode: {}", SDL_GetError());
            display_mode=Ps4RtSettings::Get().display_mode=Ps4RtGraphics::Borderless;
            SDL_SetWindowFullscreenMode(window,nullptr);
        }
        if(!SDL_SetWindowFullscreen(window, true)) {
            LOG_WARNING(Frontend,"Fullscreen request refused: {}",SDL_GetError());
            display_mode=Ps4RtSettings::Get().display_mode=Ps4RtGraphics::Windowed;
            SDL_SetWindowFullscreen(window,false);
        }
    }
    fullscreen_output=Ps4RtSettings::Get().output_res;
    SDL_SyncWindow(window);
    std::printf("Window: %s\n", Ps4RtGraphics::DisplayModeNames[display_mode]);
}

WindowSDL::~WindowSDL() {
    SDL_DestroyWindow(window);
}

void WindowSDL::ApplyWindowSize() {
    const int output = std::clamp(Ps4RtSettings::Get().output_res.load(),0,Ps4RtSettings::OutputCount-1);
    if (!size_selection.Changed(display_mode,output)) return;
    const int wanted_w = Ps4RtSettings::OutputWidths[output];
    const int wanted_h = Ps4RtSettings::OutputHeights[output];
    if (!Ps4RtWindow::ResizeClientPixels(window,wanted_w,wanted_h)) {
        LOG_WARNING(Frontend,"Window size request failed: {}",SDL_GetError());
    }
    int w=0,h=0; SDL_GetWindowSizeInPixels(window,&w,&h);
    width=w; height=h;
    std::printf("Window: requested client %dx%d pixels, actual %dx%d; mouse resizing remains enabled\n",
                wanted_w,wanted_h,w,h);
}

void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 0;
    text_requested = true;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    const std::string title = text_active ? base_title + " \u2014 " + text_prompt + ": " + text + "_  (Enter = OK, Esc = cancel)"
                                          : base_title;
    SDL_SetWindowTitle(window, title.c_str());
    Ps4RtOverlay::SetTextEntry(text_active, text_prompt, text);
}

bool WindowSDL::PollEvents() {
    {
        std::scoped_lock lock{text_mutex};
        if (text_requested) { // SDL text input must be toggled from the window thread
            text_requested = false;
            text_active = true;
            SDL_StartTextInput(window);
            UpdateTextTitle();
        }
    }
    if (!text_active) {
        Ps4RtOverlay::UpdateTextInput(window);
    }
    if (const int mode = Ps4RtSettings::Get().display_mode; mode != display_mode ||
            (mode==Ps4RtGraphics::Fullscreen && Ps4RtSettings::Get().output_res!=fullscreen_output)) {
        ApplyDisplayMode(mode);
    }
    ApplyWindowSize();
    Ps4RtOverlay::UpdateCursor(window);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (text_active && (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN)) {
            std::scoped_lock lock{text_mutex};
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                text += event.text.text;
            } else if (event.key.key == SDLK_BACKSPACE && !text.empty()) {
                size_t cut = text.size() - 1; // drop one UTF-8 code point
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                text.erase(cut);
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                text_state = event.key.key == SDLK_ESCAPE ? 2 : 1;
                text_active = false;
                SDL_StopTextInput(window);
            }
            UpdateTextTitle();
            continue;
        }
        // The controller finishes the text dialog too: Cross (A) accepts, Circle (B) cancels.
        if (text_active && event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
             event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)) {
            std::scoped_lock lock{text_mutex};
            text_state = event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ? 1 : 2;
            text_active = false;
            SDL_StopTextInput(window);
            UpdateTextTitle();
            continue;
        }
        if (Ps4RtOverlay::HandleEvent(event)) {
            continue;
        }
        switch (event.type) {
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
            ApplyDisplayLimits(SDL_GetDisplayForWindow(window));
            break;
        case SDL_EVENT_KEYMAP_CHANGED:
            runtime_pad_refresh_keyboard();
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            width = w;
            height = h;
            break;
        }
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            is_open = false;
            break;
        default:
            break;
        }
    }
    Ps4RtOverlay::UpdateCursor(window);
    return is_open;
}

} // namespace Frontend
