// SPDX-License-Identifier: GPL-2.0-or-later
// ps4rt: two-stage draw pipeline (docs/parallel_gpu.md, "Two-stage draw pipeline").
//
// The GPU command thread (stage A) decodes PM4, keeps the register file and selects pipelines;
// for a direct draw it writes a packet (the register blocks changed since the previous packet,
// the stages' user data, the draw parameters) into this ring and goes on decoding. The draw
// recording thread (stage B) owns the caches, the barriers and the command recording while
// packets are in flight: it applies each packet to its own copy of the registers and runs the
// rest of the draw. Anything else stage A does on that state waits for stage B to run dry
// (Drain) and then runs on stage A as before.

#pragma once

#include <cstdlib>
#include "ps4rt_threads.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <x86intrin.h>

#include "common/assert.h"
#include "common/thread.h"
#include "common/types.h"

namespace Vulkan {

class DrawPipe {
public:
    /// Runs a packet on stage B.
    using Handler = void (*)(void* context, const u8* packet, u32 size);

    DrawPipe(Handler handler_, void* context_) : handler{handler_}, context{context_} {
        const char* capture = std::getenv("PS4RT_PERF_CAPTURE_DIR");
        telemetry_enabled = capture && capture[0];
        ring = std::make_unique<u8[]>(Capacity);
        thread = std::jthread([this](std::stop_token stop) { Run(stop); });
    }

    ~DrawPipe() {
        Drain();
        thread.request_stop();
        published.fetch_add(0, std::memory_order_seq_cst);
        wake.fetch_add(1, std::memory_order_seq_cst);
        wake.notify_one();
    }

    /// True on the draw recording thread.
    [[nodiscard]] static bool OnStageB() noexcept {
        return on_stage_b;
    }

    [[nodiscard]] u32 StageBThreadId() const noexcept {
        return stage_b_tid.load(std::memory_order_acquire);
    }

    /// Stage A: space for a packet of `size` bytes (8-byte aligned), valid until Commit().
    u8* Begin(u32 size) {
        size = Align(size + sizeof(Header));
        ASSERT(size <= Capacity / 4);
        u64 at = head;
        const u64 offset = at % Capacity;
        if (offset + size > Capacity) {
            // Wrap: the rest of the ring is skipped (a header with zero size marks it).
            WaitForSpace(at, Capacity - offset);
            reinterpret_cast<Header*>(ring.get() + offset)->size = 0;
            at += Capacity - offset;
            head = at;
            published.store(at, std::memory_order_release);
        }
        WaitForSpace(at, size);
        pending_size = size;
        auto* header = reinterpret_cast<Header*>(ring.get() + at % Capacity);
        header->size = size;
        return reinterpret_cast<u8*>(header + 1);
    }

    /// Stage A: hands the packet from Begin() to stage B.
    void Commit(u32 payload_size) {
        reinterpret_cast<Header*>(ring.get() + head % Capacity)->payload = payload_size;
        head += pending_size;
        published.store(head, std::memory_order_seq_cst);
        ++packets;
        if (telemetry_enabled) {
            telemetry_packets.store(packets, std::memory_order_relaxed);
            const u64 queued = head - consumed.load(std::memory_order_acquire);
            telemetry_queue_bytes.store(queued, std::memory_order_relaxed);
            if (queued > queue_high_water) {
                queue_high_water = queued;
                telemetry_queue_high_water.store(queued, std::memory_order_relaxed);
            }
        }
        if (sleeping.load(std::memory_order_seq_cst)) {
            wake.fetch_add(1, std::memory_order_seq_cst);
            wake.notify_one();
        }
    }

    /// Why stage A drained (statistics): a PM4 opcode, or one of these.
    enum Reason : u32 {
        ReasonConstRam = 256,
        ReasonCommands,
        ReasonCompute,
        ReasonSubmissionEnd,
        ReasonDraw,
        ReasonRasterizer,
        NumReasons,
    };
    /// Stage-B work categories sampled only by an explicit performance capture. Some entries
    /// are nested (for example DirectDraw contains RenderState through DrawEmit).
    enum Phase : u32 {
        PhasePacketDecode,
        PhaseOrderedTask,
        PhaseDirectDraw,
        PhaseIndirectDraw,
        PhaseDispatch,
        PhaseSpecialDraw,
        PhaseRenderState,
        PhaseBindResources,
        PhaseRenderSetup,
        PhaseDrawEmit,
        PhaseBindBuffers,
        PhaseBindTextures,
        NumPhases,
    };
    struct Telemetry {
        bool enabled{};
        u64 packets{};
        u64 drain_requests{};
        u64 drain_waits{};
        u64 drain_cycles{};
        u64 busy_cycles{};
        u64 pm4_waits{};
        u64 queue_bytes{};
        u64 queue_high_water_bytes{};
        std::array<u64, NumReasons> requests_by_reason{};
        std::array<u64, NumReasons> waits_by_reason{};
        std::array<u64, NumReasons> cycles_by_reason{};
        std::array<u64, NumPhases> phase_calls{};
        std::array<u64, NumPhases> phase_cycles{};
    };
    std::array<u64, NumReasons> drains_by_reason{};
    std::array<u64, NumReasons> cycles_by_reason{};

