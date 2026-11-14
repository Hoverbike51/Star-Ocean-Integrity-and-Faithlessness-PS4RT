// SPDX-License-Identifier: GPL-2.0-or-later
// Include the production writer, discard unused renderer entry points at link time.
#include "../gpu/shim/ps4rt_performance_probe.cpp"
#include <cassert>
namespace Vulkan {
std::atomic<u64> g_ps4rt_compile_ns{},g_ps4rt_capture_ns{},g_ps4rt_async_ns{},g_ps4rt_compile_wait_ns{},
                 g_ps4rt_snapshot_ns{},g_ps4rt_guest_wait_ns{};
std::atomic<u32> g_ps4rt_compiles{},g_ps4rt_pending_draws{};
}
namespace Ps4RtPerfStats {
void Start(const Vulkan::Instance&) {}
Sample Latest() { return {}; }
}
namespace Ps4RtSettings {
Values& Get() { static Values settings; return settings; }
const char* UpscalerName(int) { return "Test provider"; }
}
namespace Ps4RtShaderCapture { Statistics GetStatistics() { return {}; } }
int main(int argc,char**argv) {
    assert(argc==2);
    using namespace Ps4RtPerformanceProbe;
    directory=argv[1]; stem="diagnostic-report-test";
    Frame a; a.index=0;
    Frame b; b.index=1; b.elapsed_ms=250; b.frame_ms=250; b.present_cpu_ms=2;
    b.outside_present_ms=248;
    b.timing.reconstruction_state=Ps4RtPerfStats::ReconstructionState(5,0);
    b.timing.output_width=3840; b.timing.output_height=2160;
    b.render_width=1920; b.render_height=1080;
    b.requested_provider=6; b.requested_preset=1;
    stall_windows.Observe(0,0,0); stall_windows.Observe(1,250,250);
    auto& snap=stall_snapshots[0]; snap.count=1;
    snap.threads[0].thread_id=123; snap.threads[0].phase=SO5_DIAG_CONDITION;
    snap.threads[0].calls[SO5_DIAG_CONDITION]=2;
    snap.usage.top_threads[0]={123,100,"Test \"thread\""};
    WriteReports({a,b});
    stem="diagnostic-empty-windows-test"; stall_windows={}; WriteReports({a});
}
