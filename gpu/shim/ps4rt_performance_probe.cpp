// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4rt_performance_probe.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <x86intrin.h>

#include "ps4rt_perf_stats.h"
#include "ps4rt_settings.h"
#include "ps4rt_shader_capture.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

namespace Vulkan {
extern std::atomic<u64> g_ps4rt_compile_ns;
extern std::atomic<u32> g_ps4rt_compiles;
extern std::atomic<u64> g_ps4rt_capture_ns, g_ps4rt_async_ns, g_ps4rt_compile_wait_ns;
extern std::atomic<u64> g_ps4rt_snapshot_ns, g_ps4rt_guest_wait_ns;
extern std::atomic<u32> g_ps4rt_pending_draws;
} // namespace Vulkan

namespace Ps4RtPerformanceProbe {
namespace {

using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

struct Frame {
    std::uint64_t index{};
    double elapsed_ms{};
    double frame_ms{};
    double present_cpu_ms{};
    double gpu_wait_ms{};
    double host_wait_ms{};
    double present_wait_ms{};
    std::uint32_t compiles{};
    double compile_ms{};
    double shader_capture_ms{};
    double shader_async_compile_ms{};
    double compile_wait_ms{};
    double shader_guest_wait_ms{};
    double shader_snapshot_ms{};
    std::uint32_t skipped_draws{};
    std::uint32_t skipped_draws_total{};
    std::uint64_t draw_packets{};
    std::uint64_t draw_drain_requests{};
    std::uint64_t draw_drain_waits{};
    double draw_busy_ms{};
    double draw_drain_wait_ms{};
    std::uint64_t draw_queue_bytes{};
    std::uint64_t draw_queue_high_water_bytes{};
    std::uint64_t drain_pm4{};
    std::uint64_t drain_commands{};
    std::uint64_t drain_compute{};
    std::uint64_t drain_submission_end{};
    std::uint64_t drain_draw{};
    std::uint64_t drain_rasterizer{};
    float cpu_percent{-1.0f};
    float busiest_thread_percent{-1.0f};
    std::uint32_t busiest_thread_id{};
    std::string busiest_thread_name;
    std::uint32_t thread_count{};
    float gpu_percent{-1.0f};
    double ram_gb{-1.0};
    double vram_gb{-1.0};
};

std::atomic_bool active{false};
std::atomic_bool finished{false};
std::atomic<std::uint64_t> gpu_wait_ns{0}, host_wait_ns{0}, present_wait_ns{0};
std::mutex mutex;
std::vector<Frame> frames;
fs::path directory;
std::string stem;
std::string gpu_name;
const Vulkan::Rasterizer* capture_rasterizer{};
Clock::time_point started;
Clock::time_point previous_frame;
std::uint64_t started_tsc{};
std::uint64_t previous_tsc{};
std::uint64_t finished_tsc{};
Vulkan::DrawPipe::Telemetry pipeline_started{};
Vulkan::DrawPipe::Telemetry pipeline_finished{};
Vulkan::DrawPipe::Telemetry previous_pipeline{};
Vulkan::DrawPipe::Telemetry previous_log_pipeline{};
Clock::time_point previous_log_time;
std::uint64_t previous_log_tsc{};
std::uint64_t previous_compile_ns{};
std::uint64_t previous_capture_ns{};
std::uint64_t previous_async_ns{};
std::uint64_t previous_compile_wait_ns{};
std::uint64_t previous_guest_wait_ns{};
std::uint64_t previous_snapshot_ns{};
std::uint32_t previous_compiles{};
std::uint32_t previous_skipped_draws{};
Ps4RtShaderCapture::Statistics capture_started{};

double Milliseconds(std::uint64_t nanoseconds) {
    return double(nanoseconds) / 1'000'000.0;
}

std::uint64_t Delta(std::uint64_t end, std::uint64_t begin) {
    return end >= begin ? end - begin : 0;
}

double CyclesToMilliseconds(std::uint64_t cycles, std::uint64_t interval_cycles,
                            double interval_ms) {
    return interval_cycles ? double(cycles) * interval_ms / double(interval_cycles) : 0.0;
}

std::uint64_t ReasonDelta(const Vulkan::DrawPipe::Telemetry& end,
                          const Vulkan::DrawPipe::Telemetry& begin, std::uint32_t reason) {
    return Delta(end.waits_by_reason[reason], begin.waits_by_reason[reason]);
}

std::uint64_t Pm4DrainDelta(const Vulkan::DrawPipe::Telemetry& end,
                            const Vulkan::DrawPipe::Telemetry& begin) {
    return Delta(end.pm4_waits, begin.pm4_waits);
}

std::string DrainReasonName(std::uint32_t reason) {
    switch (reason) {
    case Vulkan::DrawPipe::ReasonConstRam:
        return "constant_ram";
    case Vulkan::DrawPipe::ReasonCommands:
        return "commands";
    case Vulkan::DrawPipe::ReasonCompute:
        return "compute";
    case Vulkan::DrawPipe::ReasonSubmissionEnd:
        return "submission_end";
    case Vulkan::DrawPipe::ReasonDraw:
        return "draw";
    case Vulkan::DrawPipe::ReasonRasterizer:
        return "rasterizer";
    default: {
        char name[24];
        std::snprintf(name, sizeof(name), "pm4_opcode_0x%02X", reason);
        return name;
    }
    }
}

const char* DrawPhaseName(std::uint32_t phase) {
    switch (phase) {
    case Vulkan::DrawPipe::PhasePacketDecode:
        return "packet_decode";
    case Vulkan::DrawPipe::PhaseOrderedTask:
        return "ordered_task";
    case Vulkan::DrawPipe::PhaseDirectDraw:
        return "direct_draw_total";
    case Vulkan::DrawPipe::PhaseIndirectDraw:
        return "indirect_draw_total";
    case Vulkan::DrawPipe::PhaseDispatch:
        return "dispatch_total";
    case Vulkan::DrawPipe::PhaseSpecialDraw:
        return "special_draw_total";
    case Vulkan::DrawPipe::PhaseRenderState:
        return "render_state";
    case Vulkan::DrawPipe::PhaseBindResources:
        return "bind_resources";
    case Vulkan::DrawPipe::PhaseRenderSetup:
        return "render_setup";
    case Vulkan::DrawPipe::PhaseDrawEmit:
        return "draw_emit";
    case Vulkan::DrawPipe::PhaseBindBuffers:
        return "bind_buffers";
    case Vulkan::DrawPipe::PhaseBindTextures:
        return "bind_textures";
    default:
        return "unknown";
    }
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &raw);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if (c >= 0x20) out.push_back(char(c));
    }
    return out;
}

