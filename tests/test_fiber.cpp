// SPDX-License-Identifier: GPL-2.0-or-later
// sceFiber contract tests against src/runtime_fiber.c, called the way guest code calls it:
// through the resolved exports as System V functions.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <xmmintrin.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#endif

#define ABI __attribute__((sysv_abi))
extern "C" {
struct RuntimeExport { const char *name; void *function; };
uintptr_t runtime_fiber_resolve(const char *name);
// The runtime maps NIDs to names (runtime.c); the test resolves by name.
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    return 0;
}
}

namespace {
constexpr int32_t FERR(int n) { return (int32_t)(UINT32_C(0x80590000) | (uint32_t)n); }
constexpr uint32_t OPT_MAGIC = 0xbb40e64d, NEW_VERSION = 0x3500000, OLD_VERSION = 0x3000000;
struct alignas(16) Fiber { unsigned char bytes[112]; };
typedef void (ABI *Entry)(uint64_t, uint64_t);
typedef int32_t (ABI *Initialize)(Fiber *, const char *, Entry, uint64_t, void *, uint64_t, const uint32_t *, uint32_t);
typedef int32_t (ABI *Finalize)(Fiber *);
typedef int32_t (ABI *GetSelf)(Fiber **);
typedef int32_t (ABI *Run)(Fiber *, uint64_t, uint64_t *);
typedef int32_t (ABI *Switch)(Fiber *, uint64_t, uint64_t *);
typedef int32_t (ABI *Return)(uint64_t, uint64_t *);
Initialize fiber_init;
Finalize fiber_finalize;
GetSelf fiber_self;
Run fiber_run;
Switch fiber_switch;
Return fiber_return;

int failures;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
// Inside a fiber a failed check cannot unwind; record it and keep going.

uint32_t state(Fiber *fiber) { uint32_t s; std::memcpy(&s, fiber->bytes + 4, 4); return s; }
constexpr uint32_t RUNNING = 1, IDLE = 2;

struct alignas(16) Stack { unsigned char bytes[256 * 1024]; };
Stack stacks[4];

void init(Fiber *fiber, const char *name, Entry entry, uint64_t initial, Stack *stack, uint32_t version = NEW_VERSION) {
    alignas(8) static const uint32_t opt = OPT_MAGIC; // the runtime requires 8-byte aligned options
    CHECK(fiber_init(fiber, name, entry, initial, stack, stack ? sizeof(stack->bytes) : 0, &opt, version) == 0);
}
Fiber *self() { Fiber *out = nullptr; CHECK(fiber_self(&out) == 0); return out; }

// 1. Run -> ReturnToThread -> Run (resume) round trip.
Fiber round_fiber;
uint64_t round_initial, round_arg, round_resumed;
ABI void round_entry(uint64_t initial, uint64_t arg) {
    round_initial = initial; round_arg = arg;
    CHECK(self() == &round_fiber);
    CHECK(state(&round_fiber) == RUNNING);
    CHECK(fiber_finalize(&round_fiber) == FERR(6));
    CHECK(fiber_return(0x111, &round_resumed) == 0);
    CHECK(fiber_return(0x222, nullptr) == 0);
    std::abort();
}
void test_round_trip() {
    init(&round_fiber, "round", round_entry, 0xabc, &stacks[0]);
    uint64_t out = 0;
    CHECK(fiber_run(&round_fiber, 0x10, &out) == 0);
    CHECK(round_initial == 0xabc && round_arg == 0x10 && out == 0x111);
    CHECK(state(&round_fiber) == IDLE);
    CHECK(fiber_run(&round_fiber, 0x20, &out) == 0);
    CHECK(round_resumed == 0x20 && out == 0x222);
    CHECK(fiber_finalize(&round_fiber) == 0);
    CHECK(fiber_run(&round_fiber, 0, &out) == FERR(6));
}

// 2. Switch ping-pong A -> B -> A -> B -> thread.
Fiber fiber_a, fiber_b;
uint64_t b_initial, b_arg, a_switched, b_switched;
ABI void entry_a(uint64_t, uint64_t) {
    CHECK(self() == &fiber_a);
    CHECK(fiber_switch(&fiber_b, 7, &a_switched) == 0);
    CHECK(self() == &fiber_a && state(&fiber_b) == IDLE && state(&fiber_a) == RUNNING);
    CHECK(fiber_switch(&fiber_b, 9, nullptr) == 0);
    std::abort();
}
ABI void entry_b(uint64_t initial, uint64_t arg) {
    b_initial = initial; b_arg = arg;
    CHECK(self() == &fiber_b && state(&fiber_a) == IDLE);
    CHECK(fiber_switch(&fiber_a, 8, &b_switched) == 0);
    CHECK(self() == &fiber_b && state(&fiber_a) == IDLE);
    CHECK(fiber_return(10, nullptr) == 0);
    std::abort();
}
void test_switch() {
    init(&fiber_a, "a", entry_a, 1, &stacks[0]);
    init(&fiber_b, "b", entry_b, 2, &stacks[1]);
    uint64_t out = 0;
    CHECK(fiber_run(&fiber_a, 0, &out) == 0);
    CHECK(b_initial == 2 && b_arg == 7 && a_switched == 8 && b_switched == 9 && out == 10);
    CHECK(state(&fiber_a) == IDLE && state(&fiber_b) == IDLE);
}

// 3. A stackless fiber restarts its entry instead of resuming.
Fiber stackless;
int stackless_entries;
ABI void stackless_entry(uint64_t, uint64_t arg) {
    ++stackless_entries;
    CHECK(fiber_return(arg + (uint64_t)stackless_entries, nullptr) == 0);
    std::abort();
}
void test_stackless() {
    init(&stackless, "stackless", stackless_entry, 0, nullptr);
    uint64_t out = 0;
    CHECK(fiber_run(&stackless, 100, &out) == 0 && out == 101);
    CHECK(fiber_run(&stackless, 200, &out) == 0 && out == 202);
    CHECK(stackless_entries == 2);
}

// 4. Error contract.
Fiber errors_fiber, errors_other;
ABI void errors_entry(uint64_t, uint64_t) {
    uint64_t out;
    CHECK(fiber_run(&errors_other, 0, &out) == FERR(5));
    CHECK(fiber_switch(&errors_fiber, 0, &out) == FERR(6));
    fiber_return(0, nullptr);
    std::abort();
}
void test_errors() {
    alignas(16) static unsigned char misaligned[sizeof(Fiber) + 8];
    Fiber zero{}, *out = nullptr;
    uint64_t value;
    CHECK(fiber_self(nullptr) == FERR(1));
    CHECK(fiber_self(&out) == FERR(5));
    CHECK(fiber_return(0, &value) == FERR(5));
    CHECK(fiber_run(nullptr, 0, &value) == FERR(1));
    CHECK(fiber_run((Fiber *)(misaligned + 4), 0, &value) == FERR(2));
    CHECK(fiber_run(&zero, 0, &value) == FERR(4));
    CHECK(fiber_init(&zero, "small", errors_entry, 0, &stacks[2], 256, nullptr, NEW_VERSION) == FERR(3));
    init(&errors_other, "other", errors_entry, 0, &stacks[2]);
    CHECK(fiber_switch(&errors_other, 0, &value) == FERR(5));
    init(&errors_fiber, "errors", errors_entry, 0, &stacks[3]);
    CHECK(fiber_run(&errors_fiber, 0, &value) == 0);
}

// 5. x87 control word and MXCSR (status flags masked) are per context.
uint16_t get_cw() { uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw)); return cw; }
void set_cw(uint16_t cw) { __asm__ volatile("fldcw %0" : : "m"(cw)); }
uint32_t get_csr() { return _mm_getcsr() & ~0x3fu; }
constexpr uint16_t THREAD_CW = 0x77f, FIBER_CW = 0xb7f;
constexpr uint32_t THREAD_CSR = 0x3f80, FIBER_CSR = 0x5f80;
Fiber fp_fiber;
uint16_t fp_start_cw, fp_resume_cw;
uint32_t fp_start_csr, fp_resume_csr;
ABI void fp_entry(uint64_t, uint64_t) {
    fp_start_cw = get_cw(); fp_start_csr = get_csr();
    set_cw(FIBER_CW); _mm_setcsr(FIBER_CSR);
    fiber_return(0, nullptr);
    fp_resume_cw = get_cw(); fp_resume_csr = get_csr();
    fiber_return(0, nullptr);
    std::abort();
}
void test_fp(uint32_t version, uint16_t start_cw, uint32_t start_csr) {
    uint16_t cw = get_cw(); uint32_t csr = _mm_getcsr();
    set_cw(THREAD_CW); _mm_setcsr(THREAD_CSR);
    init(&fp_fiber, "fp", fp_entry, 0, &stacks[0], version);
    uint64_t out;
    CHECK(fiber_run(&fp_fiber, 0, &out) == 0);
    CHECK(get_cw() == THREAD_CW && get_csr() == THREAD_CSR);
    CHECK(fiber_run(&fp_fiber, 0, &out) == 0);
    CHECK(get_cw() == THREAD_CW && get_csr() == THREAD_CSR);
    CHECK(fp_start_cw == start_cw && fp_start_csr == start_csr);
    CHECK(fp_resume_cw == FIBER_CW && fp_resume_csr == FIBER_CSR);
    set_cw(cw); _mm_setcsr(csr);
}

