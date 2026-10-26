// SPDX-License-Identifier: GPL-2.0-or-later
// ps4rt (Windows): small POSIX helpers the renderer uses for diagnostics and thread
// identity. Force-included into the GPU library on Windows only (CMakeLists.txt); kept free
// of <windows.h> so its macros do not reach every translation unit.
#pragma once
#ifdef _WIN32
#include <stddef.h>
#include <wchar.h>
#ifdef __cplusplus
#include <cmath>
#include <mutex>
#include <new>
#endif
#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif
#ifndef swprintf_s
#define swprintf_s swprintf
#endif

#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableA(
    const char* name, char* buffer, unsigned long size);
__declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
int __cdecl _putenv_s(const char* name, const char* value);
#ifdef __cplusplus
}
#endif

static inline int ps4rt_setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && GetEnvironmentVariableA(name, NULL, 0) != 0) {
        return 0;
    }
    return _putenv_s(name, value);
}

static inline int ps4rt_unsetenv(const char* name) {
    return _putenv_s(name, "");
}

#ifndef setenv
#define setenv ps4rt_setenv
#endif
#ifndef unsetenv
#define unsetenv ps4rt_unsetenv
#endif
static inline int gettid(void) { return (int)GetCurrentThreadId(); }
#endif