std::string CsvEscape(const std::string& value) {
    std::string out{"\""};
    for (const char c : value) {
        if (c == '\"') out.push_back('\"');
        out.push_back(c);
    }
    out.push_back('\"');
    return out;
}

double Percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    const double position = fraction * double(sorted.size() - 1);
    const size_t lower = size_t(position);
    const size_t upper = std::min(sorted.size() - 1, lower + 1);
    const double mix = position - double(lower);
    return sorted[lower] * (1.0 - mix) + sorted[upper] * mix;
}

double LowFps(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    const size_t count = std::max<size_t>(1, size_t(std::ceil(double(sorted.size()) * fraction)));
    const double mean = std::accumulate(sorted.end() - count, sorted.end(), 0.0) / double(count);
    return mean > 0.0 ? 1000.0 / mean : 0.0;
}

void WriteReports(const std::vector<Frame>& captured) {
    if (captured.empty() || directory.empty()) return;
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        std::printf("Performance capture ERROR: cannot create report directory\n");
        return;
    }

    const fs::path csv_path = directory / (stem + ".csv");
    std::ofstream csv(csv_path, std::ios::trunc);
    csv << "frame,elapsed_ms,frame_ms,present_cpu_ms,gpu_wait_ms,host_wait_ms,present_wait_ms,"
           "shader_compiles,shader_compile_ms,shader_capture_ms,shader_async_compile_ms,"
           "shader_wait_ms,shader_guest_wait_ms,shader_snapshot_ms,skipped_draws,"
           "skipped_draws_total,draw_packets,draw_drain_requests,draw_drain_waits,"
           "draw_busy_ms,draw_drain_wait_ms,draw_queue_bytes,draw_queue_high_water_bytes,"
           "drain_pm4,drain_commands,drain_compute,drain_submission_end,drain_draw,"
           "drain_rasterizer,cpu_percent,busiest_thread_percent,busiest_thread_id,"
           "busiest_thread_name,thread_count,gpu_percent,"
           "ram_gb,vram_gb\n";
    csv << std::fixed << std::setprecision(3);
    for (const Frame& f : captured) {
        csv << f.index << ',' << f.elapsed_ms << ',' << f.frame_ms << ',' << f.present_cpu_ms << ','
            << f.gpu_wait_ms << ',' << f.host_wait_ms << ',' << f.present_wait_ms << ','
            << f.compiles << ',' << f.compile_ms << ',' << f.shader_capture_ms << ','
            << f.shader_async_compile_ms << ',' << f.compile_wait_ms << ','
            << f.shader_guest_wait_ms << ',' << f.shader_snapshot_ms << ',' << f.skipped_draws
            << ',' << f.skipped_draws_total << ',' << f.draw_packets << ','
            << f.draw_drain_requests << ',' << f.draw_drain_waits << ',' << f.draw_busy_ms << ','
            << f.draw_drain_wait_ms << ',' << f.draw_queue_bytes << ','
            << f.draw_queue_high_water_bytes << ',' << f.drain_pm4 << ',' << f.drain_commands
            << ',' << f.drain_compute << ',' << f.drain_submission_end << ',' << f.drain_draw
            << ',' << f.drain_rasterizer << ',' << f.cpu_percent << ','
            << f.busiest_thread_percent << ',' << f.busiest_thread_id << ','
            << CsvEscape(f.busiest_thread_name) << ',' << f.thread_count << ',' << f.gpu_percent << ','
            << f.ram_gb << ',' << f.vram_gb << '\n';
    }
    csv.close();

    std::vector<double> frame_times;
    frame_times.reserve(captured.size());
    double sum = 0.0, gpu_wait = 0.0, host_wait = 0.0, present_wait = 0.0;
    double compile = 0.0, capture = 0.0, async_compile = 0.0, shader_wait = 0.0;
    double guest_wait = 0.0, snapshot = 0.0;
    double cpu_sum = 0.0, thread_sum = 0.0, gpu_sum = 0.0;
    double cpu_max = 0.0, thread_max = 0.0, gpu_max = 0.0;
    std::uint64_t compiles = 0, skipped_draws = 0, over25 = 0, over33 = 0, over50 = 0;
    std::uint64_t cpu_samples = 0, thread_samples = 0, gpu_samples = 0;
    struct ThreadAggregate {
        std::uint64_t samples{};
        double total_percent{};
        double max_percent{};
    };
    std::unordered_map<std::string, ThreadAggregate> busiest_threads;
    for (const Frame& f : captured) {
        if (f.frame_ms > 0.0) {
            frame_times.push_back(f.frame_ms);
            sum += f.frame_ms;
            over25 += f.frame_ms > 25.0;
            over33 += f.frame_ms > 33.333;
            over50 += f.frame_ms > 50.0;
        }
        gpu_wait += f.gpu_wait_ms;
        host_wait += f.host_wait_ms;
        present_wait += f.present_wait_ms;
        compiles += f.compiles;
        compile += f.compile_ms;
        capture += f.shader_capture_ms;
        async_compile += f.shader_async_compile_ms;
        shader_wait += f.compile_wait_ms;
        guest_wait += f.shader_guest_wait_ms;
        snapshot += f.shader_snapshot_ms;
        skipped_draws += f.skipped_draws;
        if (f.cpu_percent >= 0.0f) { cpu_sum += f.cpu_percent; cpu_max = std::max(cpu_max, double(f.cpu_percent)); ++cpu_samples; }
        if (f.busiest_thread_percent >= 0.0f) {
            thread_sum += f.busiest_thread_percent;
            thread_max = std::max(thread_max, double(f.busiest_thread_percent));
            ++thread_samples;
            const std::string name = f.busiest_thread_name.empty() ? "<unnamed>"
                                                                   : f.busiest_thread_name;
            auto& aggregate = busiest_threads[name];
            ++aggregate.samples;
            aggregate.total_percent += f.busiest_thread_percent;
            aggregate.max_percent = std::max(aggregate.max_percent,
                                             double(f.busiest_thread_percent));
        }
        if (f.gpu_percent >= 0.0f) { gpu_sum += f.gpu_percent; gpu_max = std::max(gpu_max, double(f.gpu_percent)); ++gpu_samples; }
    }
    std::sort(frame_times.begin(), frame_times.end());
    const double mean = frame_times.empty() ? 0.0 : sum / double(frame_times.size());
    const auto& settings = Ps4RtSettings::Get();
    const int output = std::clamp(settings.output_res.load(), 0, Ps4RtSettings::OutputCount - 1);
    const char* version = std::getenv("PS4RT_BUILD_VERSION");
    const auto capture_finished = Ps4RtShaderCapture::GetStatistics();
    const double duration_ms = captured.back().elapsed_ms;
    const std::uint64_t capture_cycles = Delta(finished_tsc, started_tsc);
    const std::uint64_t draw_packets = Delta(pipeline_finished.packets, pipeline_started.packets);
    const std::uint64_t drain_requests =
        Delta(pipeline_finished.drain_requests, pipeline_started.drain_requests);
    const std::uint64_t drain_waits =
        Delta(pipeline_finished.drain_waits, pipeline_started.drain_waits);
    const double draw_busy_ms = CyclesToMilliseconds(
        Delta(pipeline_finished.busy_cycles, pipeline_started.busy_cycles), capture_cycles,
        duration_ms);
    const double drain_wait_ms = CyclesToMilliseconds(
        Delta(pipeline_finished.drain_cycles, pipeline_started.drain_cycles), capture_cycles,
        duration_ms);
    struct DrainReasonAggregate {
        std::uint32_t reason{};
        std::uint64_t requests{};
        std::uint64_t waits{};
        std::uint64_t cycles{};
    };
    std::vector<DrainReasonAggregate> drain_reasons;
    for (std::uint32_t reason = 0; reason < Vulkan::DrawPipe::NumReasons; ++reason) {
        DrainReasonAggregate aggregate{
            .reason = reason,
            .requests = Delta(pipeline_finished.requests_by_reason[reason],
                              pipeline_started.requests_by_reason[reason]),
            .waits = Delta(pipeline_finished.waits_by_reason[reason],
                           pipeline_started.waits_by_reason[reason]),
            .cycles = Delta(pipeline_finished.cycles_by_reason[reason],
                            pipeline_started.cycles_by_reason[reason]),
        };
        if (aggregate.requests || aggregate.waits || aggregate.cycles) {
            drain_reasons.push_back(aggregate);
        }
    }
    std::sort(drain_reasons.begin(), drain_reasons.end(), [](const auto& left, const auto& right) {
        if (left.cycles != right.cycles) return left.cycles > right.cycles;
        return left.waits > right.waits;
    });
    const fs::path json_path = directory / (stem + ".json");
    std::ofstream json(json_path, std::ios::trunc);
    json << std::fixed << std::setprecision(3)
         << "{\n"
         << "  \"format\": \"PS4RT performance capture 6\",\n"
         << "  \"runtime_revision\": \"windows-binding-serial-quiet\",\n"
         << "  \"version\": \"" << JsonEscape(version ? version : "unknown") << "\",\n"
         << "  \"gpu\": \"" << JsonEscape(gpu_name) << "\",\n"
         << "  \"upscaler\": \"" << Ps4RtSettings::UpscalerName(settings.upscaler.load()) << "\",\n"
         << "  \"output_resolution\": \"" << Ps4RtSettings::OutputWidths[output] << 'x'
         << Ps4RtSettings::OutputHeights[output] << "\",\n"
         << "  \"vsync\": " << (settings.vsync.load() ? "true" : "false") << ",\n"
         << "  \"fps_limit\": " << settings.fps_limit.load() << ",\n"
         << "  \"async_shaders\": " << (settings.async_shaders.load() ? "true" : "false") << ",\n"
         << "  \"frames_queued\": " << settings.frames_queued.load() << ",\n"
         << "  \"frames\": " << captured.size() << ",\n"
         << "  \"duration_seconds\": " << captured.back().elapsed_ms / 1000.0 << ",\n"
         << "  \"average_fps\": " << (mean > 0.0 ? 1000.0 / mean : 0.0) << ",\n"
         << "  \"one_percent_low_fps\": " << LowFps(frame_times, 0.01) << ",\n"
         << "  \"point_one_percent_low_fps\": " << LowFps(frame_times, 0.001) << ",\n"
         << "  \"frame_time_ms\": {\"mean\": " << mean << ", \"p50\": "
         << Percentile(frame_times, 0.50) << ", \"p95\": " << Percentile(frame_times, 0.95)
         << ", \"p99\": " << Percentile(frame_times, 0.99) << "},\n"
         << "  \"stutter_frames\": {\"over_25_ms\": " << over25 << ", \"over_33_3_ms\": "
         << over33 << ", \"over_50_ms\": " << over50 << "},\n"
         << "  \"usage_percent\": {\"cpu_mean\": " << (cpu_samples ? cpu_sum / cpu_samples : -1.0)
         << ", \"cpu_max\": " << cpu_max << ", \"busiest_thread_mean\": "
         << (thread_samples ? thread_sum / thread_samples : -1.0) << ", \"busiest_thread_max\": "
         << thread_max << ", \"gpu_mean\": " << (gpu_samples ? gpu_sum / gpu_samples : -1.0)
         << ", \"gpu_max\": " << gpu_max << "},\n"
         << "  \"totals_ms\": {\"gpu_wait\": " << gpu_wait << ", \"host_wait\": "
         << host_wait << ", \"present_wait\": " << present_wait << ", \"shader_compile\": "
         << compile << ", \"shader_capture\": " << capture << ", \"shader_async_compile\": "
         << async_compile << ", \"shader_wait\": " << shader_wait << ", \"shader_guest_wait\": "
         << guest_wait << ", \"shader_snapshot\": " << snapshot << "},\n"
         << "  \"shader_compiles\": " << compiles << ",\n"
         << "  \"shader_context_capture\": {\"queued\": "
         << Delta(capture_finished.queued, capture_started.queued) << ", \"bytes\": "
         << Delta(capture_finished.bytes, capture_started.bytes) << ", \"fetch_shader\": "
         << Delta(capture_finished.fetch_shader, capture_started.fetch_shader)
         << ", \"geometry_copy_shader\": "
         << Delta(capture_finished.geometry_copy_shader,
                  capture_started.geometry_copy_shader)
         << ", \"failures\": " << Delta(capture_finished.failures, capture_started.failures)
         << "},\n"
         << "  \"skipped_draws\": " << skipped_draws << ",\n"
         << "  \"draw_pipeline\": {\"enabled\": "
         << (pipeline_finished.enabled ? "true" : "false") << ", \"packets\": "
         << draw_packets << ", \"drain_requests\": " << drain_requests
         << ", \"drain_waits\": " << drain_waits << ", \"drawrec_busy_ms\": "
         << draw_busy_ms << ", \"drain_wait_ms\": " << drain_wait_ms
         << ", \"queue_bytes_at_finish\": " << pipeline_finished.queue_bytes
         << ", \"queue_high_water_bytes\": " << pipeline_finished.queue_high_water_bytes
         << ",\n    \"top_drain_reasons\": [";
    for (size_t i = 0; i < std::min<size_t>(drain_reasons.size(), 16); ++i) {
        const auto& reason = drain_reasons[i];
        json << (i ? "," : "") << "\n      {\"name\": \""
             << DrainReasonName(reason.reason) << "\", \"requests\": " << reason.requests
             << ", \"waits\": " << reason.waits << ", \"wait_ms\": "
             << CyclesToMilliseconds(reason.cycles, capture_cycles, duration_ms) << "}";
    }
    if (!drain_reasons.empty()) json << '\n';
    json << "    ],\n    \"texture_bind_helper\": "
         << (capture_rasterizer && capture_rasterizer->TextureBindHelperEnabled() ? "true"
                                                                                  : "false")
         << ",\n    \"recording_phases\": [";
    bool wrote_phase = false;
    for (std::uint32_t phase = 0; phase < Vulkan::DrawPipe::NumPhases; ++phase) {
        const std::uint64_t calls = Delta(pipeline_finished.phase_calls[phase],
                                          pipeline_started.phase_calls[phase]);
        const std::uint64_t cycles = Delta(pipeline_finished.phase_cycles[phase],
                                           pipeline_started.phase_cycles[phase]);
        if (!calls && !cycles) {
            continue;
        }
        const double milliseconds = CyclesToMilliseconds(cycles, capture_cycles, duration_ms);
        json << (wrote_phase ? "," : "") << "\n      {\"name\": \""
             << DrawPhaseName(phase) << "\", \"calls\": " << calls << ", \"total_ms\": "
             << milliseconds << ", \"mean_us\": "
             << (calls ? milliseconds * 1000.0 / double(calls) : 0.0) << "}";
        wrote_phase = true;
    }
    if (wrote_phase) json << '\n';
    json << "    ]\n  },\n"
         << "  \"busiest_threads\": [";
    std::vector<std::pair<std::string, ThreadAggregate>> ranked_threads(busiest_threads.begin(),
                                                                        busiest_threads.end());
    std::sort(ranked_threads.begin(), ranked_threads.end(), [](const auto& left, const auto& right) {
        return left.second.total_percent > right.second.total_percent;
    });
    for (size_t i = 0; i < std::min<size_t>(ranked_threads.size(), 12); ++i) {
        const auto& [name, aggregate] = ranked_threads[i];
        json << (i ? "," : "") << "\n    {\"name\": \"" << JsonEscape(name)
             << "\", \"samples\": " << aggregate.samples << ", \"mean_percent\": "
             << aggregate.total_percent / double(aggregate.samples) << ", \"max_percent\": "
             << aggregate.max_percent << "}";
    }
    if (!ranked_threads.empty()) json << '\n';
    json << "  ]\n}\n";
    json.close();
    std::printf("Performance capture written: %s.json and %s.csv\n", stem.c_str(), stem.c_str());
}

} // namespace

