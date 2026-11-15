/* Pause from Crowbar's Guide menu (crowbar-sdk 1.2, SPEC section 4).
 *
 * When Crowbar starts the game it passes PS4RT_CONTROL_DIR. The runtime writes "0 ready" in
 * <dir>/state, then a watcher thread reads <dir>/request ten times a second:
 *   "<n> pause"  -> the game stops (answer "<n> paused"), or refuses while a movie plays
 *                   ("<n> refused": Crowbar then shows "Guide unavailable" instead of its menu);
 *   "<n> resume" -> the game starts again (answer "<n> running").
 * Stopping the game: the thread that presents frames waits in runtime_pause_gate() (called from
 * sceVideoOutSubmitFlip and sceGnmSubmitAndFlipCommandBuffers), audio output waits there too
 * (the device plays silence), and the guest's monotonic clocks stand still for the time paused,
 * so the game does not see the pause as a long frame. Without PS4RT_CONTROL_DIR nothing runs
 * and the clock reads no lock. */
#define _GNU_SOURCE
#include "runtime.h"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static pthread_mutex_t pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_cond = PTHREAD_COND_INITIALIZER;
static int paused; /* written under pause_lock, read lock-free by runtime_pause_gate */
/* Paused time, for the guest's clocks. Written under pause_lock inside a sequence lock, read
 * lock-free: the clock never takes a mutex. `enabled` stays 0 without Crowbar, and then the
 * clock costs one relaxed load. */
static int enabled;
static unsigned clock_seq;
static uint64_t input_epoch; /* completed pauses, read atomically by the input adapter */
static uint64_t paused_since_ns, paused_total_ns;
static uint64_t last_movie_frame_ns; /* read and written with __atomic builtins */
static char control_dir[1024];

static uint64_t host_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

/* CLOCK_MONOTONIC in ns, without the time the game spent paused (runtime_kernel.c's clocks).
 * The host clock is read inside the sequence lock, so the time and the pause state always
 * belong together: the result stands still while paused and never steps back. Lock-free; when
 * Crowbar did not start the game it is the host clock and one relaxed load. */
