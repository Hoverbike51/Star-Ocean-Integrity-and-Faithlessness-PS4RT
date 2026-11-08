// Exercise the actual native menu/layout and input code, without guest data or a GPU.
#include "../gpu/shim/ps4rt_overlay.cpp"
#include <cassert>
#include "imgui_cpu_capture.h"
extern "C" void test_gpu_cheat_fixture();

int main(int argc,char** argv) {
    const wchar_t* languages[]={L"English",L"Français",L"Español",L"Português",L"Português (Brasil)",L"Italiano",L"Deutsch",L"Русский",L"简体中文",L"繁體中文",L"한국어",L"日本語"};
    const char* resumes[]={"Resume","Reprendre","Continuar","Retomar","Continuar","Riprendi","Fortsetzen","Продолжить","继续游戏","繼續遊戲","계속하기","ゲームに戻る"};
    const int language=argc>1?std::atoi(argv[1]):0;assert(language>=0 && language<12);
    SetEnvironmentVariableW(L"SO5_UI_LANGUAGE",languages[language]);
    const auto config=std::filesystem::temp_directory_path()/("so5-cockpit-"+std::to_string(GetCurrentProcessId())+".ini");
    _putenv_s("PS4RT_CONFIG",config.string().c_str());
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    Ps4RtOverlay::ConfigureMenuAppearance();
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    auto& s=Ps4RtSettings::Get();
    s.native_scene=true;s.display_window_modes=Ps4RtOutput::Fitting(3840,2160);
    s.display_fullscreen_modes=s.display_window_modes.load();s.output_res=3;
    using namespace Ps4RtOverlay;
    for (const auto* key : {L"Show GPU model",L"Show CPU model",L"Top center",L"Bottom center"}) {
        const auto* text=ps4rt_localize(languages[language],key);
        assert(text && *text);
        if(language) assert(std::wcscmp(text,key));
    }
    assert(ReconstructionLabel(0)=="Native · NO PRESET");
    assert(ReconstructionLabel(Ps4RtPerfStats::ReconstructionState(Ps4RtSettings::UpscalerDlss,0))=="DLAA");
    assert(ReconstructionLabel(Ps4RtPerfStats::ReconstructionState(Ps4RtSettings::UpscalerTaa,0))=="TAA · NO PRESET");
    for(int provider:{Ps4RtSettings::UpscalerFsr3,Ps4RtSettings::UpscalerFsr4,Ps4RtSettings::UpscalerDlss}) {
        for(int preset=1;preset<Ps4RtSettings::PresetCount;++preset) {
            const auto text=ReconstructionLabel(Ps4RtPerfStats::ReconstructionState(provider,preset));
            assert(text.find(" · ")!=std::string::npos && text.find("NO PRESET")==std::string::npos);
            assert(text.find("DLAA")==std::string::npos);
        }
    }
    assert(!std::strcmp(Tr(L"Resume",L"Reprendre"),resumes[language]));
      for (const auto* text:{L"Tweaks",L"Camera fixes",L"HUD adjust",
          L"Configure before launch. During play, use Tweaks in the in-game menu.",
          L"Softens positional camera follow. Original restores the game setting.",
          L"During exploration, use D-pad left/right or O/P to change leader. Your choice is saved.",
          L"HUD layout saved for the next launch.",L"Change leader during exploration",L"This patch is unavailable for the prepared game version.",
          L"After changing leader, move clear of a zone entrance before using it. Story transitions remain unchanged.",
          L"Exploration leader",L"Exploration leader unavailable in the current scene.",
          L"During exploration, use D-pad left/right or O/P to change leader. Your choice is saved and restored after loading or battle. Menu choices apply after Resume.",
          L"Resume to apply the selected exploration leader.",L"Leader changed.",
          L"Leader change refused. See Logs and diagnostics.",
          L"Camera follow",L"Light damping",L"Damped follow",
        L"Adjusts positional follow damping immediately. Rotation controls and native collisions are unchanged. Original restores the game setting."}) {
        const auto* localized=ps4rt_localize(languages[language],text);
        assert(localized && *localized);
        if(language) assert(std::wcscmp(localized,text));
    }
    for(const auto& d:So5Cheats::Definitions) {
        const auto* title=ps4rt_localize(languages[language],d.title);
        const auto* detail=ps4rt_localize(languages[language],d.detail);
        assert(title[0]&&detail[0]);
        if(language){assert(std::wcscmp(title,d.title));assert(std::wcscmp(detail,d.detail));}
        if(d.warning){
            const auto* warning=ps4rt_localize(languages[language],d.warning);assert(warning[0]);
            if(language)assert(std::wcscmp(warning,d.warning));
        }
    }
    if(language) assert(std::strcmp(Tr(L"Output resolution",L"Résolution de sortie"),"Output resolution"));
    for(const wchar_t* text:{L"Applied render resolution",
        L"Without reconstruction, choose the internal resolution manually. The UI keeps the output resolution. With FSR or DLSS, quality presets control the internal resolution.",
        L"HUD layout",L"Select a HUD preset",L"Refresh presets",L"Open HUD presets folder",
        L"Could not open the HUD presets folder.",L"Ready",L"Controller shortcut R2 + L2",
        L"Scanning HUD presets…",L"Applying HUD layout…",L"HUD layout queued for the next frame.",
        L"HUD layout could not be changed. See Logs and diagnostics.",
        L"Place exported JSON presets in the game’s hud-presets folder. Incompatible presets are disabled."}) {
        const auto* localized=ps4rt_localize(languages[language],text);assert(localized&&*localized);
        if(language)assert(std::wcscmp(localized,text));
    }
    initialized=true;SetOpen(true);
    test_gpu_cheat_fixture();
    const auto frame=[&] {
        io.DeltaTime=1.0f/60;ImGui::NewFrame();Menu();ImGui::Render();
        for(auto* texture:ImGui::GetPlatformIO().Textures) {
            if(texture->Status==ImTextureStatus_WantCreate) texture->SetTexID(ImTextureID(reinterpret_cast<uintptr_t>(texture)));
            if(texture->Status==ImTextureStatus_WantCreate || texture->Status==ImTextureStatus_WantUpdates) texture->SetStatus(ImTextureStatus_OK);
        }
        assert(ImGui::GetDrawData()->CmdListsCount>0 && ImGui::GetDrawData()->TotalVtxCount>0);
        const auto* window=ImGui::FindWindowByName("##so5-cockpit");assert(window);
        assert(window->Pos.x>=0 && window->Pos.y>=0);
        assert(window->Pos.x+window->Size.x<=io.DisplaySize.x+1);
        assert(window->Pos.y+window->Size.y<=io.DisplaySize.y+1);
        for(int i=0;i<GImGui->Tables.GetBufSize();++i) {
            const auto* table=GImGui->Tables.GetByIndex(i);
            if(table->LastFrameActive!=GImGui->FrameCount || table->ColumnsCount!=2) continue;
            const float total=table->Columns[0].WidthGiven+table->Columns[1].WidthGiven;
            assert(table->Columns[0].WidthGiven>total*.35f && table->Columns[1].WidthGiven>total*.35f);
        }
    };
    for(const auto size:{ImVec2(1920,1080),ImVec2(1280,720),ImVec2(800,600)}) {
        io.DisplaySize=size;
        for(int tab=0;tab<7;++tab) {requested_tab=tab;frame();frame();assert(s.menu_tab==tab);}
    }
    const wchar_t* wide_notice=L"Experimental ultrawide: restart the game to change aspect ratio. HUD and movies keep their original proportions.";
    if(language) assert(std::wcscmp(ps4rt_localize(languages[language],wide_notice),wide_notice));
    for(const auto size:{ImVec2(1600,450),ImVec2(1600,686)}) {
        const int output=Ps4RtOutput::Find(int(size.x),int(size.y));
        Ps4RtUltrawide::session_output=output;s.output_res=output;s.display_mode=Ps4RtGraphics::Windowed;
        s.output_aspect=Ps4RtOutput::Group(output);io.DisplaySize=size;
        for(int tab:{0,1,6}) {requested_tab=tab;frame();frame();assert(s.output_res==output);}
    }
    Ps4RtUltrawide::session_output=-1;s.output_res=3;s.output_aspect=Ps4RtOutput::All;
    io.DisplaySize=ImVec2(800,600);
    requested_tab=5;frame();frame();
    io.AddKeyEvent(ImGuiKey_E,true);frame();frame();assert(s.menu_tab==0);
    io.AddKeyEvent(ImGuiKey_E,false);frame();
    io.AddKeyEvent(ImGuiKey_Q,true);frame();frame();assert(s.menu_tab==5);
    io.AddKeyEvent(ImGuiKey_Q,false);frame();
    requested_tab=2;frame();frame();
    io.AddKeyEvent(ImGuiKey_E,true);frame();frame();assert(s.menu_tab==6);
    io.AddKeyEvent(ImGuiKey_E,false);frame();
    io.AddKeyEvent(ImGuiKey_E,true);frame();frame();assert(s.menu_tab==3);
    io.AddKeyEvent(ImGuiKey_E,false);frame();
    if(argc>2) {
        io.DisplaySize=ImVec2(1280,720);
        Ps4RtPng::Image logo;
        if(Ps4RtPng::LoadRgba("launcher/assets/star-ocean-logo.png",logo)) {
            menu_logo_texture=IM_NEW(ImTextureData)();menu_logo_texture->Create(ImTextureFormat_RGBA32,logo.width,logo.height);
            std::memcpy(menu_logo_texture->GetPixels(),logo.rgba.data(),logo.rgba.size());
            ImGui::RegisterUserTexture(menu_logo_texture);menu_logo_size=ImVec2(float(logo.width),float(logo.height));
        }
        for(int tab=0;tab<7;++tab) {requested_tab=tab;frame();frame();CaptureMenuDrawData(std::string(argv[2])+"-"+std::to_string(tab)+".bmp");}
        requested_tab=2;frame();frame();
        char result[64]{};assert(runtime_cheat_select(7,1,result,sizeof(result))==0);
        for(auto* window:GImGui->Windows)if(window->Active&&std::strstr(window->Name,"##live-controls"))ImGui::SetScrollY(window,window->ScrollMax.y);
        frame();frame();CaptureMenuDrawData(std::string(argv[2])+"-cheats-bottom.bmp");
        assert(runtime_cheat_set(7,0,result,sizeof(result))==0);
    }
    s.startup_anisotropy=4;
    Store(s.anisotropy,16,true);assert(s.anisotropy==16 && s.startup_anisotropy==4);
    SetOpen(false);Ps4RtSettings::Load();assert(s.anisotropy==16 && s.startup_anisotropy==16);
    SetOpen(true);
    s.upscaler=Ps4RtSettings::UpscalerOff;s.scene_preset=3;pending_scene_preset=4;
    s.native_render_height=1440;dirty=true;SetOpen(false);
    assert(s.scene_preset==3 && s.native_render_height==1440);
    Ps4RtSettings::Load();assert(s.scene_preset==3 && s.native_render_height==1440);
    SetOpen(true);s.upscaler=Ps4RtSettings::UpscalerFsr3;
    s.scene_preset=-1; pending_scene_preset=1;
    assert(s.scene_preset==-1); SetOpen(false); assert(s.scene_preset==1);
    SetOpen(true); assert(pending_scene_preset==1);
    for(const auto size:{ImVec2(1920,1080),ImVec2(1280,720),ImVec2(800,600)}) {
        io.DisplaySize=size;cockpit_exit_confirmation=true;frame();frame();
        const auto* popup=ImGui::FindWindowByName("##so5-exit");assert(popup && popup->Active);
        assert(popup->Pos.x>=0 && popup->Pos.y>=0 && popup->Pos.x+popup->Size.x<=size.x+1 && popup->Pos.y+popup->Size.y<=size.y+1);
        assert(std::abs(popup->Pos.x+popup->Size.x*.5f-size.x*.5f)<2);
        assert(std::abs(popup->Pos.y+popup->Size.y*.5f-size.y*.5f)<2);
        cockpit_exit_confirmation=false;frame();frame();
    }
    cockpit_exit_confirmation=true;
    SDL_Event escape{};escape.type=SDL_EVENT_KEY_DOWN;escape.key.key=SDLK_ESCAPE;
    assert(HandleEvent(escape) && menu_open);
    cockpit_exit_confirmation=false;dirty=false;
    assert(HandleEvent(escape) && !menu_open);
    // Exercise the real compact selector through mouse, keyboard and controller navigation.
    for(const auto size:{ImVec2(1920,1080),ImVec2(1280,720),ImVec2(800,600)}) {
        io.DisplaySize=size;int choice=0;ImVec2 minimum{},maximum{};
        const auto slider_frame=[&](bool focus=false) {
            io.DeltaTime=1.f/60;ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20,20),ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(340,180),ImGuiCond_Always);
            ImGui::Begin("##cheat-slider-regression",nullptr,ImGuiWindowFlags_NoSavedSettings);
            if(focus)ImGui::SetKeyboardFocusHere();
            CheatValueSelector(choice,2,148,"×1.5");
            minimum=ImGui::GetItemRectMin();maximum=ImGui::GetItemRectMax();
            assert(maximum.x<=ImGui::GetWindowPos().x+ImGui::GetWindowWidth()-6);
            assert(maximum.x-minimum.x<=148.1f&&maximum.y-minimum.y<34);
            ImGui::End();ImGui::Render();
        };
        slider_frame();slider_frame();
        io.AddMousePosEvent(maximum.x-3,(minimum.y+maximum.y)*.5f);
        io.AddMouseButtonEvent(0,true);slider_frame();slider_frame();assert(choice==1);
        io.AddMouseButtonEvent(0,false);slider_frame();slider_frame(true);slider_frame();
        io.AddKeyEvent(ImGuiKey_LeftArrow,true);slider_frame();slider_frame();assert(choice==0);
        io.AddKeyEvent(ImGuiKey_LeftArrow,false);slider_frame();
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight,true);slider_frame();slider_frame();assert(choice==1);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight,false);slider_frame();
    }
    // Pending three-position movement control: x1 is genuinely off, then x1.5 and x2.
    // Run these interaction checks with the next authorized build.
    io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();
    for(const auto size:{ImVec2(1920,1080),ImVec2(1280,720),ImVec2(800,600)}) {
        io.DisplaySize=size;int position=0;ImVec2 minimum{},maximum{};
        const auto switch_frame=[&] {
            io.DeltaTime=1.f/60;ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20,20),ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(340,180),ImGuiCond_Always);
            ImGui::Begin("##movement-three-position-regression",nullptr,ImGuiWindowFlags_NoSavedSettings);
            CheatMovementSwitch(position,148);
            minimum=ImGui::GetItemRectMin();maximum=ImGui::GetItemRectMax();
            assert(maximum.x<=ImGui::GetWindowPos().x+ImGui::GetWindowWidth()-6);
            assert(maximum.x-minimum.x<=148.1f&&maximum.y-minimum.y<=31*base_scale);
            ImGui::End();ImGui::Render();
        };
        switch_frame();switch_frame();assert(position==0);
        for(int tick:{2,1,0}) {
            io.AddMousePosEvent(minimum.x+(maximum.x-minimum.x)*(tick+.5f)/3,(minimum.y+maximum.y)*.5f);
            io.AddMouseButtonEvent(0,true);switch_frame();
            io.AddMouseButtonEvent(0,false);switch_frame();switch_frame();assert(position==tick);
        }
        for(int next:{1,2}){
            io.AddKeyEvent(ImGuiKey_RightArrow,true);switch_frame();switch_frame();assert(position==next);
            io.AddKeyEvent(ImGuiKey_RightArrow,false);switch_frame();
        }
        for(int next:{1,0}){
            io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,true);switch_frame();switch_frame();assert(position==next);
            io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,false);switch_frame();
        }
    }
    ImGui::DestroyContext();initialized=false;
    std::filesystem::remove(config);
    std::printf("PASS: native Cockpit seven tabs, three viewport sizes, input navigation and locale %d\n",language);
}
