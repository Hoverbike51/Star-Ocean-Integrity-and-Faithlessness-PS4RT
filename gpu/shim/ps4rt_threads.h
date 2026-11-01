// SPDX-License-Identifier: GPL-2.0-or-later
// ps4rt: helper thread sizing. Counts follow the hardware threads this process may run on
// (the Windows process affinity mask). Speculative helpers use THREAD_PRIORITY_IDLE
// to reduce competition with game threads; Windows remains responsible for scheduling.

#pragma once

#include <algorithm>
#include <thread>
#include <bit>
#include <windows.h>

namespace Ps4RtThreads {
/// Hardware threads available to the process (its affinity mask).
inline unsigned Available() {
    DWORD_PTR process = 0, system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process) {
        return std::max(1, std::popcount(static_cast<unsigned long long>(process)));
    }
    return std::max(1u, std::thread::hardware_concurrency());
}

/// Request the lowest regular Windows thread priority for speculative work.
inline void MakeBackground() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
}

} // namespace Ps4RtThreads