    [[nodiscard]] Telemetry ReadTelemetry(bool detailed = false) const noexcept {
        Telemetry result{};
        result.enabled = telemetry_enabled;
        if (!telemetry_enabled) {
            return result;
        }
        result.packets = telemetry_packets.load(std::memory_order_relaxed);
        result.drain_requests = telemetry_drain_requests.load(std::memory_order_relaxed);
        result.drain_waits = telemetry_drain_waits.load(std::memory_order_relaxed);
        result.drain_cycles = telemetry_drain_cycles.load(std::memory_order_relaxed);
        result.busy_cycles = busy_cycles.load(std::memory_order_relaxed);
        result.pm4_waits = telemetry_pm4_waits.load(std::memory_order_relaxed);
        result.queue_bytes = telemetry_queue_bytes.load(std::memory_order_relaxed);
        result.queue_high_water_bytes =
            telemetry_queue_high_water.load(std::memory_order_relaxed);
        const u32 first = detailed ? 0 : ReasonConstRam;
        for (u32 i = first; i < NumReasons; ++i) {
            result.requests_by_reason[i] =
                telemetry_requests_by_reason[i].load(std::memory_order_relaxed);
            result.waits_by_reason[i] =
                telemetry_waits_by_reason[i].load(std::memory_order_relaxed);
            result.cycles_by_reason[i] =
                telemetry_cycles_by_reason[i].load(std::memory_order_relaxed);
        }
        if (detailed) {
            for (u32 i = 0; i < NumPhases; ++i) {
                result.phase_calls[i] = telemetry_phase_calls[i].load(std::memory_order_relaxed);
                result.phase_cycles[i] = telemetry_phase_cycles[i].load(std::memory_order_relaxed);
            }
        }
        return result;
    }

    [[nodiscard]] bool TelemetryEnabled() const noexcept {
        return telemetry_enabled;
    }

    void RecordPhase(Phase phase, u64 cycles) noexcept {
        if (!telemetry_enabled) {
            return;
        }
        telemetry_phase_calls[phase].fetch_add(1, std::memory_order_relaxed);
        telemetry_phase_cycles[phase].fetch_add(cycles, std::memory_order_relaxed);
    }

    /// Stage A: waits until stage B has run every committed packet.
    void Drain(u32 reason = ReasonRasterizer) {
        const u32 slot = reason < NumReasons ? reason : ReasonRasterizer;
        if (telemetry_enabled) {
            ++drain_requests;
            telemetry_drain_requests.store(drain_requests, std::memory_order_relaxed);
            telemetry_requests_by_reason[slot].store(++requests_by_reason[slot],
                                                     std::memory_order_relaxed);
        }
        const u64 target = head;
        if (consumed.load(std::memory_order_acquire) == target) {
            return;
        }
        ++drains;
        ++drains_by_reason[slot];
        const u64 start = __rdtsc();
        u64 seen = consumed.load(std::memory_order_acquire);
        u32 spins = 0;
        while (seen != target && spins < 4096) {
            __builtin_ia32_pause();
            ++spins;
            seen = consumed.load(std::memory_order_acquire);
        }
        if (seen != target) {
            // A repeated SwitchToThread loop consumed most of the GPU command thread in the
            // Windows CPU trace. Publish the exact position we need and let atomic::wait park
            // the producer until stage B reaches it. The recheck makes a completion racing
            // with this store safe without waking on every recorded draw.
            drain_target.store(target, std::memory_order_release);
            seen = consumed.load(std::memory_order_acquire);
            while (seen != target) {
                consumed.wait(seen, std::memory_order_acquire);
                seen = consumed.load(std::memory_order_acquire);
            }
            drain_target.store(0, std::memory_order_release);
        }
        const u64 waited = __rdtsc() - start;
        drain_cycles += waited;
        cycles_by_reason[slot] += waited;
        if (telemetry_enabled) {
            telemetry_drain_waits.store(drains, std::memory_order_relaxed);
            telemetry_drain_cycles.store(drain_cycles, std::memory_order_relaxed);
            telemetry_waits_by_reason[slot].store(drains_by_reason[slot],
                                                  std::memory_order_relaxed);
            telemetry_cycles_by_reason[slot].store(cycles_by_reason[slot],
                                                   std::memory_order_relaxed);
            if (slot < ReasonConstRam) {
                telemetry_pm4_waits.fetch_add(1, std::memory_order_relaxed);
            }
            telemetry_queue_bytes.store(0, std::memory_order_relaxed);
        }
    }

    /// Stage A: position after the last committed packet; Reached(position) once B ran it.
    [[nodiscard]] u64 Head() const noexcept {
        return head;
    }
    [[nodiscard]] bool Reached(u64 position) const noexcept {
        return consumed.load(std::memory_order_acquire) >= position;
    }

    [[nodiscard]] bool Idle() const noexcept {
        return consumed.load(std::memory_order_acquire) == head;
    }

