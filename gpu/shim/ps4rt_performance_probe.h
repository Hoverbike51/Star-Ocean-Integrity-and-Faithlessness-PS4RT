// SPDX-License-Identifier: GPL-2.0-or-later
// Low-overhead gameplay performance capture. Data stays in memory until shutdown and is written
// as an English CSV timeline plus a compact JSON summary.

#pragma once

#include <cstdint>

namespace Vulkan {
class Instance;
class Rasterizer;
}

namespace Ps4RtPerformanceProbe {

void Start(const Vulkan::Instance& instance, const Vulkan::Rasterizer& rasterizer);
bool Active();
void AddGpuWait(std::uint64_t nanoseconds);
void AddHostWait(std::uint64_t nanoseconds);
void AddPresentWait(std::uint64_t nanoseconds);
struct PresentTiming {
    std::uint64_t total_ns{}, acquire_ns{}, compose_ns{}, submit_ns{}, present_ns{}, retire_ns{};
    unsigned reconstruction_state{}, output_width{}, output_height{};
};
void RecordFrame(const PresentTiming& timing);
void Finish();

} // namespace Ps4RtPerformanceProbe
