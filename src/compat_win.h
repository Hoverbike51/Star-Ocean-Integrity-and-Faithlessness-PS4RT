/* Windows (MSYS2 CLANG64, MinGW-w64 + winpthreads) counterparts of the POSIX calls the
 * runtime uses. Paths are UTF-8: the executable manifest selects the UTF-8 code page. */
#ifndef PS4RT_COMPAT_WIN_H
#define PS4RT_COMPAT_WIN_H
#ifdef _WIN32
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef O_SYNC
#define O_SYNC 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC _O_NOINHERIT
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#endif

#define localtime_r(time, out) (localtime_s((out), (time)) ? NULL : (out))
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#endif

/* Protection bits used by the platform-neutral loader and memory backend. */
#ifndef PROT_NONE
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#endif

static inline int compat_mkdir(const char *path, int mode) { (void)mode; return _mkdir(path); }
#define mkdir(path, mode) compat_mkdir(path, mode)

ssize_t compat_pread(int fd, void *buffer, size_t size, int64_t offset);
ssize_t compat_pwrite(int fd, const void *buffer, size_t size, int64_t offset);
#define pread compat_pread
#define pwrite compat_pwrite
static inline int compat_fsync(int fd) { return _commit(fd); }
#define fsync compat_fsync
static inline char *compat_realpath(const char *path, char *out) { return _fullpath(out, path, PATH_MAX); }
#define realpath compat_realpath

long compat_utc_offset(time_t t);
int compat_random(void *buffer, size_t size);
void compat_sleep_ns(uint64_t ns);
int compat_copy_tree(const char *source, const char *destination);
int compat_remove_tree(const char *path);
int compat_errno_from_win32(unsigned long error);
int compat_cpu_times(int who, int64_t *user_us, int64_t *kernel_us);

/* setjmp/longjmp without SEH unwinding. Guest frames have no Win64 unwind metadata, so the
 * buffer explicitly preserves every non-volatile GPR plus xmm6-xmm15. */
typedef unsigned long long ps4rt_jmp_buf[32];
__attribute__((returns_twice)) int ps4rt_setjmp(ps4rt_jmp_buf buffer);
__attribute__((noreturn)) void ps4rt_longjmp(ps4rt_jmp_buf buffer, int value);
#endif
#endif
