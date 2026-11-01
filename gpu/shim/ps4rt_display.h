// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <vector>
#include <cwchar>
#include <algorithm>
#include <string>
#include "ps4rt_output.h"
#include <SDL3/SDL.h>

namespace Ps4RtDisplay {
struct Target {
    HMONITOR monitor{};
    MONITORINFOEXW info{};
    int width=1920,height=1080;
    std::wstring id,name;
    std::uint64_t window_modes=0,fullscreen_modes=0;
};
inline Target Inspect(HMONITOR monitor) {
    Target result; result.monitor=monitor; result.info.cbSize=sizeof(result.info);
    if (!GetMonitorInfoW(monitor,&result.info)) return result;
    result.width=result.info.rcMonitor.right-result.info.rcMonitor.left;
    result.height=result.info.rcMonitor.bottom-result.info.rcMonitor.top;
    // The preferred physical mode avoids exposing supersampling/DSR virtual modes.
    UINT32 paths_count=0,modes_count=0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths_count,&modes_count)!=ERROR_SUCCESS) paths_count=modes_count=0;
    if (paths_count>256 || modes_count>1024) paths_count=modes_count=0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(paths_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modes_count);
    if (paths_count && QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths_count,paths.data(),&modes_count,modes.data(),nullptr)!=ERROR_SUCCESS) paths_count=0;
    for (UINT32 i=0;i<paths_count;++i) {
        const auto& path=paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName,result.info.szDevice)) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME identity{};
        identity.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(identity),path.targetInfo.adapterId,path.targetInfo.id};
        if(DisplayConfigGetDeviceInfo(&identity.header)==ERROR_SUCCESS) {
            result.id=identity.monitorDevicePath; result.name=identity.monitorFriendlyDeviceName;
        }
        DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{};
        preferred.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE,sizeof(preferred),path.targetInfo.adapterId,path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&preferred.header)!=ERROR_SUCCESS || !preferred.width || !preferred.height) continue;
        int w=int(preferred.width),h=int(preferred.height);
        if (path.targetInfo.rotation==DISPLAYCONFIG_ROTATION_ROTATE90 || path.targetInfo.rotation==DISPLAYCONFIG_ROTATION_ROTATE270)
            std::swap(w,h);
        result.width=w; result.height=h; break;
    }
    if(result.id.empty()) result.id=result.info.szDevice;
    if(result.name.empty()) result.name=result.info.szDevice;
    const auto bounds=Ps4RtOutput::Fitting(result.width,result.height);
    DEVMODEW mode{}; mode.dmSize=sizeof(mode);
    for(DWORD index=0;EnumDisplaySettingsExW(result.info.szDevice,index,&mode,0);++index) {
        const int output=Ps4RtOutput::Find(int(mode.dmPelsWidth),int(mode.dmPelsHeight));
        if(output>=0 && mode.dmBitsPerPel>=24 && !(mode.dmDisplayFlags&DM_INTERLACED) && (bounds&(1ull<<output))) result.fullscreen_modes|=1ull<<output;
    }
    // Offer only exact modes exposed by both Windows and the runtime's SDL
    // display backend. Fitting within an EDID size alone does not prove support.
    std::uint64_t sdl_modes=0;
    if(SDL_WasInit(SDL_INIT_VIDEO)) {
        int display_count=0;auto* displays=SDL_GetDisplays(&display_count);
        for(int i=0;displays && i<display_count;++i) {
            const auto native=static_cast<HMONITOR>(SDL_GetPointerProperty(SDL_GetDisplayProperties(displays[i]),SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER,nullptr));
            if(native!=monitor) continue;
            int count=0;auto** modes=SDL_GetFullscreenDisplayModes(displays[i],&count);
            for(int j=0;modes && j<count;++j) {
                const int output=Ps4RtOutput::Find(modes[j]->w,modes[j]->h);
                if(output>=0) sdl_modes|=1ull<<output;
            }
            SDL_free(modes);
            DEVMODEW desktop{};desktop.dmSize=sizeof(desktop);
            const auto* sdl_desktop=SDL_GetDesktopDisplayMode(displays[i]);
            if(EnumDisplaySettingsExW(result.info.szDevice,ENUM_CURRENT_SETTINGS,&desktop,0) && sdl_desktop &&
                int(desktop.dmPelsWidth)==sdl_desktop->w && int(desktop.dmPelsHeight)==sdl_desktop->h) {
                const int output=Ps4RtOutput::Find(sdl_desktop->w,sdl_desktop->h);
                if(output>=0 && (bounds&(1ull<<output))) {sdl_modes|=1ull<<output;result.fullscreen_modes|=1ull<<output;}
            }
            break;
        }
        SDL_free(displays);
    }
    result.fullscreen_modes&=sdl_modes;
    result.window_modes=result.fullscreen_modes;
    return result;
}
inline std::vector<Target> Enumerate() {
    std::vector<Target> result;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL {
        auto target=Inspect(monitor);
        if(target.info.szDevice[0]) reinterpret_cast<std::vector<Target>*>(data)->push_back(std::move(target));
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
inline Target ForWindow(HWND window) {
    return Inspect(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY));
}
}