    /// Statistics (stage A): packets, drains that had to wait, cycles waited, cycles stage B
    /// spent running packets.
    u64 packets = 0, drains = 0, drain_cycles = 0;
    std::atomic<u64> busy_cycles{0};

private:
    struct Header {
        u32 size;    ///< bytes to the next packet (0: wrap to the ring start)
        u32 payload; ///< bytes written by the producer
    };
    static constexpr u64 Capacity = 16ull << 20;

    static constexpr u32 Align(u32 size) {
        return (size + 63) & ~63u;
    }

    void WaitForSpace(u64 at, u64 size) {
        while (at + size - consumed.load(std::memory_order_acquire) > Capacity) {
            __builtin_ia32_pause();
        }
    }

    void Run(std::stop_token stop) {
        Common::SetCurrentThreadName("ps4rt:DrawRec");
        on_stage_b = true;
        stage_b_tid.store(static_cast<u32>(gettid()), std::memory_order_release);
        u64 at = 0;
        while (true) {
            // Packets follow each other within microseconds while a frame is decoded: spin,
            // and sleep only after a longer pause (between frames).
            u64 available = published.load(std::memory_order_acquire);
            if (available == at) {
                // With few hardware threads (Steam Deck: 8) a long spin takes time from the
                // game's own threads. PS4RT_PIPE_SPIN_US overrides.
                static const auto spin_time = std::chrono::microseconds([] {
                    if (const char* env = std::getenv("PS4RT_PIPE_SPIN_US")) {
                        return std::max(0, std::atoi(env));
                    }
                    return Ps4RtThreads::Available() >= 12 ? 200 : 50;
                }());
                const auto spin_until = std::chrono::steady_clock::now() + spin_time;
                for (u32 spins = 1; available == at; ++spins) {
                    if (stop.stop_requested()) {
                        return;
                    }
                    __builtin_ia32_pause();
                    if (!(spins & 255) && std::chrono::steady_clock::now() >= spin_until) {
                        const u32 seen = wake.load(std::memory_order_seq_cst);
                        sleeping.store(true, std::memory_order_seq_cst);
                        if (published.load(std::memory_order_seq_cst) == at &&
                            !stop.stop_requested()) {
                            wake.wait(seen, std::memory_order_seq_cst);
                        }
                        sleeping.store(false, std::memory_order_relaxed);
                    }
                    available = published.load(std::memory_order_acquire);
                }
            }
            const auto* header = reinterpret_cast<const Header*>(ring.get() + at % Capacity);
            if (header->size == 0) {
                at += Capacity - at % Capacity;
                consumed.store(at, std::memory_order_release);
                NotifyDrained(at);
                continue;
            }
            const u64 start = __rdtsc();
            handler(context, reinterpret_cast<const u8*>(header + 1), header->payload);
            busy_cycles.fetch_add(__rdtsc() - start, std::memory_order_relaxed);
            at += header->size;
            consumed.store(at, std::memory_order_release);
            if (telemetry_enabled) {
                const u64 available_now = published.load(std::memory_order_acquire);
                telemetry_queue_bytes.store(available_now >= at ? available_now - at : 0,
                                            std::memory_order_relaxed);
            }
            NotifyDrained(at);
        }
    }

    void NotifyDrained(u64 at) {
        const u64 target = drain_target.load(std::memory_order_acquire);
        if (target != 0 && at >= target) {
            consumed.notify_one();
        }
    }

    static inline thread_local bool on_stage_b = false;
    Handler handler;
    void* context;
    std::unique_ptr<u8[]> ring;
    u64 head = 0; ///< stage A's write position
    u32 pending_size = 0;
    alignas(64) std::atomic<u64> published{0};
    alignas(64) std::atomic<u64> consumed{0};
    std::atomic<u64> drain_target{0};
    bool telemetry_enabled = false;
    u64 drain_requests = 0;
    u64 queue_high_water = 0;
    std::array<u64, NumReasons> requests_by_reason{};
    std::atomic<u64> telemetry_packets{0};
    std::atomic<u64> telemetry_drain_requests{0};
    std::atomic<u64> telemetry_drain_waits{0};
    std::atomic<u64> telemetry_drain_cycles{0};
    std::atomic<u64> telemetry_pm4_waits{0};
    std::atomic<u64> telemetry_queue_bytes{0};
    std::atomic<u64> telemetry_queue_high_water{0};
    std::array<std::atomic<u64>, NumReasons> telemetry_requests_by_reason{};
    std::array<std::atomic<u64>, NumReasons> telemetry_waits_by_reason{};
    std::array<std::atomic<u64>, NumReasons> telemetry_cycles_by_reason{};
    std::array<std::atomic<u64>, NumPhases> telemetry_phase_calls{};
    std::array<std::atomic<u64>, NumPhases> telemetry_phase_cycles{};
    alignas(64) std::atomic<u32> wake{0};
    std::atomic<bool> sleeping{false};
    std::atomic<u32> stage_b_tid{0};
    std::jthread thread;
};

} // namespace Vulkan
