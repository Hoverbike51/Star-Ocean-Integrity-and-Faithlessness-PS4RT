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
void RecordFrame(std::uint64_t present_cpu_nanoseconds);
void Finish();

} // namespace Ps4RtPerformanceProbe
