// SPDX-License-Identifier: GPL-2.0-or-later
// Windows CPU sampling helpers. No graphics device or game state is required.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <chrono>
#include <memory>
#include <thread>
#include <unordered_map>
#include "ps4rt_perf_stats.h"

namespace Ps4RtPerfStats::Windows {
using Clock = std::chrono::steady_clock;
inline uint64_t Ticks(const FILETIME& time) {
    return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
inline uint64_t Now() {
    return uint64_t(std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 10000000>>>(
        Clock::now().time_since_epoch()).count());
}
inline unsigned LogicalProcessors() {
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count ? unsigned(count) : std::max(1u, std::thread::hardware_concurrency());
}
inline std::string ThreadName(HANDLE thread) {
    PWSTR wide{};
    if (FAILED(GetThreadDescription(thread, &wide)) || !wide) return "<unnamed>";
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string result = bytes > 1 ? std::string(size_t(bytes), '\0') : std::string{"<unnamed>"};
    if (bytes > 1) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), bytes, nullptr, nullptr);
        result.pop_back();
    }
    LocalFree(wide);
    return result;
}

class ThreadUsage {
    struct Thread {
        HANDLE handle;
        CpuCounter counter;
        std::string name;
        explicit Thread(HANDLE value) : handle(value), name(ThreadName(value)) {}
        ~Thread() { CloseHandle(handle); }
    };
    const DWORD pid = GetCurrentProcessId();
    std::unordered_map<DWORD, std::unique_ptr<Thread>> threads;
    Clock::time_point refresh_at{};

    void Refresh() {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return; // Retain handles when enumeration fails.
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID != pid) continue;
                auto found = threads.find(entry.th32ThreadID);
                if (found != threads.end()) {
                    found->second->name = ThreadName(found->second->handle);
                    continue;
                }
                const HANDLE handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                                                 entry.th32ThreadID);
                if (!handle) continue;
                // A thread ID can be reused between the snapshot and OpenThread.
                if (GetProcessIdOfThread(handle) != pid) { CloseHandle(handle); continue; }
                threads.emplace(entry.th32ThreadID, std::make_unique<Thread>(handle));
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
public:
    void Read(Sample& sample) {
        // Remove dead handles before enumeration so reused IDs get a new baseline.
        for (auto it = threads.begin(); it != threads.end();) {
            FILETIME created{}, ended{}, kernel{}, user{};
            auto& thread = *it->second;
            if (!GetThreadTimes(thread.handle, &created, &ended, &kernel, &user) || Ticks(ended)) {
                it = threads.erase(it);
                continue;
            }
            ++sample.thread_count;
            const float percent = thread.counter.Read(Ticks(kernel) + Ticks(user), Now());
            if (percent >= 0.0f) {
                for (std::size_t rank=0; rank<sample.top_threads.size(); ++rank) {
                    if (percent <= sample.top_threads[rank].percent) continue;
                    for (std::size_t j=sample.top_threads.size()-1; j>rank; --j)
                        sample.top_threads[j] = std::move(sample.top_threads[j-1]);
                    sample.top_threads[rank].Assign(it->first, percent, thread.name);
                    break;
                }
            }
            if (percent > sample.busiest_thread_percent) {
                sample.busiest_thread_percent = percent;
                sample.busiest_thread_id = it->first;
                sample.busiest_thread_name = thread.name;
            }
            ++it;
        }
        const auto now = Clock::now();
        if (now >= refresh_at) {
            Refresh();
            refresh_at = now + std::chrono::seconds(2);
            // Newly discovered threads need a baseline, never their lifetime CPU total.
            for (auto& [id, thread] : threads) {
                if (thread->counter.valid) continue;
                FILETIME created{}, ended{}, kernel{}, user{};
                if (GetThreadTimes(thread->handle, &created, &ended, &kernel, &user) && !Ticks(ended)) {
                    thread->counter.Read(Ticks(kernel) + Ticks(user), Now());
                    ++sample.thread_count;
                }
            }
        }
    }
};
} // namespace Ps4RtPerfStats::Windows
