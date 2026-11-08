// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4rt_perf_stats.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <cstring>
#include <cpuid.h>
#include "video_core/renderer_vulkan/vk_instance.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace Ps4RtPerfStats {

namespace {

std::mutex mutex;
Sample latest;
std::once_flag started;

uint64_t Ticks(const FILETIME& time) {
    return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

std::string ThreadName(HANDLE thread) {
    PWSTR wide{};
    if (FAILED(GetThreadDescription(thread, &wide)) || !wide) {
        return "<unnamed>";
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string result = bytes > 1 ? std::string(size_t(bytes), '\0') : std::string{"<unnamed>"};
    if (bytes > 1) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), bytes, nullptr, nullptr);
        result.pop_back();
    }
    LocalFree(wide);
    return result;
}

// This process's 3D-engine utilization from Windows GPU Engine counters. Restrict the
// measurement to the selected Vulkan adapter's LUID. Unavailable/invalid data stays negative.
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
        DWORD size = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                         nullptr) != PDH_MORE_DATA) {
            return -1.0f;
        }
        buffer.resize(size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                         items) != ERROR_SUCCESS) {
            return -1.0f;
        }
        double total = 0.0;
        bool matched = false;
        for (DWORD i = 0; i < count; ++i) {
            const wchar_t* name = items[i].szName;
            const auto status = items[i].FmtValue.CStatus;
            const double value = items[i].FmtValue.doubleValue;
            if (name && (status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA) &&
                std::isfinite(value) && value >= 0.0 &&
                MatchesGpuEngine(name, GetCurrentProcessId(), luid_high, luid_low)) {
                total += value;
                matched = true;
            }
        }
        return matched ? std::clamp(float(total), 0.0f, 100.0f) : -1.0f;
    }

private:
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
    FILETIME created, ended, kernel, user, now;
    uint64_t last_process = 0, last_wall = 0;
    std::unordered_map<DWORD, uint64_t> last_thread_ticks;
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
    while (true) {
        Sample s;
        s.gpu_name = gpu_model;
        s.cpu_name = cpu_model;
        GetSystemTimeAsFileTime(&now);
        uint64_t wall_delta = 0;
        if (GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user)) {
            const uint64_t process = Ticks(kernel) + Ticks(user), wall = Ticks(now);
            if (last_wall) {
                wall_delta = wall - last_wall;
                s.cpu_percent = CpuPercent(process - last_process, wall_delta, cores);
            }
            last_process = process;
            last_wall = wall;
        }
        // Process-wide CPU usage hides a saturated game thread on high-core-count CPUs. Sample
        // every thread twice a second and report the busiest one as a percentage of one core.
        std::unordered_map<DWORD, uint64_t> current_thread_ticks;
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 entry{.dwSize = sizeof(THREADENTRY32)};
            if (Thread32First(snapshot, &entry)) {
                do {
                    if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
                    ++s.thread_count;
                    const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                                                     entry.th32ThreadID);
                    if (!thread) continue;
                    FILETIME thread_created{}, thread_ended{}, thread_kernel{}, thread_user{};
                    if (GetThreadTimes(thread, &thread_created, &thread_ended, &thread_kernel,
                                       &thread_user)) {
                        const uint64_t ticks = Ticks(thread_kernel) + Ticks(thread_user);
                        current_thread_ticks.emplace(entry.th32ThreadID, ticks);
                        if (wall_delta) {
                            const auto previous = last_thread_ticks.find(entry.th32ThreadID);
                            if (previous != last_thread_ticks.end() && ticks >= previous->second) {
                                const float percent = std::clamp(
                                    float(double(ticks - previous->second) * 100.0 /
                                          double(wall_delta)), 0.0f, 100.0f);
                                if (percent > s.busiest_thread_percent) {
                                    s.busiest_thread_percent = percent;
                                    s.busiest_thread_id = entry.th32ThreadID;
                                    s.busiest_thread_name = ThreadName(thread);
                                }
                            }
                        }
                    }
                    CloseHandle(thread);
                } while (Thread32Next(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
        last_thread_ticks = std::move(current_thread_ticks);
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
        {
            std::scoped_lock lock{mutex};
            latest = std::move(s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
    return latest;
}

} // namespace Ps4RtPerfStats
