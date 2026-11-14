// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise real Windows thread counters, late thread discovery and handle retirement.
#include "ps4rt_perf_windows.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

int main() {
    using namespace Ps4RtPerfStats;
    using namespace std::chrono_literals;
    Windows::ThreadUsage sampler;
    Sample first;
    sampler.Read(first);
    assert(first.busiest_thread_percent < 0.0f);
    const unsigned processors = Windows::LogicalProcessors();
    assert(processors > 0);
    std::atomic<bool> running{true};
    std::atomic<DWORD> busy_id{};
    std::thread worker([&] {
        SetThreadDescription(GetCurrentThread(), L"Overlay sampling test");
        busy_id = GetCurrentThreadId();
        while (running.load(std::memory_order_relaxed)) {
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
    });
    while (!busy_id.load()) std::this_thread::yield();
    // Created after the initial snapshot: the sampler must discover this thread later.
    bool found = false;
    double worst_read_ms = 0.0;
    float peak = -1.0f;
    for (int i = 0; i < 16; ++i) {
        std::this_thread::sleep_for(250ms);
        Sample sample;
        const auto before = Windows::Clock::now();
        sampler.Read(sample);
        worst_read_ms = std::max(worst_read_ms,
            std::chrono::duration<double, std::milli>(Windows::Clock::now() - before).count());
        if (sample.busiest_thread_id == busy_id && sample.busiest_thread_percent > 10.0f) {
            assert(sample.busiest_thread_name == "Overlay sampling test");
            assert(sample.top_threads[0].id == sample.busiest_thread_id);
            assert(sample.top_threads[0].percent == sample.busiest_thread_percent);
            assert(std::string(sample.top_threads[0].name) == sample.busiest_thread_name);
            for (std::size_t rank=1; rank<sample.top_threads.size(); ++rank)
                assert(sample.top_threads[rank-1].percent >= sample.top_threads[rank].percent);
            found = true;
            peak = std::max(peak, sample.busiest_thread_percent);
        }
    }
    running = false;
    worker.join();
    assert(found);
    Sample after_exit;
    sampler.Read(after_exit);
    assert(after_exit.busiest_thread_id != busy_id);
    DWORD before_handles = 0, after_handles = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &before_handles));
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(1050ms);
        Sample sample;
        sampler.Read(sample);
    }
    assert(GetProcessHandleCount(GetCurrentProcess(), &after_handles));
    assert(after_handles <= before_handles + 1);
    std::printf("Windows sampling passed: %u logical CPUs, busy thread %.1f%%, "
                "worst thread-sampling read %.3f ms, handles %lu -> %lu\n",
                processors, peak, worst_read_ms,
                static_cast<unsigned long>(before_handles), static_cast<unsigned long>(after_handles));
}
