// SPDX-License-Identifier: GPL-2.0-or-later
// Settings persistence and safe provider transitions, without game files or Vulkan.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <unistd.h>
#include "ps4rt_graphics.h"
#include "ps4rt_settings.h"
#include "ps4rt_perf_stats.h"
#include "ps4rt_scene_preset.h"

int main() {
    using namespace Ps4RtSettings;
    char path[] = "ps4rt-scene-settings-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    setenv("PS4RT_CONFIG", path, 1);
    for (const char* key : {"PS4RT_UPSCALER", "PS4RT_OUTPUT_RES", "PS4RT_RCAS",
                            "PS4RT_OBJECT_MOTION", "PS4RT_JITTER",
                            "PS4RT_ASYNC_SHADERS"}) unsetenv(key);
    auto& s = Get();
    Load();
    assert(s.native_scene && s.CompactGraphicsMenu());
    assert(s.upscaler == UpscalerOff && s.output_res == OutputDefault);
    assert(OutputWidths[0]==1280 && OutputWidths[1]==1920 && OutputWidths[2]==2560 && OutputWidths[3]==3840);
    assert(Ps4RtOutput::Limit(3,1920,1080)==1);
    assert(Ps4RtOutput::Limit(2,1366,768)==Ps4RtOutput::Find(1366,768));
    assert(Ps4RtOutput::Limit(4,3840,2160)==4);
    for(int output:Ps4RtOutput::Order) {
        assert(Ps4RtOutput::Parse(Ps4RtOutput::Labels[output])==output);
        assert(Ps4RtOutput::Fits(output,1920,1080)==(OutputHeights[output]<=1080 && OutputWidths[output]<=1920));
        assert(Ps4RtOutput::TemporalAllowed(output));
        s.output_res=output;s.scene_preset=-1;SetSceneUpscaler(UpscalerFsr3,false);
        assert(s.upscaler==UpscalerFsr3 && s.jitter);
        const auto surface=Ps4RtOutput::Content(output);
        const auto input=Ps4RtScene::Input(s.scene_preset,surface.width,surface.height);
        assert(input.width<=surface.width && input.height<=surface.height);
        Save();s.scene_preset=-1;s.output_res=OutputDefault;Load();
        assert(s.output_res==output && s.upscaler==UpscalerFsr3);
    }
    s.output_res=OutputDefault; SetSceneUpscaler(UpscalerOff,false); Save(); Load();
    const auto hd=Ps4RtOutput::Fitting(1920,1080),uhd=Ps4RtOutput::Fitting(3840,2160);
    assert(!Ps4RtOutput::Filter(hd,Ps4RtOutput::Ratio21_9) && !Ps4RtOutput::Filter(hd,Ps4RtOutput::Ratio32_9));
    assert(Ps4RtOutput::Filter(hd,Ps4RtOutput::Ratio4_3));
    assert(Ps4RtOutput::Group(Ps4RtOutput::Find(3440,1440))==Ps4RtOutput::Ratio21_9);
    assert(Ps4RtOutput::Group(Ps4RtOutput::Find(5120,1440))==Ps4RtOutput::Ratio32_9);
    assert(Ps4RtOutput::Group(Ps4RtOutput::Find(1280,1024))==Ps4RtOutput::All);
    assert(Ps4RtOutput::Select(3,0)==-1);
    for(int aspect=0;aspect<Ps4RtOutput::AspectCount;++aspect) {
        const auto allowed=Ps4RtOutput::Filter(uhd,aspect);
        const int choice=Ps4RtOutput::Select(3,uhd,aspect);
        if(allowed) assert(choice>=0 && (allowed&(1ull<<choice)));
        for(int index=0;index<OutputCount;++index) if(allowed&(1ull<<index)) {
            assert(Ps4RtOutput::Fits(index,3840,2160));
            assert(aspect==0 || Ps4RtOutput::Group(index)==aspect);
        }
    }
    const auto ultra=Ps4RtOutput::Content(Ps4RtOutput::Find(3440,1440));
    assert(ultra.width==2560 && ultra.height==1440);
    const auto wide=Ps4RtOutput::Content(Ps4RtOutput::Find(5120,1440));
    assert(wide.width==2560 && wide.height==1440);
    const auto classic=Ps4RtOutput::Content(Ps4RtOutput::Find(1600,1200));
    assert(classic.width==1600 && classic.height==900);
    s.output_aspect=Ps4RtOutput::Ratio16_10; Save(); s.output_aspect=0; Load();
    assert(s.output_aspect==Ps4RtOutput::Ratio16_10);
    assert(!s.object_motion && !s.jitter && !s.rcas && s.temporal_problem);
    assert(s.frames_queued == 2);
    assert(!s.window_stretch);
    s.window_stretch = true;
    Save(); s.window_stretch = false; Load();
    assert(s.window_stretch);
    assert(Ps4RtGraphics::StretchWindow(Ps4RtGraphics::Windowed, true));
    assert(!Ps4RtGraphics::StretchWindow(Ps4RtGraphics::Windowed, false));
    assert(!Ps4RtGraphics::StretchWindow(Ps4RtGraphics::Borderless, true));
    assert(!Ps4RtGraphics::StretchWindow(Ps4RtGraphics::Fullscreen, true));
    assert(s.engine_fps == 60 && s.applied_engine_fps == 60);
    assert(Ps4RtGraphics::Resolutions.size() == 5);
    assert(Ps4RtGraphics::ParseResolution("4K") == 4);
    assert(Ps4RtGraphics::ParseResolution("1800p") == 0);
    // Fresh configs ship with the visually stable synchronous path.
    assert(!s.async_shaders);
    // The overlay changes this atomic live. Make sure closing the panel and
    // relaunching respects either saved choice, without launcher overrides.
    s.async_shaders = true;
    Save();
    s.async_shaders = false;
    Load();
    assert(s.async_shaders);
    s.async_shaders = false;
    Save();
    s.async_shaders = true;
    Load();
    assert(!s.async_shaders);
    s.show_fps = true;
    s.overlay_items = 0xdb;
    s.overlay_scale = 150;
    s.overlay_corner = 3;
    s.overlay_opacity = 75;
    s.overlay_layout = 1;
    Save();
    s.show_fps = false;
    s.overlay_items = 0;
    s.overlay_scale = 50;
    s.overlay_corner = 0;
    s.overlay_opacity = 0;
    s.overlay_layout = 0;
    Load();
    assert(s.show_fps && s.overlay_items == 0xdb);
    assert(s.overlay_scale == 150 && s.overlay_corner == 3);
    assert(s.overlay_opacity == 75 && s.overlay_layout == 1);
    Ps4RtPerfStats::Sample overlay_sample;
    overlay_sample.version = "0.4.0";
    assert(Ps4RtPerfStats::Format(overlay_sample, Ps4RtPerfStats::Version, true) == "Windows 0.4.0");
    assert(Ps4RtPerfStats::Format(overlay_sample, Ps4RtPerfStats::Fps | Ps4RtPerfStats::Version, false) ==
           "0 FPS\nWindows 0.4.0");
    overlay_sample.cpu_percent=15;overlay_sample.fps=60;overlay_sample.frame_ms=16.7f;
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::Fps|Ps4RtPerfStats::Cpu,true)=="60 FPS   CPU 15%");
    s.menu_tab=4; Save();s.menu_tab=0;Load();assert(s.menu_tab==4);
    for (int provider : {UpscalerOff, UpscalerFsr3, UpscalerFsr4}) {
        for (int output : {1, 2, 3}) {
            s.sharpen = true;
            s.sharpness = 0.42f;
            s.anisotropy = 16;
            s.output_res = output;
            SetSceneUpscaler(provider, false);
            Save();
            Load();
            assert(s.upscaler == provider && s.startup_upscaler == provider);
            assert(s.output_res == output && s.startup_output_res == output);
            assert(s.sharpness > 0.419f && s.sharpness < 0.421f);
            assert(s.anisotropy == 16 && s.startup_anisotropy == 16);
            assert(!s.object_motion && !s.fsr1 && !s.spatial_aa);
            assert(s.jitter == (provider != UpscalerOff));
            assert(s.rcas == (provider == UpscalerOff));
        }
    }
    setenv("PS4RT_UPSCALER", "off", 1);
    setenv("PS4RT_OUTPUT_RES", "1920x1080", 1);
    Load();
    assert(s.upscaler == UpscalerOff && s.output_res == 1 && !s.jitter);
    unsetenv("PS4RT_UPSCALER");
    unsetenv("PS4RT_OUTPUT_RES");
    Load();
    assert(!s.object_motion && s.temporal_problem);
    s.upscaler=UpscalerFsr4;
    ConfigureUpscalerSupport(false,false);
    assert(s.upscaler==UpscalerFsr3 && !s.fsr4_supported && s.fsr4_problem);
    s.upscaler=UpscalerFsr4;
    ConfigureUpscalerSupport(true,false);
    assert(s.upscaler==UpscalerFsr4 && s.fsr4_supported);
    s.upscaler=UpscalerFsr411;
    ConfigureUpscalerSupport(true,false);
    assert(s.upscaler==UpscalerFsr3 && !s.fsr411_supported);
    unlink(path);
    std::puts("PASS: native-scene settings persistence and native-port defaults");
}