// 6. A default-ABI (MS-ABI on Windows) caller keeps values live across switches.
volatile int seed_int = 3;
volatile double seed_double = 1.5;
__attribute__((noinline)) bool live_values_survive() {
    int i0 = seed_int, i1 = i0 * 5, i2 = i1 + 7, i3 = i2 ^ 0x55, i4 = i3 * 3, i5 = i4 - 11;
    double d0 = seed_double, d1 = d0 * 3, d2 = d1 + 0.25, d3 = d2 * d2, d4 = d3 - d0, d5 = d4 / 2,
           d6 = d5 + d1, d7 = d6 * 1.25, d8 = d7 - 2, d9 = d8 + d3;
    init(&fiber_a, "a", entry_a, 1, &stacks[0]);
    init(&fiber_b, "b", entry_b, 2, &stacks[1]);
    uint64_t out = 0;
    bool ran = fiber_run(&fiber_a, 0, &out) == 0 && out == 10;
    return ran && i0 == 3 && i1 == 15 && i2 == 22 && i3 == (22 ^ 0x55) && i4 == i3 * 3 && i5 == i4 - 11 &&
           d0 == 1.5 && d1 == 4.5 && d2 == 4.75 && d3 == 4.75 * 4.75 && d4 == d3 - 1.5 && d5 == d4 / 2 &&
           d6 == d5 + 4.5 && d7 == d6 * 1.25 && d8 == d7 - 2 && d9 == d8 + d3;
}

