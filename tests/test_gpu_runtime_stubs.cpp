// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <setjmp.h>
#ifdef _WIN32
typedef unsigned long long sigjmp_buf[32];
#endif
// Renderer tests have no guest process. Clock/host-thread services work; guest accesses abort.
extern "C" {
thread_local sigjmp_buf* runtime_fault_recover = nullptr;
#ifdef _WIN32
int ps4rt_setjmp(unsigned long long buffer[32]) {
    (void)buffer;
    return 0;
}
#endif
uint32_t runtime_disabled_optimizations = 0;
int runtime_pause_gate() { return 0; } // runtime_crowbar_pause.c: never paused in renderer tests
void runtime_report() {}
uint64_t runtime_tsc_frequency() { return 1000000000; }
int runtime_file_translate(const char*, char*, size_t) { std::abort(); }
uint64_t runtime_memory_clamp(uintptr_t, uint64_t) { std::abort(); }
uint64_t runtime_process_time_us() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int runtime_memory_region(uintptr_t, uintptr_t*, uintptr_t*, int*) { std::abort(); }
void runtime_memory_set_gpu_hooks(void (*)(uintptr_t, uint64_t),
    void (*)(uintptr_t, uint64_t), void (*)(uintptr_t, uint64_t)) { std::abort(); }
void runtime_thread_attach_host(const char*) {}
int32_t runtime_thread_current_priority() { return 700; }
void runtime_memory_gpu_protect(uintptr_t, uint64_t, int, int) { std::abort(); }
void runtime_restart() { std::abort(); }
void runtime_pad_refresh_keyboard() {}
void runtime_pad_devices_changed() {}
uint64_t runtime_process_time_counter() { return runtime_process_time_us() * 1000; }
uint64_t runtime_read_tsc() { return runtime_process_time_counter(); }
int32_t* runtime_errno() { std::abort(); }
int runtime_memory_write_backing(uintptr_t, const void*, uint64_t) { std::abort(); }
int runtime_memory_is_mapped(uintptr_t, uint64_t) { std::abort(); }
}


extern "C" {
static bool cheat_fixture=false;
void test_gpu_cheat_fixture(){cheat_fixture=true;}

int runtime_cheat_count(){return cheat_fixture?8:0;}
int runtime_cheat_catalog_id(int i){static const int ids[]={1,2,4,5,6,7,8,9};return i>=0&&i<8?ids[i]:0;}
int runtime_cheat_builtin(int i){return i>=0&&i<8;}
const char* runtime_cheat_description(int){return "Native fixture description.";}
const char* runtime_cheat_name(int){return "Native fixture code";}
static int states[10]{},values[10]{};
int runtime_cheat_state(int i){return cheat_fixture&&i>=0&&i<8?states[i]:-1;}
const char* runtime_cheat_problem(int){return nullptr;}
int runtime_cheat_file_problem_count(){return 0;} const char* runtime_cheat_file_problem(int){return "";}
int runtime_cheat_set(int i,int on,char*,size_t){states[i]=on;return 0;}
int runtime_cheat_choice_count(int i){return cheat_fixture&&i==7?2:0;}
float runtime_cheat_choice_value(int,int choice){return choice?2.f:1.5f;}
int runtime_cheat_choice(int i){return states[i]?values[i]:-1;}
int runtime_cheat_select(int i,int choice,char*,size_t){states[i]=choice>=0;if(choice>=0)values[i]=choice;return 0;}
int runtime_cheat_is_action(int){return 0;} int runtime_cheat_add(int,char*,size_t){return -1;}
}
