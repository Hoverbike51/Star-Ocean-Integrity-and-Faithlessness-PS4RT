/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "diagnostic_trace.h"
#include <stdatomic.h>
#include <windows.h>

typedef struct {
    atomic_uint sequence, thread_id, phase, live;
    atomic_uint_fast64_t object, start, total_ns[SO5_DIAG_PHASES], calls[SO5_DIAG_PHASES];
} Slot;
static Slot slots[SO5_DIAG_THREADS];
static atomic_uint used, dropped;
static atomic_int enabled;
static atomic_uint_fast64_t totals[SO5_DIAG_PHASES];
static _Thread_local unsigned local_slot; /* zero: unregistered; capacity + 1: full */
static uint64_t frequency;
static uint64_t ticks(void) { LARGE_INTEGER v; QueryPerformanceCounter(&v); return (uint64_t)v.QuadPart; }
static uint64_t nanos(uint64_t value) {
    return frequency ? value / frequency * UINT64_C(1000000000) +
        value % frequency * UINT64_C(1000000000) / frequency : 0;
}
void so5_diag_enable(int on) {
    /* Called before instrumented capture work; disable does not reset live TLS slots. */
    if (on && !frequency) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); frequency=(uint64_t)f.QuadPart; }
    atomic_store_explicit(&enabled,on,memory_order_release);
}
So5DiagScope so5_diag_begin(unsigned phase, uintptr_t object) {
    So5DiagScope scope={0};
    if (!atomic_load_explicit(&enabled,memory_order_acquire) || !phase || phase>=SO5_DIAG_PHASES) return scope;
    if (!local_slot) {
        unsigned index=atomic_fetch_add_explicit(&used,1,memory_order_relaxed);
        if (index>=SO5_DIAG_THREADS) {
            local_slot=SO5_DIAG_THREADS+1;
            atomic_fetch_add_explicit(&dropped,1,memory_order_relaxed);
        } else {
            local_slot=index+1;
            atomic_store(&slots[index].thread_id,GetCurrentThreadId());
            atomic_store(&slots[index].live,1);
        }
    }
    if (local_slot>SO5_DIAG_THREADS) return scope;
    Slot *s=&slots[local_slot-1];
    scope.slot=local_slot; scope.phase=phase; scope.start=ticks();
    scope.previous_phase=atomic_load(&s->phase);
    scope.previous_start=atomic_load(&s->start);
    scope.previous_object=atomic_load(&s->object);
    atomic_fetch_add(&s->sequence,1);
    atomic_store(&s->object,object); atomic_store(&s->start,scope.start); atomic_store(&s->phase,phase);
    atomic_fetch_add(&s->sequence,1);
    return scope;
}
void so5_diag_end(So5DiagScope scope) {
    if (!scope.slot) return;
    Slot *s=&slots[scope.slot-1];
    uint64_t elapsed=nanos(ticks()-scope.start);
    atomic_fetch_add(&s->sequence,1);
    atomic_fetch_add(&s->total_ns[scope.phase],elapsed);
    atomic_fetch_add_explicit(&totals[scope.phase],elapsed,memory_order_relaxed);
    atomic_fetch_add(&s->calls[scope.phase],1);
    atomic_store(&s->phase,scope.previous_phase); atomic_store(&s->start,scope.previous_start);
    atomic_store(&s->object,scope.previous_object);
    atomic_fetch_add(&s->sequence,1);
}
void so5_diag_thread_exit(void) {
    if (local_slot && local_slot<=SO5_DIAG_THREADS) atomic_store(&slots[local_slot-1].live,0);
}
void so5_diag_totals(uint64_t out[SO5_DIAG_PHASES]) {
    for (unsigned p=0;p<SO5_DIAG_PHASES;++p) out[p]=atomic_load_explicit(&totals[p],memory_order_relaxed);
}
size_t so5_diag_snapshot(So5DiagThread *out,size_t capacity,unsigned *lost) {
    size_t count=0;
    unsigned n=atomic_load(&used); if (n>SO5_DIAG_THREADS) n=SO5_DIAG_THREADS;
    uint64_t now=ticks();
    for (unsigned i=0;i<n && count<capacity;++i) {
        Slot *s=&slots[i]; So5DiagThread v={0};
        /* Bounded retries: never hold up presentation for a hot/paused writer. */
        for (unsigned attempt=0;attempt<3;++attempt) {
            unsigned before=atomic_load(&s->sequence); if (before&1) continue;
            v.slot=i; v.thread_id=atomic_load(&s->thread_id); v.live=atomic_load(&s->live);
            v.phase=atomic_load(&s->phase); v.object=atomic_load(&s->object);
            uint64_t start=atomic_load(&s->start);
            v.age_ns=v.phase && now>=start ? nanos(now-start) : 0;
            for (unsigned p=0;p<SO5_DIAG_PHASES;++p) {
                v.total_ns[p]=atomic_load(&s->total_ns[p]); v.calls[p]=atomic_load(&s->calls[p]);
            }
            if (before==atomic_load(&s->sequence) && v.thread_id) { out[count++]=v; break; }
        }
    }
    if (lost) *lost=atomic_load(&dropped);
    return count;
}
const char *so5_diag_phase_name(unsigned phase) {
    static const char *const names[SO5_DIAG_PHASES]={"outside_instrumented_wait","mutex_lock","condition_wait","semaphore_wait","thread_join"};
    return phase<SO5_DIAG_PHASES ? names[phase] : "unknown";
}