// 7. A fiber suspended on one host thread resumes and finishes on another.
Fiber cross_fiber;
std::thread::id cross_first, cross_second;
ABI void cross_entry(uint64_t, uint64_t) {
    cross_first = std::this_thread::get_id();
    fiber_return(1, nullptr);
    cross_second = std::this_thread::get_id();
    CHECK(self() == &cross_fiber);
    fiber_return(2, nullptr);
    std::abort();
}
void test_cross_thread() {
    init(&cross_fiber, "cross", cross_entry, 0, &stacks[0]);
    uint64_t out = 0;
    CHECK(fiber_run(&cross_fiber, 0, &out) == 0 && out == 1);
    uint64_t other_out = 0;
    std::thread other([&] { CHECK(fiber_run(&cross_fiber, 0, &other_out) == 0); });
    other.join();
    CHECK(other_out == 2 && cross_first == std::this_thread::get_id() && cross_second != cross_first);
    Fiber *current;
    CHECK(fiber_self(&current) == FERR(5));
}

#ifdef _WIN32
// 8. A VEH-handled write fault (the GPU page-tracking pattern) taken on a fiber stack.
volatile unsigned char *tracked;
int tracked_faults;
LONG CALLBACK track_fault(EXCEPTION_POINTERS *info) {
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2 ||
        record->ExceptionInformation[0] != 1 || record->ExceptionInformation[1] - (ULONG_PTR)tracked >= 4096)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD old;
    VirtualProtect((void *)tracked, 4096, PAGE_READWRITE, &old);
    ++tracked_faults;
    return EXCEPTION_CONTINUE_EXECUTION;
}
Fiber fault_fiber;
ABI void fault_entry(uint64_t, uint64_t) {
    tracked[123] = 0x5a;
    fiber_return(0, nullptr);
    std::abort();
}
void test_veh_fault() {
    tracked = (unsigned char *)VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old;
    CHECK(tracked && VirtualProtect((void *)tracked, 4096, PAGE_READONLY, &old));
    void *handler = AddVectoredExceptionHandler(1, track_fault);
    init(&fault_fiber, "fault", fault_entry, 0, &stacks[0]);
    uint64_t out;
    CHECK(fiber_run(&fault_fiber, 0, &out) == 0);
    CHECK(tracked_faults == 1 && tracked[123] == 0x5a);
    RemoveVectoredExceptionHandler(handler);
    VirtualFree((void *)tracked, 0, MEM_RELEASE);
}

