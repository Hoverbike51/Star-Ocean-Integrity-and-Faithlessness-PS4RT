// Real ImGui layout/geometry, all languages and anchors, with no renderer or game.
#include <windows.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include "../gpu/shim/ps4rt_technical_overlay.h"
#include "../launcher/localization.h"
#include "imgui_cpu_capture.h"

int main(int argc, char** argv) {
    const wchar_t* languages[] = {L"English",L"Français",L"Español",L"Português",L"Português (Brasil)",L"Italiano",L"Deutsch",L"Русский",L"简体中文",L"繁體中文",L"한국어",L"日本語"};
    const auto output = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
    if (!output.empty()) std::filesystem::create_directories(output);
    std::vector<double> times;
    unsigned checks = 0;
    for (int language = 0; language < 12; ++language) {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        assert(io.Fonts->AddFontFromFileTTF(PS4RT_TEST_FONT_PATH,18));
        const char* fallback = language == 8 ? "C:/Windows/Fonts/msyh.ttc" : language == 9 ? "C:/Windows/Fonts/msjh.ttc" :
                               language == 10 ? "C:/Windows/Fonts/malgun.ttf" : language == 11 ? "C:/Windows/Fonts/YuGothR.ttc" : nullptr;
        if (fallback && std::filesystem::is_regular_file(fallback)) {
            ImFontConfig merge; merge.MergeMode = true; io.Fonts->AddFontFromFileTTF(fallback,18,&merge);
        }
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding=10; style.WindowBorderSize=1.2f; style.FrameRounding=6;
        style.Colors[ImGuiCol_WindowBg]=ImVec4(.025f,.065f,.10f,1);
        style.Colors[ImGuiCol_Border]=ImVec4(.32f,.72f,.95f,.70f);
        style.Colors[ImGuiCol_FrameBg]=ImVec4(.08f,.18f,.30f,.85f);
        std::map<std::wstring,std::string> translations;
        const auto tr = [&](const wchar_t* en, const wchar_t* fr) -> const char* {
            auto [it, added] = translations.try_emplace(en);
            if (added) {
                const auto* translated = language == 1 ? fr : ps4rt_localize(languages[language],en);
                const int bytes = WideCharToMultiByte(CP_UTF8,0,translated,-1,nullptr,0,nullptr,nullptr);
                it->second.resize(bytes); WideCharToMultiByte(CP_UTF8,0,translated,-1,it->second.data(),bytes,nullptr,nullptr);
                it->second.pop_back();
            }
            return it->second.c_str();
        };
        for (const auto* key : {L"Update available",L"Installed version",L"New version",L"The launcher will restart after installation.",
            L"Your saves and settings are kept.",L"Later",L"Release notes",L"Download & install",L"Installation complete",L"Open backup folder",L"Done",L"Package",L"Extraction folder",L"Choose where the game files will be extracted.",L"CPU (process)",L"CPU thread"}) {
            const auto* translated = ps4rt_localize(languages[language], key);
            assert(translated && *translated);
            if (language) assert(std::wcscmp(translated,key));
        }
        for (auto viewport : {ImVec2(1024,768),ImVec2(1280,800),ImVec2(1920,1080),ImVec2(2520,1080),ImVec2(3840,1080)}) {
            io.DisplaySize = viewport; io.DeltaTime = 1.f / 60;
            for (int corner = 0; corner < 6; ++corner) for (float scale : {.5f,1.f,2.f}) {
                Ps4RtTechnicalOverlay::Config cfg;
                cfg.corner = corner; cfg.scale = scale; cfg.render_width = 3840; cfg.render_height = 1080;
                Ps4RtPerfStats::Sample sample;
                sample.show_gpu_model = sample.show_cpu_model = true;
                Ps4RtPerfStats::FrameHistory history;
                Ps4RtTechnicalOverlay::Geometry baseline{};
                for (int frame = 0; frame < 9; ++frame) {
                    sample.fps = frame % 2 ? 9 : 1000; sample.frame_ms = frame % 2 ? 999.9f : 16.7f;
                    sample.gpu_percent = sample.cpu_percent = sample.busiest_thread_percent = float(frame * 14 - 1);
                    sample.gpu_name = frame % 2 ? "AMD Radeon RX 9070 XT" : "A very long graphics adapter name that must never resize the panel";
                    sample.cpu_name = frame % 2 ? "AMD Ryzen 9 9950X3D2 16-Core Processor" : "CPU";
                    sample.upscaler = frame % 2 ? "Native" : "AMD FSR 4 v7 (INT8) · Ultra Performance";
                    sample.version = "1.1.3"; sample.thread_count = frame % 2 ? 9 : 1234; sample.logical_processors = 32;
                    sample.ram_used_gb = sample.vram_used_gb = frame % 2 ? 9.9 : 100;
                    sample.ram_total_gb = sample.vram_total_gb = 128;
                    sample.sample_interval_ms = 500; sample.sample_age_ms = frame % 2 ? 9 : 1000; sample.sampling_ms = frame % 2 ? .01 : 100;
                    history.Add(frame % 2 ? 16.7f : 100);
                    const auto begin = std::chrono::steady_clock::now();
                    ImGui::NewFrame(); const auto geometry = Ps4RtTechnicalOverlay::Draw(sample,history,cfg,tr); ImGui::Render();
                    const auto end = std::chrono::steady_clock::now();
                    if (frame >= 2) times.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
                    if (frame == 1) baseline = geometry;
                    if (frame > 1) {
                        assert(geometry.position.x == baseline.position.x && geometry.position.y == baseline.position.y);
                        assert(geometry.size.x == baseline.size.x && geometry.size.y == baseline.size.y && geometry.value_x == baseline.value_x);
                        ++checks;
                    }
                    assert(geometry.position.x >= 0 && geometry.position.y >= 0);
                    assert(geometry.position.x + geometry.size.x <= viewport.x + 1 && geometry.position.y + geometry.size.y <= viewport.y + 1);
                    const auto* draw = ImGui::GetDrawData();
                    for (const auto* list : draw->CmdLists) for (const auto& vertex : list->VtxBuffer)
                        assert(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y));
                    if (!output.empty() && language < 2 && viewport.x == 1920 && corner == 0 && scale == 1 && frame == 8) {
                        // Illustrative guide capture, separate from the extreme-value fixture.
                        sample.fps=60; sample.frame_ms=16.7f; sample.gpu_percent=24; sample.cpu_percent=3; sample.busiest_thread_percent=16;
                        sample.gpu_name="AMD Radeon RX 9070 XT"; sample.cpu_name="AMD Ryzen 9 9950X3D2 16-Core Processor";
                        sample.upscaler="AMD FSR 4.1.2 · Native (100%)"; sample.thread_count=84;
                        sample.ram_used_gb=3.4; sample.ram_total_gb=31.7; sample.vram_used_gb=2.3; sample.vram_total_gb=15.1;
                        sample.sample_interval_ms=501; sample.sample_age_ms=12; sample.sampling_ms=.64;
                        history={}; for(int i=0;i<240;++i)history.Add(i==120?17.f:16.7f);
                        ImGui::NewFrame(); const auto illustration=Ps4RtTechnicalOverlay::Draw(sample,history,cfg,tr); ImGui::Render();
                        assert(illustration.size.x==baseline.size.x && illustration.value_x==baseline.value_x);
                        CaptureMenuDrawData(output/("technical-"+std::to_string(language)+".bmp"));
                    }
                    for (auto* texture : io.Fonts->TexList) if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) texture->SetStatus(ImTextureStatus_OK);
                }
            }
        }
        ImGui::DestroyContext();
    }
    std::sort(times.begin(),times.end());
    if (!output.empty()) std::ofstream(output/"technical.json") << "{\"geometry_checks\":" << checks << ",\"languages\":12,\"ratios\":5,\"anchors\":6,\"p95_ms\":" << times[times.size()*95/100] << ",\"max_ms\":" << times.back() << "}\n";
    std::printf("Technical overlay: %u stable-geometry checks; CPU draw p95 %.3f ms, max %.3f ms\n",checks,times[times.size()*95/100],times.back());
}
