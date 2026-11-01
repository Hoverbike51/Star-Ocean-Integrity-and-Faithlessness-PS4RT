// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ctime>
#include <string>
#include <thread>


#include "common/error.h"
#include "common/logging/log.h"
#include "common/thread.h"
#include "ntapi.h"
#include <windows.h>
#include "common/string_util.h"


namespace Common {


void SetCurrentThreadRealtime(const std::chrono::nanoseconds period_ns) {
    // The presentation thread drives the guest vblank clock. Leaving it at normal priority on
    // Windows lets ordinary worker threads delay several 16.7 ms slots at once, which appears as
    // severe frame-time spikes even while the GPU is mostly idle. HIGHEST is deliberately used
    // instead of TIME_CRITICAL so audio, input and the desktop cannot be starved.
    (void)period_ns;
    const HANDLE thread = GetCurrentThread();
    SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);

    // Do not let Windows apply EcoQoS/power throttling to the timing thread. This is especially
    // important on heterogeneous and multi-CCD processors where a background classification can
    // otherwise move the vblank clock onto an unsuitable logical processor.
    THREAD_POWER_THROTTLING_STATE power{
        .Version = THREAD_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED,
        .StateMask = 0,
    };
    SetThreadInformation(thread, ThreadPowerThrottling, &power, sizeof(power));
}



void SetCurrentThreadPriority(ThreadPriority new_priority) {
    auto handle = GetCurrentThread();
    int windows_priority = 0;
    switch (new_priority) {
    case ThreadPriority::Low:
        windows_priority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
    case ThreadPriority::Normal:
        windows_priority = THREAD_PRIORITY_NORMAL;
        break;
    case ThreadPriority::High:
        windows_priority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
    case ThreadPriority::VeryHigh:
        windows_priority = THREAD_PRIORITY_HIGHEST;
        break;
    case ThreadPriority::Critical:
        windows_priority = THREAD_PRIORITY_TIME_CRITICAL;
        break;
    default:
        windows_priority = THREAD_PRIORITY_NORMAL;
        break;
    }
    SetThreadPriority(handle, windows_priority);
}

bool AccurateSleep(const std::chrono::nanoseconds duration, std::chrono::nanoseconds* remaining,
                   const bool interruptible) {
    const auto begin_sleep = std::chrono::high_resolution_clock::now();

    if (duration.count() <= 0) {
        SwitchToThread();
        if (remaining) {
            *remaining = std::chrono::nanoseconds(0);
        }
        return true;
    }

    LARGE_INTEGER interval{
        .QuadPart = -std::max<LONGLONG>(1, (duration.count() + 99) / 100),
    };
    // Reuse one timer per thread. Creating and destroying a kernel object every vblank caused
    // avoidable scheduler traffic, and the legacy timer did not request Windows' high-resolution
    // wait path.
    thread_local HANDLE timer = [] {
        HANDLE value = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                               TIMER_ALL_ACCESS);
        if (!value) {
            value = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        return value;
    }();
    if (!timer || !SetWaitableTimer(timer, &interval, 0, nullptr, nullptr, FALSE)) {
        Sleep(static_cast<DWORD>((duration.count() + 999'999) / 1'000'000));
        if (remaining) {
            *remaining = std::chrono::nanoseconds(0);
        }
        return true;
    }
    const auto ret = WaitForSingleObjectEx(timer, INFINITE, interruptible);

    if (remaining) {
        const auto end_sleep = std::chrono::high_resolution_clock::now();
        const auto sleep_time = end_sleep - begin_sleep;
        *remaining = duration > sleep_time ? duration - sleep_time : std::chrono::nanoseconds(0);
    }
    return ret == WAIT_OBJECT_0;
}



// Sets the debugger-visible name of the current thread.
void SetCurrentThreadName(const char* name) {
    // ps4rt: guest thread names are kept by the C runtime.
    SetThreadDescription(GetCurrentThread(), UTF8ToUTF16W(name).data());
}

void SetThreadName(void* thread, const char* name) {
    SetThreadDescription(thread, UTF8ToUTF16W(name).data());
}


AccurateTimer::AccurateTimer(std::chrono::nanoseconds target_interval)
    : target_interval(target_interval) {}

void AccurateTimer::Start() {
    const auto begin_sleep = std::chrono::high_resolution_clock::now();
    if (total_wait.count() > 0) {
        AccurateSleep(total_wait, nullptr, false);
    }
    start_time = std::chrono::high_resolution_clock::now();
    total_wait -= std::chrono::duration_cast<std::chrono::nanoseconds>(start_time - begin_sleep);
}

void AccurateTimer::End() {
    auto now = std::chrono::high_resolution_clock::now();
    total_wait +=
        target_interval - std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time);
}

std::string GetCurrentThreadName() {
    PWSTR name{};
    if (FAILED(GetThreadDescription(GetCurrentThread(), &name)) || name == nullptr) {
        return "<unknown name>";
    }
    const auto result = Common::UTF16ToUTF8(name);
    LocalFree(name);
    return result;
}

} // namespace Common
