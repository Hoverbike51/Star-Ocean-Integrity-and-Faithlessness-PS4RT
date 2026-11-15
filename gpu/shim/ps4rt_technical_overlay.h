// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ps4rt_perf_stats.h"
#include "imgui.h"
#include <cfloat>

namespace Ps4RtTechnicalOverlay {
struct Config {
    float base_scale = 1, scale = 1, opacity = .8f;
    int corner = 0, render_width = 0, render_height = 0;
    ImFont* font = nullptr;
};
struct Geometry { ImVec2 position, size; float value_x = 0; };

// Both panel geometry and the value column depend only on language, configuration
// and viewport. Unknown counters, changing digits and provider/device names cannot move them.
template<class Localize>
Geometry Draw(const Ps4RtPerfStats::Sample& s, const Ps4RtPerfStats::FrameHistory& history,
              const Config& cfg, Localize tr) {
    const auto* vp = ImGui::GetMainViewport();
    ImFont* font = cfg.font ? cfg.font : ImGui::GetFont();
    const char* labels[] = {"FPS", tr(L"Frame time", L"Temps par image"),
        tr(L"Upscaling", L"Reconstruction"), "GPU", tr(L"CPU (process)", L"CPU (processus)"),
        tr(L"CPU thread", L"Thread CPU"), "RAM", "VRAM", "Windows",
        tr(L"Threads / logical CPUs", L"Threads / processeurs logiques"),
        tr(L"Applied render resolution", L"Résolution de rendu appliquée"),
        tr(L"Output resolution", L"Résolution de sortie"),
        tr(L"Sampling: interval / age / cost", L"Mesure : intervalle / âge / coût"),
        tr(L"Frame time: average / peak", L"Temps par image : moyenne / pic")};
    constexpr float font_size = 18, line = 24, inset = 16, gap = 18;
    float label_width = 0;
    for (const auto* label : labels) label_width = std::max(label_width, font->CalcTextSizeA(font_size, FLT_MAX, 0, label).x);
    // Reserve the longest numeric field independently from the current reading.
    const float value_width = font->CalcTextSizeA(font_size, FLT_MAX, 0, "99999 ms / 99999 ms / 999.99 ms").x;
    const float logical_width = std::max(560.f, 2 * inset + label_width + gap + value_width);
    const int device_rows = int(s.show_gpu_model) + int(s.show_cpu_model);
    const float logical_height = 2 * inset + line * (16 + device_rows) + 56;
    const float pad = 12 * cfg.base_scale;
    const ImVec2 available(std::max(1.f, vp->WorkSize.x - 2 * pad), std::max(1.f, vp->WorkSize.y - 2 * pad));
    const float scale = std::max(.01f, std::min({cfg.scale * cfg.base_scale,
        available.x / logical_width, available.y / logical_height}));
    const ImVec2 size(std::ceil(logical_width * scale), std::ceil(logical_height * scale));
    const auto anchor = Ps4RtPerfStats::PositionAnchor(cfg.corner);
    const ImVec2 position(vp->WorkPos.x + pad + (available.x - size.x) * anchor.x,
                          vp->WorkPos.y + pad + (available.y - size.y) * anchor.y);
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(cfg.opacity);
    ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin(position.x + inset * scale, position.y + inset * scale);
    const float right = position.x + size.x - inset * scale;
    const float value_x = origin.x + (label_width + gap) * scale;
    const ImU32 cyan = IM_COL32(82, 214, 255, 255), green = IM_COL32(123, 234, 174, 255);
    const ImU32 amber = IM_COL32(255, 207, 132, 255), purple = IM_COL32(201, 176, 255, 255);
    const ImU32 white = IM_COL32(239, 246, 252, 255), muted = IM_COL32(155, 181, 202, 255);
    const ImU32 divider = IM_COL32(62, 117, 145, 180);
    const float fh = font_size * scale, lh = line * scale;
    float y = origin.y;
    const auto cell = [&](const char* value, float x, float end, float at_y, ImU32 color) {
        draw->PushClipRect(ImVec2(x, at_y), ImVec2(end, at_y + lh), true);
        draw->AddText(font, fh, ImVec2(x, at_y), color, value);
        draw->PopClipRect();
    };
    const auto row = [&](int label, const char* value, ImU32 color = 0) {
        cell(labels[label], origin.x, value_x - gap * scale / 2, y, color ? color : muted);
        cell(value, value_x, right, y, value[0] == 'N' && value[1] == '/' ? muted : white);
        y += lh;
    };
    const auto section = [&] { draw->AddLine(ImVec2(origin.x, y - 4 * scale), ImVec2(right, y - 4 * scale), divider); };
    cell(tr(L"Technical", L"Technique"), origin.x, right, y, cyan); y += lh; section();
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%.0f FPS", s.fps); row(0, buffer, green);
    std::snprintf(buffer, sizeof(buffer), "%.1f ms", s.frame_ms); row(1, buffer);
    // Providers get a full fixed-width row so long preset names never displace counters.
    cell(s.upscaler.empty() ? "Native" : s.upscaler.c_str(), origin.x, right, y, cyan); y += lh;
    const auto percent = [&](int label, float value, ImU32 color) {
        if (std::isfinite(value) && value >= 0) std::snprintf(buffer, sizeof(buffer), "%.0f%%", value);
        else std::snprintf(buffer, sizeof(buffer), "N/A");
        row(label, buffer, color);
    };
    percent(3, s.gpu_percent, cyan); percent(4, s.cpu_percent, amber); percent(5, s.busiest_thread_percent, amber);
    const auto memory = [&](int label, double used, double total) {
        if (!std::isfinite(used) || used < 0) std::snprintf(buffer, sizeof(buffer), "N/A");
        else if (!std::isfinite(total) || total < 0) std::snprintf(buffer, sizeof(buffer), "%.1f GB", used);
        else std::snprintf(buffer, sizeof(buffer), "%.1f / %.1f GB", used, total);
        row(label, buffer, purple);
    };
    memory(6, s.ram_used_gb, s.ram_total_gb); memory(7, s.vram_used_gb, s.vram_total_gb);
    row(8, s.version.c_str()); section();
    if (s.show_gpu_model) { cell(s.gpu_name.empty() ? "GPU N/A" : s.gpu_name.c_str(), origin.x, right, y, cyan); y += lh; }
    if (s.show_cpu_model) { cell(s.cpu_name.empty() ? "CPU N/A" : s.cpu_name.c_str(), origin.x, right, y, amber); y += lh; }
    if (s.logical_processors) std::snprintf(buffer, sizeof(buffer), "%u / %u", s.thread_count, s.logical_processors);
    else std::snprintf(buffer, sizeof(buffer), "N/A");
    row(9, buffer);
    if (cfg.render_width > 0 && cfg.render_height > 0) std::snprintf(buffer, sizeof(buffer), "%d x %d", cfg.render_width, cfg.render_height);
    else std::snprintf(buffer, sizeof(buffer), "N/A");
    row(10, buffer);
    std::snprintf(buffer, sizeof(buffer), "%.0f x %.0f", vp->Size.x, vp->Size.y); row(11, buffer);
    if (s.sample_interval_ms >= 0 && s.sample_age_ms >= 0 && s.sampling_ms >= 0)
        std::snprintf(buffer, sizeof(buffer), "%.0f ms / %.0f ms / %.2f ms", s.sample_interval_ms, s.sample_age_ms, s.sampling_ms);
    else std::snprintf(buffer, sizeof(buffer), "N/A");
    row(12, buffer); section();
    if (history.count) std::snprintf(buffer, sizeof(buffer), "%.1f / %.1f ms", history.Mean(), history.Peak());
    else std::snprintf(buffer, sizeof(buffer), "N/A");
    row(13, buffer);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
    ImGui::PushStyleColor(ImGuiCol_PlotLines, cyan);
    ImGui::PlotLines("##frametimes", history.values.data(), int(history.count), int(history.Offset()),
        nullptr, 0.f, std::clamp(history.Peak() * 1.1f, 33.4f, 250.f), ImVec2(right - origin.x, 56 * scale));
    ImGui::PopStyleColor();
    ImGui::End();
    return {position, size, value_x};
}
} // namespace Ps4RtTechnicalOverlay
