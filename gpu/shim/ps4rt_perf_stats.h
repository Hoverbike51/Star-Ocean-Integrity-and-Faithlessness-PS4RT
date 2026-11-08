// SPDX-License-Identifier: GPL-2.0-or-later
// ps4rt: numbers for the performance overlay (ps4rt_overlay.cpp). A thread started once samples
// CPU, RAM, GPU use (Windows counters) and VRAM (VK_EXT_memory_budget) twice a second;
// a value that cannot be measured stays negative and shows as "n/a".

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cwchar>
#include <string_view>

namespace Vulkan {
class Instance;
}

namespace Ps4RtPerfStats {

/// Overlay items (Ps4RtSettings::Values::overlay_items).
enum Item : unsigned {
    Fps = 1u << 0,
    FrameTime = 1u << 1,
    Upscaler = 1u << 2,
    Gpu = 1u << 3,
    Cpu = 1u << 4,
    Ram = 1u << 5,
    Vram = 1u << 6,
    Version = 1u << 7,
    AllItems = (1u << 8) - 1,
};
inline constexpr unsigned DefaultItems = Fps | FrameTime | Upscaler;

struct Sample {
    float fps = 0.0f, frame_ms = 0.0f;
    std::string upscaler;
    std::string version;
    std::string gpu_name;
    std::string cpu_name;
    bool show_gpu_model = true, show_cpu_model = false;
    std::string busiest_thread_name;
    float gpu_percent = -1.0f, cpu_percent = -1.0f, busiest_thread_percent = -1.0f;
    unsigned busiest_thread_id = 0, thread_count = 0;
    double ram_used_gb = -1.0, ram_total_gb = -1.0, vram_used_gb = -1.0, vram_total_gb = -1.0;
};

// Stable saved IDs: four original corners, then top center and bottom center.
struct Anchor { float x, y; };
inline Anchor PositionAnchor(int position) {
    switch (position) {
    case 0: return {0, 0};
    case 2: return {0, 1};
    case 3: return {1, 1};
    case 4: return {.5f, 0};
    case 5: return {.5f, 1};
    default: return {1, 0};
    }
}

// PDH engine instance identity. Do not combine another process or adapter's activity.
inline bool MatchesGpuEngine(std::wstring_view name, unsigned process, uint32_t high, uint32_t low) {
    unsigned pid = 0, luid_high = 0, luid_low = 0;
    int end = 0;
    const std::wstring terminated{name};
    return std::swscanf(terminated.c_str(), L"pid_%u_luid_0x%x_0x%x_%n",
                       &pid, &luid_high, &luid_low, &end) == 3 && end > 0 &&
           pid == process && luid_high == high && luid_low == low &&
           name.find(L"_engtype_3D", size_t(end)) != std::wstring_view::npos;
}

// Publish provider and preset together, only after a successful dispatch. Provider IDs are
// Ps4RtSettings::Upscaler; preset -1 means the original fixed-resolution scene.
inline unsigned ReconstructionState(int provider, int preset) {
    return unsigned(provider & 0xff) | (unsigned(std::clamp(preset, -1, 4) + 1) << 8);
}
inline int ReconstructionProvider(unsigned state) { return int(state & 0xff); }
inline int ReconstructionPreset(unsigned state) { return int((state >> 8) & 0xff) - 1; }

/// Process CPU time over wall time on every core, 0 to 100; -1 without a wall interval.
inline float CpuPercent(uint64_t process_delta, uint64_t wall_delta, unsigned cores) {
    if (!wall_delta || !cores) {
        return -1.0f;
    }
    return std::clamp(float(double(process_delta) * 100.0 / (double(wall_delta) * cores)), 0.0f, 100.0f);
}

/// The overlay text for the items in `mask`: one line each, or one line when `row`.
inline std::string Format(const Sample& s, unsigned mask, bool row) {
    std::string out;
    char part[160];
    const auto add = [&] {
        if (!out.empty()) {
            out += row ? "   " : "\n";
        }
        out += part;
    };
    const auto pair = [](char* to, size_t size, const char* label, double used, double total) {
        if (used < 0.0) {
            std::snprintf(to, size, "%s n/a", label);
        } else if (total < 0.0) {
            std::snprintf(to, size, "%s %.1f GB", label, used);
        } else {
            std::snprintf(to, size, "%s %.1f / %.1f GB", label, used, total);
        }
    };
    if (mask & Fps) {
        std::snprintf(part, sizeof(part), "%.0f FPS", s.fps);
        add();
    }
    if (mask & FrameTime) {
        std::snprintf(part, sizeof(part), "%.1f ms", s.frame_ms);
        add();
    }
    if (mask & Upscaler) {
        std::snprintf(part, sizeof(part), "%s", s.upscaler.empty() ? "Native · NO PRESET" : s.upscaler.c_str());
        add();
    }
    if (mask & Gpu) {
        const char* name = !s.show_gpu_model || s.gpu_name.empty() ? "GPU" : s.gpu_name.c_str();
        if (s.gpu_percent < 0.0f) {
            std::snprintf(part, sizeof(part), "%s N/A", name);
        } else {
            std::snprintf(part, sizeof(part), "%s %.0f%%", name, s.gpu_percent);
        }
        add();
    }
    if (mask & Cpu) {
        const char* name = !s.show_cpu_model || s.cpu_name.empty() ? "CPU" : s.cpu_name.c_str();
        if (s.cpu_percent < 0.0f) {
            std::snprintf(part, sizeof(part), "%s N/A", name);
        } else {
            std::snprintf(part, sizeof(part), "%s %.0f%%", name, s.cpu_percent);
        }
        add();
    }
    if (mask & Ram) {
        pair(part, sizeof(part), "RAM", s.ram_used_gb, s.ram_total_gb);
        add();
    }
    if (mask & Vram) {
        pair(part, sizeof(part), "VRAM", s.vram_used_gb, s.vram_total_gb);
        add();
    }
    if (mask & Version) {
        std::snprintf(part, sizeof(part), "Windows %s", s.version.c_str());
        add();
    }
    return out;
}

/// Starts the sampling thread the first time; later calls do nothing.
void Start(const Vulkan::Instance& instance);
/// The latest CPU, RAM, GPU and VRAM values (the caller fills FPS, frametime and upscaler).
Sample Latest();

} // namespace Ps4RtPerfStats
