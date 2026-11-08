/* SPDX-License-Identifier: GPL-2.0-or-later
 * Guest allowances are separate from Windows RAM and the Vulkan memory budget.
 * Keep console-compatible defaults; larger values are explicit developer experiments. */
#ifndef PS4RT_RUNTIME_MEMORY_BUDGET_H
#define PS4RT_RUNTIME_MEMORY_BUDGET_H
#include <stdint.h>

#define PS4RT_DIRECT_DEFAULT_MB UINT64_C(5056)
#define PS4RT_DIRECT_MAX_MB UINT64_C(16384)
#define PS4RT_FLEXIBLE_DEFAULT_MB UINT64_C(448)
#define PS4RT_FLEXIBLE_MAX_MB UINT64_C(1024)

/* Strict decimal MiB, rejecting signs, junk and overflow. Never silently wrap or
 * reduce an allowance below the compatibility default. No allocation or host calls. */
static inline uint64_t runtime_memory_budget_mb(const char *text, uint64_t minimum,
                                                uint64_t maximum, int *accepted) {
    uint64_t value = 0;
    if (accepted) *accepted = 0;
    if (!text || !*text) return minimum;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9') return minimum;
        const unsigned digit = *p - '0';
        if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10))
            return minimum;
        value = value * 10 + digit;
    }
    if (value < minimum) return minimum;
    if (accepted) *accepted = 1;
    return value;
}
#endif
