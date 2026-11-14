/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SO5_DIAGNOSTIC_TRACE_H
#define SO5_DIAGNOSTIC_TRACE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SO5_DIAG_THREADS = 256, SO5_DIAG_PHASES = 5 };
enum { SO5_DIAG_NONE, SO5_DIAG_MUTEX, SO5_DIAG_CONDITION, SO5_DIAG_SEMAPHORE, SO5_DIAG_JOIN };
typedef struct {
    uint64_t start, previous_start, previous_object;
    unsigned slot, phase, previous_phase;
} So5DiagScope;
typedef struct {
    unsigned slot, thread_id, phase, live;
    uint64_t object, age_ns, total_ns[SO5_DIAG_PHASES], calls[SO5_DIAG_PHASES];
} So5DiagThread;
/* Set once at capture startup. Fixed storage, no allocation or logging on wait paths. */
void so5_diag_enable(int enabled);
So5DiagScope so5_diag_begin(unsigned phase, uintptr_t object);
void so5_diag_end(So5DiagScope scope);
void so5_diag_thread_exit(void);
void so5_diag_totals(uint64_t totals[SO5_DIAG_PHASES]);
size_t so5_diag_snapshot(So5DiagThread *out, size_t capacity, unsigned *dropped);
const char *so5_diag_phase_name(unsigned phase);
#ifdef __cplusplus
}
#endif
#endif