void Start(const Vulkan::Instance& instance, const Vulkan::Rasterizer& rasterizer) {
    const wchar_t* requested = _wgetenv(L"PS4RT_PERF_CAPTURE_DIR");
    if (!requested || !requested[0] || active.load(std::memory_order_acquire)) return;
    directory = fs::path(requested);
    stem = "performance-" + Timestamp();
    gpu_name = std::string(instance.GetModelName());
    capture_rasterizer = &rasterizer;
    started = previous_frame = Clock::now();
    previous_log_time = started;
    started_tsc = previous_tsc = previous_log_tsc = __rdtsc();
    pipeline_started = rasterizer.DrawPipelineTelemetry(true);
    previous_pipeline = previous_log_pipeline = pipeline_started;
    previous_compile_ns = Vulkan::g_ps4rt_compile_ns.load(std::memory_order_relaxed);
    previous_capture_ns = Vulkan::g_ps4rt_capture_ns.load(std::memory_order_relaxed);
    previous_async_ns = Vulkan::g_ps4rt_async_ns.load(std::memory_order_relaxed);
    previous_compile_wait_ns = Vulkan::g_ps4rt_compile_wait_ns.load(std::memory_order_relaxed);
    previous_guest_wait_ns = Vulkan::g_ps4rt_guest_wait_ns.load(std::memory_order_relaxed);
    previous_snapshot_ns = Vulkan::g_ps4rt_snapshot_ns.load(std::memory_order_relaxed);
    previous_compiles = Vulkan::g_ps4rt_compiles.load(std::memory_order_relaxed);
    previous_skipped_draws = Vulkan::g_ps4rt_pending_draws.load(std::memory_order_relaxed);
    capture_started = Ps4RtShaderCapture::GetStatistics();
    frames.clear();
    frames.reserve(60 * 30 * 20); // up to about 20 minutes without reallocating at 60 FPS
    finished = false;
    active = true;
    Ps4RtPerfStats::Start(instance);
    std::printf("Performance capture started: %s\n", stem.c_str());
}

