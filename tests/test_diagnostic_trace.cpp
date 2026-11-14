// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/diagnostic_trace.h"
#include "../gpu/shim/ps4rt_stall_windows.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    using namespace Ps4RtPerformanceProbe;
    using namespace std::chrono_literals;
    std::array<So5DiagThread,SO5_DIAG_THREADS> rows{};
    unsigned dropped=0;
    assert(!so5_diag_begin(SO5_DIAG_MUTEX,1).slot);
    assert(!so5_diag_snapshot(rows.data(),rows.size(),&dropped));
    so5_diag_enable(1);
    auto outer=so5_diag_begin(SO5_DIAG_MUTEX,123);
    auto inner=so5_diag_begin(SO5_DIAG_CONDITION,456);
    assert(outer.slot && inner.slot==outer.slot);
    auto n=so5_diag_snapshot(rows.data(),rows.size(),&dropped);
    assert(n==1 && rows[0].phase==SO5_DIAG_CONDITION && rows[0].object==456);
    so5_diag_end(inner);
    n=so5_diag_snapshot(rows.data(),rows.size(),&dropped);
    assert(n==1 && rows[0].phase==SO5_DIAG_MUTEX && rows[0].object==123);
    so5_diag_end(outer);
    std::atomic<bool> entered{},release{};
    std::atomic<unsigned> id{};
    std::thread waiting([&] {
        id=GetCurrentThreadId();
        auto scope=so5_diag_begin(SO5_DIAG_SEMAPHORE,789);
        entered=true;
        while (!release.load()) std::this_thread::sleep_for(1ms);
        so5_diag_end(scope); so5_diag_thread_exit();
    });
    while (!entered.load()) std::this_thread::yield();
    std::this_thread::sleep_for(15ms);
    n=so5_diag_snapshot(rows.data(),rows.size(),&dropped);
    bool found=false;
    for (std::size_t i=0;i<n;++i) if (rows[i].thread_id==id) {
        found=true; assert(rows[i].phase==SO5_DIAG_SEMAPHORE && rows[i].age_ns>=1000000);
    }
    assert(found); release=true; waiting.join();
    n=so5_diag_snapshot(rows.data(),rows.size(),&dropped);
    for (std::size_t i=0;i<n;++i) if (rows[i].thread_id==id)
        assert(!rows[i].live && rows[i].calls[SO5_DIAG_SEMAPHORE]==1 && rows[i].total_ns[SO5_DIAG_SEMAPHORE]>=1000000);
    uint64_t totals[SO5_DIAG_PHASES]{}; so5_diag_totals(totals);
    assert(totals[SO5_DIAG_SEMAPHORE]>=1000000);
    so5_diag_enable(0);
    assert(!so5_diag_begin(SO5_DIAG_JOIN,1).slot);
    uint64_t after[SO5_DIAG_PHASES]{}; so5_diag_totals(after);
    for (unsigned p=0;p<SO5_DIAG_PHASES;++p) assert(after[p]==totals[p]);
    so5_diag_enable(1);
    // Lifetime slots cannot alias when Windows reuses thread IDs.
    for (unsigned i=0;i<SO5_DIAG_THREADS+4;++i) {
        std::thread t([] { auto s=so5_diag_begin(SO5_DIAG_JOIN,1); so5_diag_end(s); so5_diag_thread_exit(); }); t.join();
    }
    assert(so5_diag_snapshot(rows.data(),rows.size(),&dropped)==SO5_DIAG_THREADS);
    assert(dropped>=4);
    so5_diag_enable(0);
    StallWindows windows;
    for (unsigned i=0;i<300;++i) windows.Observe(i,i*16.0,16.0);
    assert(windows.Observe(300,4800,100)==-1 && windows.count==0);
    assert(windows.Observe(301,4901,101)==0 && windows.count==1);
    assert(windows.windows[0].first==182 && windows.windows[0].trigger==301);
    assert(windows.Observe(302,5020,119)==0 && windows.count==1);
    assert(windows.windows[0].worst==302 && windows.windows[0].stalls==2);
    assert(windows.Observe(303,5036,16)==-1 && windows.windows[0].last==303);
    // The next stall is three seconds later: its two-second pre-context still
    // overlaps the previous two-second follow-up, so it must merge.
    assert(windows.Observe(304,8036,110)==-1 && windows.count==1);
    assert(windows.windows[0].stalls==3 && windows.windows[0].last==304);
    for (unsigned i=1;i<32;++i) assert(windows.Observe(303+i,10000+i*5000.0,150)==int(i));
    assert(windows.count==32);
    assert(windows.Observe(400,300000,500)==-1 && windows.dropped_stalls==1);
    windows={}; assert(!windows.count && !windows.dropped_stalls);
    std::puts("Diagnostic trace passed: disabled path, nested waits, live/completed wait, thread lifetime, bounded registry and stall windows.");
}
