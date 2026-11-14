// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4rt_perf_stats.h"
#include "ps4rt_perf_windows.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <cmath>
#include <cstring>
#include <cpuid.h>
#include "video_core/renderer_vulkan/vk_instance.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>

namespace Ps4RtPerfStats {

namespace {

std::mutex mutex;
Sample latest;
std::once_flag started;
Windows::Clock::time_point sampled_at{};

// Selected process and adapter only. Use the busiest graphics/compute engine;
// summing concurrent engines overstates GPU saturation. Invalid data stays unavailable.
class GpuUsage {
public:
    explicit GpuUsage(const Vulkan::Instance& instance) {
        const auto properties = instance.GetPhysicalDevice().getProperties2<
            vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
        const auto& identity = properties.get<vk::PhysicalDeviceIDProperties>();
        if (!identity.deviceLUIDValid) return;
        static_assert(sizeof(identity.deviceLUID) == 8);
        std::memcpy(&luid_low, identity.deviceLUID.data(), 4);
        std::memcpy(&luid_high, identity.deviceLUID.data() + 4, 4);
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            query = nullptr;
            return;
        }
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) !=
            ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
            return;
        }
        PdhCollectQueryData(query);
    }

    ~GpuUsage() { if (query) PdhCloseQuery(query); }

    float Percent() {
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) {
            return -1.0f;
        }
        // Wildcard instances can change between the size query and read (hotplug/process exit).
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            DWORD size = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                             &size, &count, nullptr) != PDH_MORE_DATA || !size)
                return -1.0f;
            buffer.resize(size);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            const auto result = PdhGetFormattedCounterArrayW(counter,
                PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items);
            if (result == PDH_MORE_DATA) continue; // Re-query size from zero on the next attempt.
            if (result != ERROR_SUCCESS) return -1.0f;
            double busiest = -1.0;
            for (DWORD i = 0; i < count; ++i) {
                const auto status = items[i].FmtValue.CStatus;
                const double value = items[i].FmtValue.doubleValue;
                if (items[i].szName &&
                    (status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA) &&
                    std::isfinite(value) && value >= 0.0 &&
                    MatchesGpuEngine(items[i].szName, pid, luid_high, luid_low))
                    busiest = std::max(busiest, value);
            }
            return busiest < 0.0 ? -1.0f : std::clamp(float(busiest), 0.0f, 100.0f);
        }
        return -1.0f;
    }

private:
    const DWORD pid = GetCurrentProcessId();
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    uint32_t luid_low = 0, luid_high = 0;
    std::vector<unsigned char> buffer;
};

std::string CpuModel() {
    if (__get_cpuid_max(0x80000000, nullptr) < 0x80000004) return {};
    char brand[49]{};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned registers[4]{};
        if (!__get_cpuid(0x80000002 + i, &registers[0], &registers[1],
                         &registers[2], &registers[3])) return {};
        std::memcpy(brand + i * 16, registers, 16);
    }
    std::string result{brand};
    const auto first = result.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return result.substr(first, result.find_last_not_of(" \t\r\n") - first + 1);
}

void Run(const Vulkan::Instance* instance) {
    GpuUsage gpu{*instance};
    const std::string gpu_model{instance->GetModelName()}, cpu_model = CpuModel();
    CpuCounter process;
    Windows::ThreadUsage threads;
    const unsigned processors = Windows::LogicalProcessors();
    auto previous_sample = Windows::Clock::time_point{};
    constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
    while (true) {
        const auto begin = Windows::Clock::now();
        Sample s;
        s.gpu_name = gpu_model;
        s.cpu_name = cpu_model;
        s.logical_processors = processors;
        if (previous_sample != Windows::Clock::time_point{})
            s.sample_interval_ms = std::chrono::duration<double, std::milli>(begin - previous_sample).count();
        previous_sample = begin;
        FILETIME created{}, ended{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
            s.cpu_percent = process.Read(Windows::Ticks(kernel) + Windows::Ticks(user),
                                         Windows::Now(), processors);
        } else {
            process.Reset();
        }
        threads.Read(s);
        PROCESS_MEMORY_COUNTERS memory{};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
            s.ram_used_gb = double(memory.WorkingSetSize) / GiB;
        }
        MEMORYSTATUSEX system{.dwLength = sizeof(MEMORYSTATUSEX)};
        if (GlobalMemoryStatusEx(&system)) {
            s.ram_total_gb = double(system.ullTotalPhys) / GiB;
        }
        if (instance->CanReportMemoryUsage()) {
            s.vram_used_gb = double(instance->GetDeviceMemoryUsage()) / GiB;
            s.vram_total_gb = double(instance->GetDeviceMemoryBudgetNow()) / GiB;
        }
        s.gpu_percent = gpu.Percent();
        const auto end = Windows::Clock::now();
        s.sampling_ms = std::chrono::duration<double, std::milli>(end - begin).count();
        {
            std::scoped_lock lock{mutex};
            latest = std::move(s);
            sampled_at = end;
        }
        std::this_thread::sleep_until(begin + std::chrono::milliseconds(500));
    }
}

} // namespace

void Start(const Vulkan::Instance& instance) {
    // ponytail: never joined; a thread that ended while the game ran crashed in ntdll
    // (vk_pipeline_serialization.cpp, StopWarmUp). It sleeps between samples.
    std::call_once(started, [&] { std::thread(Run, &instance).detach(); });
}

Sample Latest() {
    std::scoped_lock lock{mutex};
    Sample result = latest;
    if (sampled_at != Windows::Clock::time_point{}) {
        result.sample_age_ms = std::chrono::duration<double, std::milli>(
            Windows::Clock::now() - sampled_at).count();
        if (result.sample_age_ms > 2000.0) {
            result.cpu_percent = result.gpu_percent = result.busiest_thread_percent = -1.0f;
            result.ram_used_gb = result.ram_total_gb = -1.0;
            result.vram_used_gb = result.vram_total_gb = -1.0;
            result.busiest_thread_id = result.thread_count = 0;
            result.busiest_thread_name.clear();
            result.top_threads = {};
        }
    }
    return result;
}

} // namespace Ps4RtPerfStats
