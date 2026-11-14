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
#include "ps4rt_ultrawide.h"

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
    assert(s.native_render_height==1080);
    for(int height:Ps4RtScene::NativeHeights) {
        s.native_render_height=height;SetSceneUpscaler(UpscalerOff,false);Save();
        s.native_render_height=1080;Load();assert(s.native_render_height==height);
        assert(!ResolutionNeedsRestart());
        const auto native=Ps4RtScene::RequestedInput(false,4,height,3840,2160);
        assert(native.height==uint32_t(height) && native.width==uint32_t(height*16/9));
        for(int provider:{UpscalerFsr3,UpscalerFsr4,UpscalerDlss}) {
            s.scene_preset=1;SetSceneUpscaler(provider,false);
            assert(s.native_render_height==height);
            const auto input=Ps4RtScene::RequestedInput(true,1,height,3840,2160);
            assert(input==Ps4RtScene::Input(1,3840,2160));
        }
        SetSceneUpscaler(UpscalerOff,false);assert(s.native_render_height==height);
    }
    assert(Ps4RtScene::NativeHeight(1800)==1080);
    assert(Ps4RtScene::NativeInput(2160,5120,1440,true)==(Ps4RtScene::Size{7680,2160}));
    assert(Ps4RtScene::NativeInput(720,3440,1440,true)==(Ps4RtScene::Size{1720,720}));
    s.native_render_height=1800;Save();Load();assert(s.native_render_height==1080);
    assert(s.camera_follow==0);
    assert(s.exploration_leader==-1);
    assert(s.exploration_leader_enabled);
    for(bool enabled:{false,true}) {
        s.exploration_leader_enabled=enabled;Save();s.exploration_leader_enabled=!enabled;
        Load();assert(s.exploration_leader_enabled==enabled);
    }
    for(int tab=0;tab<=6;++tab){s.menu_tab=tab;Save();s.menu_tab=-1;Load();assert(s.menu_tab==tab);}
    for(int id=-1;id<6;++id) {
        s.exploration_leader=id;Save();s.exploration_leader=99;Load();assert(s.exploration_leader==id);
    }
    for(int invalid:{-2,6,999}) {
        FILE* config=std::fopen(path,"w");assert(config);
        std::fprintf(config,"exploration_leader=%d\n",invalid);std::fclose(config);
        Load();assert(s.exploration_leader==-1);
    }
    for(int mode=0;mode<3;++mode) {
        s.camera_follow=mode; Save(); s.camera_follow=-1; Load(); assert(s.camera_follow==mode);
    }
    for(int invalid:{-1,3,999}) {
        FILE* config=std::fopen(path,"w"); assert(config);
        std::fprintf(config,"camera_follow=%d\n",invalid);std::fclose(config);
        Load();assert(s.camera_follow==0);
    }
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
        const bool adjusted=Ps4RtUltrawide::Adjusted(output);
        const auto surface=adjusted?Ps4RtOutput::Size{OutputWidths[output],OutputHeights[output]}:Ps4RtOutput::Content(output);
        const auto input=Ps4RtScene::Input(s.scene_preset,surface.width,surface.height,adjusted);
        assert(input.width<=surface.width && input.height<=surface.height);
        Save();s.scene_preset=-1;s.output_res=OutputDefault;Load();
        assert(s.output_res==output && s.upscaler==UpscalerFsr3);
    }
    s.output_res=OutputDefault; SetSceneUpscaler(UpscalerOff,false); Save(); Load();
    const auto hd=Ps4RtOutput::Fitting(1920,1080),uhd=Ps4RtOutput::Fitting(3840,2160);
    assert(Ps4RtOutput::Filter(hd,Ps4RtOutput::Ratio21_9) && Ps4RtOutput::Filter(hd,Ps4RtOutput::Ratio32_9));
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
    s.overlay_items = 0x1db;
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
    assert(s.show_fps && s.overlay_items == 0x1db);
    assert(s.overlay_scale == 150 && s.overlay_corner == 3);
    assert(s.overlay_opacity == 75 && s.overlay_layout == 1);
    // Existing corner IDs and both new centered positions survive save/load unchanged.
    for (int position = 0; position < 6; ++position) {
        s.overlay_corner = position;
        s.overlay_gpu_model = position % 2 != 0;
        s.overlay_cpu_model = position % 2 == 0;
        Save();
        s.overlay_corner = -1;
        s.overlay_gpu_model = s.overlay_cpu_model = false;
        Load();
        assert(s.overlay_corner == position);
        assert(s.overlay_gpu_model == (position % 2 != 0));
        assert(s.overlay_cpu_model == (position % 2 == 0));
    }
    for (int layout = 0; layout < 3; ++layout) {
        s.overlay_layout=layout; Save(); s.overlay_layout=-1; Load();
        assert(s.overlay_layout==layout);
    }
    assert(Ps4RtPerfStats::VisibleItems(0,Ps4RtPerfStats::Technical)==Ps4RtPerfStats::AllItems);
    assert(Ps4RtPerfStats::VisibleItems(7,Ps4RtPerfStats::OneLine)==7);
    assert(Ps4RtPerfStats::VisibleItems(7,Ps4RtPerfStats::Stacked)==7);
    Ps4RtPerfStats::FrameHistory history;
    history.Add(-1); history.Add(0); history.Add(NAN); history.Add(INFINITY);
    assert(history.count==0 && history.Mean()==0);
    for (int i=0; i<240; ++i) history.Add(10);
    history.Add(250); // A long frame remains visible rather than being filtered out.
    assert(history.count==240 && history.Offset()==1 && history.Peak()==250);
    assert(history.Mean()==11);
    for (int i=0; i<240; ++i) history.Add(20);
    assert(history.Mean()==20 && history.Peak()==20);
    const Ps4RtPerfStats::Anchor anchors[]={{0,0},{1,0},{0,1},{1,1},{.5f,0},{.5f,1}};
    for (int position = 0; position < 6; ++position) {
        const auto anchor = Ps4RtPerfStats::PositionAnchor(position);
        assert(anchor.x == anchors[position].x && anchor.y == anchors[position].y);
    }
    assert(Ps4RtPerfStats::MatchesGpuEngine(L"pid_123_luid_0x00000001_0x0000ABCD_phys_0_eng_0_engtype_3D",123,1,0xabcd));
    assert(!Ps4RtPerfStats::MatchesGpuEngine(L"pid_1234_luid_0x1_0xabcd_phys_0_eng_0_engtype_3D",123,1,0xabcd));
    assert(!Ps4RtPerfStats::MatchesGpuEngine(L"pid_123_luid_0x2_0xabcd_phys_0_eng_0_engtype_3D",123,1,0xabcd));
    assert(!Ps4RtPerfStats::MatchesGpuEngine(L"pid_123_luid_0x1_0xabcd_phys_0_eng_0_engtype_Copy",123,1,0xabcd));
    using namespace Ps4RtPerfStats;
    assert(MatchesGpuEngine(L"pid_123_luid_0x1_0xabcd_phys_0_eng_1_engtype_Compute_0",123,1,0xabcd));
    assert(MatchesGpuEngine(L"pid_123_luid_0x1_0xabcd_phys_0_eng_1_engtype_Compute",123,1,0xabcd));
    assert(!MatchesGpuEngine(L"pid_123_luid_0x1_0xabcd_phys_0_eng_0_engtype_3Dother",123,1,0xabcd));
    assert(!MatchesGpuEngine(L"pid_123_luid_0x1_0xabcd_phys_0_eng_0_engtype_Compute_bad",123,1,0xabcd));
    assert(!MatchesGpuEngine(L"pid_123_luid_0x1_0xabcd_phys_0_eng_0_engtype_VideoDecode",123,1,0xabcd));
    assert(CpuPercent(1000, 1000, 32) == 3.125f);
    assert(CpuPercent(1000, 1000, 20) == 5.0f);
    assert(CpuPercent(32000, 1000, 32) == 100.0f);
    assert(CpuPercent(0, 1000, 32) == 0.0f);
    assert(CpuPercent(0, 0, 32) < 0 && CpuPercent(1, 1, 0) < 0);
    CpuCounter counter;
    assert(counter.Read(5000, 1000, 32) < 0); // First observation is not lifetime work.
    assert(counter.Read(6000, 2000, 32) == 3.125f);
    assert(counter.Read(10, 3000, 32) < 0); // Reset must not underflow.
    assert(counter.Read(10, 3000, 32) < 0); // Empty interval.
    counter.Reset();
    assert(counter.Read(1010, 4000, 32) < 0);
    assert(counter.Read(2010, 5000, 32) == 3.125f);
    for (int provider = 0; provider < UpscalerCount; ++provider) {
        for (int preset = -1; preset < PresetCount; ++preset) {
            const unsigned state = Ps4RtPerfStats::ReconstructionState(provider,preset);
            assert(Ps4RtPerfStats::ReconstructionProvider(state) == provider);
            assert(Ps4RtPerfStats::ReconstructionPreset(state) == preset);
        }
    }
    Ps4RtPerfStats::Sample overlay_sample;
    overlay_sample.version = "0.4.0";
    assert(Ps4RtPerfStats::Format(overlay_sample, Ps4RtPerfStats::Version, true) == "Windows 0.4.0");
    assert(Ps4RtPerfStats::Format(overlay_sample, Ps4RtPerfStats::Fps | Ps4RtPerfStats::Version, false) ==
           "0 FPS\nWindows 0.4.0");
    overlay_sample.cpu_percent=15;overlay_sample.fps=60;overlay_sample.frame_ms=16.7f;
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::Fps|Ps4RtPerfStats::Cpu,true)=="60 FPS   CPU 15%");
    overlay_sample.cpu_name="Test CPU";overlay_sample.gpu_name="Test GPU";
    overlay_sample.show_cpu_model=true;overlay_sample.gpu_percent=42;
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::Cpu|Ps4RtPerfStats::Gpu,true)=="Test GPU 42%   Test CPU 15%");
    overlay_sample.show_cpu_model=false;overlay_sample.show_gpu_model=false;
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::Cpu|Ps4RtPerfStats::Gpu,false)=="GPU 42%\nCPU 15%");
    overlay_sample.show_cpu_model=true;overlay_sample.show_gpu_model=true;
    overlay_sample.cpu_name.clear();overlay_sample.gpu_name.clear();
    overlay_sample.cpu_percent=overlay_sample.gpu_percent=-1;
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::Cpu|Ps4RtPerfStats::Gpu,true)=="GPU N/A   CPU N/A");
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::Upscaler,true)=="Native · NO PRESET");
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::CpuThread,true)=="CPU thread N/A");
    overlay_sample.busiest_thread_percent=98;
    assert(Ps4RtPerfStats::Format(overlay_sample,Ps4RtPerfStats::CpuThread,true)=="CPU thread 98%");
    assert(!(Ps4RtPerfStats::DefaultItems & Ps4RtPerfStats::CpuThread));
    s.menu_tab=4; Save();s.menu_tab=0;Load();assert(s.menu_tab==4);
    for (int provider : {UpscalerOff, UpscalerFsr3, UpscalerFsr4}) {
        for (int output : {1, 2, 3}) {
            s.sharpen = true;
            s.sharpness = 0.42f;
            s.spatial_aa = true;
            s.anisotropy = 16;
            s.output_res = output;
            SetSceneUpscaler(provider, false);
            Save();
            Load();
            assert(s.upscaler == provider && s.startup_upscaler == provider);
            assert(s.output_res == output && s.startup_output_res == output);
            assert(s.sharpness > 0.419f && s.sharpness < 0.421f);
            assert(s.anisotropy == 16 && s.startup_anisotropy == 16);
            assert(!s.object_motion && !s.fsr1 && s.spatial_aa);
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
    s.upscaler=UpscalerFsrSdk24;ConfigureFsrSdkSupport(true,nullptr);
    assert(s.upscaler==UpscalerFsrSdk24 && s.fsr_sdk24_supported);
    Save();Load();assert(s.upscaler==UpscalerFsrSdk24);
    ConfigureFsrSdkSupport(false,"missing provider");assert(s.upscaler==UpscalerFsr3);
    s.fsr3_available=false;s.upscaler=UpscalerFsrSdk24;
    ConfigureFsrSdkSupport(false,"GPU policy");assert(s.upscaler==UpscalerOff);
    SetFsrSdkProviderName("AMD FSR 4.1.2");assert(FsrSdkProviderName()=="AMD FSR 4.1.2");
    unlink(path);
    std::puts("PASS: native-scene settings persistence and native-port defaults");
}