// 9. Consumers of the TEB stack bounds, on a fiber stack below the thread's StackLimit
// (the adversarial layout: MSVC __chkstk probes from StackLimit down to its target).
// Each runs in a child process because a failure terminates the process.
constexpr size_t LOW_STACK = 256 * 1024;
void *low_stack() {
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t limit = (uintptr_t)tib->StackLimit;
    MEMORY_BASIC_INFORMATION info;
    for (uintptr_t at = 0x10000; at + LOW_STACK <= limit && VirtualQuery((void *)at, &info, sizeof(info));
         at = (uintptr_t)info.BaseAddress + info.RegionSize) {
        uintptr_t start = ((uintptr_t)info.BaseAddress + 0xffff) & ~(uintptr_t)0xffff;
        if (info.State == MEM_FREE && start + LOW_STACK <= (uintptr_t)info.BaseAddress + info.RegionSize &&
            start + LOW_STACK <= limit)
            if (void *stack = VirtualAlloc((void *)start, LOW_STACK, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
                return stack;
    }
    return nullptr;
}
bool rsp_in_bounds() {
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t rsp = (uintptr_t)__builtin_frame_address(0);
    return rsp >= (uintptr_t)tib->StackLimit && rsp < (uintptr_t)tib->StackBase;
}
__attribute__((noinline)) int throw_and_catch() {
    try { throw 42; } catch (int value) { return value; }
    return 0;
}
// ntdll's __chkstk: probe rax bytes below the caller's rsp.
extern "C" void chkstk_probe(void *chkstk, uint64_t size);
__asm__(".text\n.def chkstk_probe; .scl 2; .type 32; .endef\n.globl chkstk_probe\nchkstk_probe:\n"
        "mov %rdx,%rax\ncall *%rcx\nret\n");
void *chkstk;
int child_result;
Fiber child_fiber;
ABI void child_entry(uint64_t which, uint64_t) {
    if (which == 0) child_result = rsp_in_bounds();
    else if (which == 1) child_result = throw_and_catch() == 42;
    else { chkstk_probe(chkstk, 64 * 1024); child_result = 1; }
    fiber_return(0, nullptr);
    std::abort();
}
const char *const CHILD_CASES[] = {"teb-bounds", "cxx-throw", "chkstk"};
int run_child(const char *name) {
    uint64_t which = 0;
    while (which < 3 && strcmp(CHILD_CASES[which], name)) ++which;
    if (which == 3) return 2;
    void *stack = low_stack();
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    if (!stack) { std::printf("SKIP %s: no free range below StackLimit %p\n", name, tib->StackLimit); return 77; }
    chkstk = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "__chkstk");
    if (which == 2 && !chkstk) { std::printf("SKIP %s: ntdll has no __chkstk\n", name); return 77; }
    void *base = tib->StackBase, *limit = tib->StackLimit;
    alignas(8) static const uint32_t opt = OPT_MAGIC; // the runtime requires 8-byte aligned options
    if (fiber_init(&child_fiber, name, child_entry, which, stack, LOW_STACK, &opt, NEW_VERSION)) return 3;
    uint64_t out;
    if (fiber_run(&child_fiber, 0, &out)) return 4;
    if (tib->StackBase != base || tib->StackLimit != limit) return 5;
    return child_result ? 0 : 1;
}
void test_children(const char *self_path) {
    for (const char *name : CHILD_CASES) {
        intptr_t code = _spawnl(_P_WAIT, self_path, self_path, "--child", name, nullptr);
        std::printf("%s %s (exit 0x%lx)\n", code == 0 ? "PASS" : code == 77 ? "SKIP" : "FAIL", name, (unsigned long)code);
        if (code != 0 && code != 77) ++failures;
    }
}
#endif

template <typename T> void resolve(T &out, const char *name) {
    out = (T)runtime_fiber_resolve(name);
    if (!out) { std::fprintf(stderr, "FAIL: %s not exported\n", name); std::exit(1); }
}
void step(const char *name, void (*test)()) {
    int before = failures;
    test();
    std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", name);
}
} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    resolve(fiber_init, "sceFiberInitialize");
    resolve(fiber_finalize, "sceFiberFinalize");
    resolve(fiber_self, "sceFiberGetSelf");
    resolve(fiber_run, "sceFiberRun");
    resolve(fiber_switch, "sceFiberSwitch");
    resolve(fiber_return, "sceFiberReturnToThread");
#ifdef _WIN32
    if (argc == 3 && !strcmp(argv[1], "--child")) return run_child(argv[2]);
#endif
    (void)argc; (void)argv;
    step("round-trip", test_round_trip);
    step("switch", test_switch);
    step("stackless-restart", test_stackless);
    step("errors", test_errors);
    step("fp-control-new", [] { test_fp(NEW_VERSION, 0x37f, 0x9fc0); });
    step("fp-control-old", [] { test_fp(OLD_VERSION, THREAD_CW, THREAD_CSR); });
    step("default-abi-caller", [] { CHECK(live_values_survive()); });
    step("cross-thread", test_cross_thread);
#ifdef _WIN32
    step("veh-write-fault", test_veh_fault);
    test_children(argv[0]);
#endif
    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