uint64_t runtime_pause_now_ns(void) {
    if (!__atomic_load_n(&enabled, __ATOMIC_RELAXED)) return host_now_ns();
    for (;;) {
        unsigned s1 = __atomic_load_n(&clock_seq, __ATOMIC_ACQUIRE);
        if (s1 & 1) continue;
        uint64_t now = host_now_ns();
        uint64_t total = __atomic_load_n(&paused_total_ns, __ATOMIC_RELAXED);
        uint64_t since = __atomic_load_n(&paused_since_ns, __ATOMIC_RELAXED);
        int on = __atomic_load_n(&paused, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (__atomic_load_n(&clock_seq, __ATOMIC_SEQ_CST) != s1) continue;
        return (on && now > since ? since : now) - total;
    }
}

/* Called by the threads that present frames and play sound: waits while the game is paused.
 * Returns 1 when it waited (audio then restarts its cadence), 0 at once otherwise. */
int runtime_pause_gate(void) {
    if (!__atomic_load_n(&paused, __ATOMIC_ACQUIRE)) return 0;
    pthread_mutex_lock(&pause_lock);
    int waited = 0;
    while (paused) { pthread_cond_wait(&pause_cond, &pause_lock); waited = 1; }
    pthread_mutex_unlock(&pause_lock);
    return waited;
}

/* The game is paused from Crowbar's Guide (read lock-free by the pad). */
int runtime_pause_active(void) { return __atomic_load_n(&paused, __ATOMIC_ACQUIRE); }
uint64_t runtime_pause_input_epoch(void) { return __atomic_load_n(&input_epoch, __ATOMIC_ACQUIRE); }

/* A movie frame was just queued (runtime_videodec.c): a pause now would cut the movie. */
void runtime_pause_movie_frame(void) { __atomic_store_n(&last_movie_frame_ns, host_now_ns(), __ATOMIC_RELEASE); }

/* Only the control watcher publishes replies (startup queues ready before it starts).
 * Keep the latest reply until publication succeeds. Retrying a reply never repeats
 * pause, unlock or quit side effects, and a newer request supersedes an old reply. */
static struct {
    long seq;
    char word[16];
    int pending, warned;
} state_reply;

static int publish_state(long seq, const char *word) {
    char path[1100], tmp[1110];
    snprintf(path, sizeof(path), "%s/state", control_dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *out = fopen(tmp, "wb");
    if (!out) return 0;
    int complete = fprintf(out, "%ld %s\n", seq, word) >= 0;
    if (fclose(out) != 0) complete = 0;
    if (!complete) { remove(tmp); return 0; }
#ifdef _WIN32
    /* Readers without FILE_SHARE_DELETE can briefly reject atomic replacement.
     * Retry for at most 100 ms on this control thread, within the Guide response
     * budget; never remove the last complete state or park game/audio threads. */
    const uint64_t started = host_now_ns();
    for (;;) {
        if (MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 1;
        const DWORD error = GetLastError();
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION &&
             error != ERROR_ACCESS_DENIED) || host_now_ns() - started >= 100000000ull) break;
        Sleep(5);
    }
#else
    if (rename(tmp, path) == 0) return 1;
#endif
    remove(tmp);
    return 0;
}

static int flush_state_reply(void) {
    if (!state_reply.pending) return 1;
    if (!publish_state(state_reply.seq, state_reply.word)) {
        if (!state_reply.warned) {
            fprintf(stderr, "Crowbar: state reply %ld pending; publication will be retried\n", state_reply.seq);
            state_reply.warned = 1;
        }
        return 0;
    }
    state_reply.pending = state_reply.warned = 0;
    return 1;
}

static int write_state(long seq, const char *word) {
    state_reply.seq = seq;
    snprintf(state_reply.word, sizeof(state_reply.word), "%s", word);
    state_reply.pending = 1;
    state_reply.warned = 0;
    return flush_state_reply();
}

/* Sequentially consistent, so a reader that still sees the old state read its host time before
 * the writer reads its own (the pause then never starts before a time already handed out). */
static void clock_write_begin(void) {
    __atomic_store_n(&clock_seq, clock_seq + 1, __ATOMIC_SEQ_CST);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static void clock_write_end(void) { __atomic_store_n(&clock_seq, clock_seq + 1, __ATOMIC_RELEASE); }

static void set_paused(int on) {
    pthread_mutex_lock(&pause_lock);
    if (on && !paused) {
        clock_write_begin(); /* the time is read inside: a reader's time is before or after it */
        __atomic_store_n(&paused_since_ns, host_now_ns(), __ATOMIC_RELAXED);
        __atomic_store_n(&paused, 1, __ATOMIC_RELAXED);
        clock_write_end();
    } else if (!on && paused) {
        clock_write_begin();
        __atomic_store_n(&paused_total_ns, paused_total_ns + (host_now_ns() - paused_since_ns), __ATOMIC_RELAXED);
        /* Publish before unpausing: a sampler that observes running can see the epoch. */
        __atomic_add_fetch(&input_epoch, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&paused, 0, __ATOMIC_RELEASE);
        clock_write_end();
        pthread_cond_broadcast(&pause_cond);
    }
    pthread_mutex_unlock(&pause_lock);
}

int runtime_crowbar_unlock(int id); /* runtime_services.c */
int ps4rtgpu_request_exit(void);    /* gpu/shim/ps4rtgpu.cpp */

/* The player quit from Crowbar: the same path as the menu's Exit to desktop (runtime settings
 * saved, window closed, reports and caches flushed, exit code 0). The game runs again first so
 * no thread stays parked in runtime_pause_gate(). Crowbar closes the process after 2 s if the
 * window does not close. */
static void runtime_crowbar_quit(void) {
    set_paused(0);
    if (ps4rtgpu_request_exit() != 0) fprintf(stderr, "Crowbar: quit request not queued\n");
}

static void *watch(void *unused) {
    (void)unused;
    long last = 0;
    char path[1100];
    snprintf(path, sizeof(path), "%s/request", control_dir);
    for (;;) {
        struct timespec nap = {0, 100000000};
        nanosleep(&nap, NULL);
        /* A consumed request may still have an unpublished reply. Retry even
         * when request is missing or its sequence number has not changed. */
        flush_state_reply();
        FILE *in = fopen(path, "rb");
        if (!in) continue;
        long seq = 0;
        char word[16] = {0};
        int arg = -1;
        int got = fscanf(in, "%ld %15s %d", &seq, word, &arg);
        fclose(in);
        if (got < 2 || seq <= last) continue;
        last = seq;
        if (!strcmp(word, "pause")) {
            uint64_t movie = __atomic_load_n(&last_movie_frame_ns, __ATOMIC_ACQUIRE);
            if (movie && host_now_ns() - movie < 300000000ull) { write_state(seq, "refused"); continue; }
            set_paused(1);
            write_state(seq, "paused");
        } else if (!strcmp(word, "resume")) {
            set_paused(0);
            write_state(seq, "running");
        } else if (!strcmp(word, "unlock")) {
            /* Crowbar's test profile: a trophy unlocked as if the game had (crowbar-sdk 1.3). */
            write_state(seq, runtime_crowbar_unlock(arg) == 0 ? "unlocked" : "refused");
        } else if (!strcmp(word, "quit")) {
            /* The player quit from Crowbar (crowbar-sdk 1.4): trophies are already on disk. */
            write_state(seq, "quitting");
            runtime_crowbar_quit();
        }
    }
    return NULL;
}

__attribute__((constructor)) static void start_pause_watch(void) {
    const char *dir = getenv("PS4RT_CONTROL_DIR");
    if (!dir || !*dir || strlen(dir) >= sizeof(control_dir)) return;
    memcpy(control_dir, dir, strlen(dir) + 1);
    __atomic_store_n(&enabled, 1, __ATOMIC_RELAXED);
    write_state(0, "ready");
    pthread_t t;
    if (pthread_create(&t, NULL, watch, NULL) == 0) pthread_detach(t);
}
