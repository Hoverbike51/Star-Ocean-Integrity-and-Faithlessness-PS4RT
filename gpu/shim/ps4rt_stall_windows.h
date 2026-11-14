// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Ps4RtPerformanceProbe {
// Stores ranges into the existing CSV timeline; never duplicates per-frame payloads.
struct StallWindows {
    struct Window {
        std::uint64_t first{}, trigger{}, worst{}, last{}, stalls{};
        double peak_ms{}, until_ms{};
    };
    struct Point { std::uint64_t index{}; double elapsed{}; };
    std::array<Window, 32> windows{};
    std::array<Point, 240> history{};
    std::size_t count{}, next{}, history_count{};
    std::uint64_t dropped_stalls{};
    // Returns the window to snapshot when a new worst stall is observed.
    int Observe(std::uint64_t index, double elapsed, double ms) {
        int snapshot = -1;
        if (count && elapsed <= windows[count-1].until_ms) windows[count-1].last = index;
        if (ms > 100.0) {
            Window* w = nullptr;
            if (count && elapsed-2000.0 <= windows[count-1].until_ms) w = &windows[count-1];
            else if (count < windows.size()) {
                w = &windows[count++];
                w->first = index;
                for (std::size_t j=0; j<history_count; ++j) {
                    const auto& p = history[(next + history.size() - history_count + j) % history.size()];
                    if (p.elapsed >= elapsed-2000.0) { w->first = p.index; break; }
                }
                w->trigger = index;
            }
            if (w) {
                ++w->stalls; w->last = index; w->until_ms = elapsed+2000.0;
                if (ms > w->peak_ms) { w->peak_ms=ms; w->worst=index; snapshot=int(count-1); }
            } else ++dropped_stalls;
        }
        history[next] = {index, elapsed}; next = (next+1) % history.size();
        history_count = std::min(history_count+1, history.size());
        return snapshot;
    }
};
} // namespace Ps4RtPerformanceProbe
