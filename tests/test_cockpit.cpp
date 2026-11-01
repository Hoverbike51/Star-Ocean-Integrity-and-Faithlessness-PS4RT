// Exercise the actual native menu/layout and input code, without guest data or a GPU.
#include "../gpu/shim/ps4rt_overlay.cpp"
#include <cassert>
#include "imgui_cpu_capture.h"

int main(int argc,char** argv) {
    const wchar_t* languages[]={L"English",L"Français",L"Español",L"Português",L"Português (Brasil)",L"Italiano",L"Deutsch",L"Русский",L"简体中文",L"繁體中文",L"한국어",L"日本語"};
    const char* resumes[]={"Resume","Reprendre","Continuar","Retomar","Continuar","Riprendi","Fortsetzen","Продолжить","继续游戏","繼續遊戲","계속하기","ゲームに戻る"};
    const int language=argc>1?std::atoi(argv[1]):0;assert(language>=0 && language<12);
    SetEnvironmentVariableW(L"SO5_UI_LANGUAGE",languages[language]);
    const auto config=std::filesystem::temp_directory_path()/("so5-cockpit-"+std::to_string(GetCurrentProcessId())+".ini");
    SetEnvironmentVariableW(L"PS4RT_CONFIG",config.c_str());
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
    assert(!std::strcmp(Tr(L"Resume",L"Reprendre"),resumes[language]));
    if(language) assert(std::strcmp(Tr(L"Output resolution",L"Résolution de sortie"),"Output resolution"));
    initialized=true;SetOpen(true);
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
        for(int tab=0;tab<5;++tab) {requested_tab=tab;frame();frame();assert(s.menu_tab==tab);}
    }
    requested_tab=4;frame();frame();
    io.AddKeyEvent(ImGuiKey_E,true);frame();frame();assert(s.menu_tab==0);
    io.AddKeyEvent(ImGuiKey_E,false);frame();
    io.AddKeyEvent(ImGuiKey_Q,true);frame();frame();assert(s.menu_tab==4);
    io.AddKeyEvent(ImGuiKey_Q,false);frame();
    if(argc>2) {
        io.DisplaySize=ImVec2(1280,720);
        Ps4RtPng::Image logo;
        if(Ps4RtPng::LoadRgba("launcher/assets/star-ocean-logo.png",logo)) {
            menu_logo_texture=IM_NEW(ImTextureData)();menu_logo_texture->Create(ImTextureFormat_RGBA32,logo.width,logo.height);
            std::memcpy(menu_logo_texture->GetPixels(),logo.rgba.data(),logo.rgba.size());
            ImGui::RegisterUserTexture(menu_logo_texture);menu_logo_size=ImVec2(float(logo.width),float(logo.height));
        }
        for(int tab=0;tab<5;++tab) {requested_tab=tab;frame();frame();CaptureMenuDrawData(std::string(argv[2])+"-"+std::to_string(tab)+".bmp");}
    }
    s.startup_anisotropy=4;
    Store(s.anisotropy,16,true);assert(s.anisotropy==16 && s.startup_anisotropy==4);
    SetOpen(false);Ps4RtSettings::Load();assert(s.anisotropy==16 && s.startup_anisotropy==16);
    SetOpen(true);
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
    ImGui::DestroyContext();initialized=false;
    std::filesystem::remove(config);
    std::printf("PASS: native Cockpit five tabs, three viewport sizes, input navigation and locale %d\n",language);
}