bool Active() {
    return active.load(std::memory_order_acquire);
}

void AddGpuWait(std::uint64_t nanoseconds) {
    if (Active()) gpu_wait_ns.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void AddHostWait(std::uint64_t nanoseconds) {
    if (Active()) host_wait_ns.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void AddPresentWait(std::uint64_t nanoseconds) {
    if (Active()) present_wait_ns.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void RecordFrame(std::uint64_t present_cpu_nanoseconds) {
    if (!Active()) return;
    const auto now = Clock::now();
    const std::uint64_t now_tsc = __rdtsc();
    const Vulkan::DrawPipe::Telemetry pipeline =
        capture_rasterizer ? capture_rasterizer->DrawPipelineTelemetry()
                           : Vulkan::DrawPipe::Telemetry{};
    const auto compile_ns = Vulkan::g_ps4rt_compile_ns.load(std::memory_order_relaxed);
    const auto capture_ns = Vulkan::g_ps4rt_capture_ns.load(std::memory_order_relaxed);
    const auto async_ns = Vulkan::g_ps4rt_async_ns.load(std::memory_order_relaxed);
    const auto compile_wait_ns = Vulkan::g_ps4rt_compile_wait_ns.load(std::memory_order_relaxed);
    const auto guest_wait_ns = Vulkan::g_ps4rt_guest_wait_ns.load(std::memory_order_relaxed);
    const auto snapshot_ns = Vulkan::g_ps4rt_snapshot_ns.load(std::memory_order_relaxed);
    const auto compiles = Vulkan::g_ps4rt_compiles.load(std::memory_order_relaxed);
    const auto skipped_draws = Vulkan::g_ps4rt_pending_draws.load(std::memory_order_relaxed);
    const Ps4RtPerfStats::Sample sample = Ps4RtPerfStats::Latest();
    Frame frame;
    {
        std::scoped_lock lock{mutex};
        frame.index = frames.size();
        if (frame.index == 0) started = previous_frame = now;
        frame.elapsed_ms = std::chrono::duration<double, std::milli>(now - started).count();
        frame.frame_ms = frame.index == 0
                             ? 0.0
                             : std::chrono::duration<double, std::milli>(now - previous_frame)
                                   .count();
        frame.present_cpu_ms = Milliseconds(present_cpu_nanoseconds);
        frame.gpu_wait_ms = Milliseconds(gpu_wait_ns.exchange(0, std::memory_order_relaxed));
        frame.host_wait_ms = Milliseconds(host_wait_ns.exchange(0, std::memory_order_relaxed));
        frame.present_wait_ms = Milliseconds(present_wait_ns.exchange(0, std::memory_order_relaxed));
        frame.compiles = compiles - previous_compiles;
        frame.compile_ms = Milliseconds(compile_ns - previous_compile_ns);
        frame.shader_capture_ms = Milliseconds(capture_ns - previous_capture_ns);
        frame.shader_async_compile_ms = Milliseconds(async_ns - previous_async_ns);
        frame.compile_wait_ms = Milliseconds(compile_wait_ns - previous_compile_wait_ns);
        frame.shader_guest_wait_ms = Milliseconds(guest_wait_ns - previous_guest_wait_ns);
        frame.shader_snapshot_ms = Milliseconds(snapshot_ns - previous_snapshot_ns);
        frame.skipped_draws = skipped_draws - previous_skipped_draws;
        frame.skipped_draws_total = skipped_draws;
        frame.draw_packets = Delta(pipeline.packets, previous_pipeline.packets);
        frame.draw_drain_requests =
            Delta(pipeline.drain_requests, previous_pipeline.drain_requests);
        frame.draw_drain_waits = Delta(pipeline.drain_waits, previous_pipeline.drain_waits);
        const std::uint64_t frame_cycles = Delta(now_tsc, previous_tsc);
        frame.draw_busy_ms = CyclesToMilliseconds(
            Delta(pipeline.busy_cycles, previous_pipeline.busy_cycles), frame_cycles,
            frame.frame_ms);
        frame.draw_drain_wait_ms = CyclesToMilliseconds(
            Delta(pipeline.drain_cycles, previous_pipeline.drain_cycles), frame_cycles,
            frame.frame_ms);
        frame.draw_queue_bytes = pipeline.queue_bytes;
        frame.draw_queue_high_water_bytes = pipeline.queue_high_water_bytes;
        frame.drain_pm4 = Pm4DrainDelta(pipeline, previous_pipeline);
        frame.drain_commands = ReasonDelta(pipeline, previous_pipeline,
                                            Vulkan::DrawPipe::ReasonCommands);
        frame.drain_compute =
            ReasonDelta(pipeline, previous_pipeline, Vulkan::DrawPipe::ReasonCompute);
        frame.drain_submission_end = ReasonDelta(
            pipeline, previous_pipeline, Vulkan::DrawPipe::ReasonSubmissionEnd);
        frame.drain_draw = ReasonDelta(pipeline, previous_pipeline, Vulkan::DrawPipe::ReasonDraw);
        frame.drain_rasterizer =
            ReasonDelta(pipeline, previous_pipeline, Vulkan::DrawPipe::ReasonRasterizer);
        frame.cpu_percent = sample.cpu_percent;
        frame.busiest_thread_percent = sample.busiest_thread_percent;
        frame.busiest_thread_id = sample.busiest_thread_id;
        frame.busiest_thread_name = sample.busiest_thread_name;
        frame.thread_count = sample.thread_count;
        frame.gpu_percent = sample.gpu_percent;
        frame.ram_gb = sample.ram_used_gb;
        frame.vram_gb = sample.vram_used_gb;
        frames.push_back(frame);
    }
    if (now - previous_log_time >= std::chrono::seconds(5)) {
        const double interval_ms =
            std::chrono::duration<double, std::milli>(now - previous_log_time).count();
        const std::uint64_t interval_cycles = Delta(now_tsc, previous_log_tsc);
        const double seconds = interval_ms / 1000.0;
        const double packet_rate =
            seconds > 0.0 ? double(Delta(pipeline.packets, previous_log_pipeline.packets)) / seconds
                          : 0.0;
        const double drain_rate = seconds > 0.0
                                      ? double(Delta(pipeline.drain_waits,
                                                     previous_log_pipeline.drain_waits)) /
                                            seconds
                                      : 0.0;
        const double busy_ms = CyclesToMilliseconds(
            Delta(pipeline.busy_cycles, previous_log_pipeline.busy_cycles), interval_cycles,
            interval_ms);
        const double wait_ms = CyclesToMilliseconds(
            Delta(pipeline.drain_cycles, previous_log_pipeline.drain_cycles), interval_cycles,
            interval_ms);
        std::printf("Draw pipeline: %.0f packets/s, %.1f drain waits/s, DrawRec %.1f ms, "
                    "drain wait %.1f ms, queue %.1f KiB (high %.1f KiB)\n",
                    packet_rate, drain_rate, busy_ms, wait_ms, double(pipeline.queue_bytes) / 1024.0,
                    double(pipeline.queue_high_water_bytes) / 1024.0);
        previous_log_time = now;
        previous_log_tsc = now_tsc;
        previous_log_pipeline = pipeline;
    }
    previous_frame = now;
    previous_tsc = now_tsc;
    previous_pipeline = pipeline;
    previous_compile_ns = compile_ns;
    previous_capture_ns = capture_ns;
    previous_async_ns = async_ns;
    previous_compile_wait_ns = compile_wait_ns;
    previous_guest_wait_ns = guest_wait_ns;
    previous_snapshot_ns = snapshot_ns;
    previous_compiles = compiles;
    previous_skipped_draws = skipped_draws;
}

void Finish() {
    if (finished.exchange(true) || !active.exchange(false)) return;
    finished_tsc = __rdtsc();
    pipeline_finished = capture_rasterizer ? capture_rasterizer->DrawPipelineTelemetry(true)
                                           : previous_pipeline;
    std::vector<Frame> captured;
    {
        std::scoped_lock lock{mutex};
        captured = frames;
    }
    WriteReports(captured);
}

} // namespace Ps4RtPerformanceProbe
