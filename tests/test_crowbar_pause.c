/* Crowbar pause (runtime_crowbar_pause.c): the guest clock never steps back across pauses and
 * resumes, and costs nothing when Crowbar did not start the game. */
#include "../src/runtime_crowbar_pause.c"
#include <assert.h>

int runtime_crowbar_unlock(int id) { (void)id; return 0; }
int ps4rtgpu_request_exit(void) { return 0; }

#define READS 200000
static int stop; /* __atomic */
static long toggles;               /* __atomic */
static long backwards[4];

static uint64_t guest_ns(void) { return runtime_pause_now_ns(); } /* runtime_kernel.c's clock */
static void *reader(void *arg) {
    intptr_t id = (intptr_t)arg;
    uint64_t last = guest_ns();
    /* At least READS reads, and on until the toggler has paused and resumed 2000 times. */
    for (long i = 0; i < READS || !__atomic_load_n(&stop, __ATOMIC_ACQUIRE); ++i) {
        uint64_t t = guest_ns();
        if (t < last) ++backwards[id];
        last = t;
    }
    return NULL;
}
static void *toggler(void *unused) {
    (void)unused;
    while (__atomic_load_n(&toggles, __ATOMIC_RELAXED) < 2000) {
        set_paused(1);
        struct timespec nap = {0, 200000};
        nanosleep(&nap, NULL);
        set_paused(0);
        __atomic_add_fetch(&toggles, 1, __ATOMIC_RELAXED);
        nanosleep(&nap, NULL);
    }
    return NULL;
}

int main(void) {
    uint64_t h0 = host_now_ns(), g = runtime_pause_now_ns(), h1 = host_now_ns();
    assert(g >= h0 && g <= h1 && "no Crowbar: the host clock");
    __atomic_store_n(&enabled, 1, __ATOMIC_RELAXED);
    pthread_t r[4], t;
    pthread_create(&t, NULL, toggler, NULL);
    for (intptr_t i = 0; i < 4; ++i) pthread_create(&r[i], NULL, reader, (void *)i);
    pthread_join(t, NULL);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < 4; ++i) pthread_join(r[i], NULL);
    long total = backwards[0] + backwards[1] + backwards[2] + backwards[3];
    printf("crowbar pause: 4 readers, %ld pauses, %ld backward steps\n", toggles, total);

    assert(total == 0);
    /* While paused the guest clock stands still. */
    set_paused(1);
    uint64_t a = guest_ns();
    struct timespec nap = {0, 20000000};
    nanosleep(&nap, NULL);
    uint64_t b = guest_ns();
    set_paused(0);
    assert(b == a);
    puts("crowbar pause: ok");
    return 0;
}
