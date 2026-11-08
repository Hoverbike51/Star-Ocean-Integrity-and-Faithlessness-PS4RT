// SPDX-License-Identifier: GPL-2.0-or-later
// Guest-memory allowance parsing: no guest data, RAM commitment or Vulkan device needed.
#include "runtime_memory_budget.h"
#include <cassert>
#include <cstdio>
#include <string>

int main() {
    struct Policy { uint64_t minimum, maximum; };
    for (auto policy : {Policy{PS4RT_DIRECT_DEFAULT_MB,PS4RT_DIRECT_MAX_MB},
                        Policy{PS4RT_FLEXIBLE_DEFAULT_MB,PS4RT_FLEXIBLE_MAX_MB}}) {
        int accepted=1;
        assert(runtime_memory_budget_mb(nullptr,policy.minimum,policy.maximum,&accepted)==policy.minimum);
        assert(!accepted);
        for (const char* invalid : {"","-1","+8192","8192x","0x2000","8192.0",
                                    " 8192","8192 ","18446744073709551616","9999999999999999999999999"}) {
            accepted=1;
            assert(runtime_memory_budget_mb(invalid,policy.minimum,policy.maximum,&accepted)==policy.minimum);
            assert(!accepted);
        }
        for (auto invalid : {policy.minimum-1,policy.maximum+1}) {
            accepted=1;
            assert(runtime_memory_budget_mb(std::to_string(invalid).c_str(),policy.minimum,policy.maximum,&accepted)==policy.minimum);
            assert(!accepted);
        }
        for (auto valid : {policy.minimum,policy.minimum+1,policy.maximum-1,policy.maximum}) {
            accepted=0;
            assert(runtime_memory_budget_mb(std::to_string(valid).c_str(),policy.minimum,policy.maximum,&accepted)==valid);
            assert(accepted);
        }
        assert(runtime_memory_budget_mb("0000",policy.minimum,policy.maximum,nullptr)==policy.minimum);
    }
    assert(runtime_memory_budget_mb("8192",PS4RT_DIRECT_DEFAULT_MB,PS4RT_DIRECT_MAX_MB,nullptr)==8192);
    assert(runtime_memory_budget_mb("1024",PS4RT_FLEXIBLE_DEFAULT_MB,PS4RT_FLEXIBLE_MAX_MB,nullptr)==1024);
    std::puts("PASS: bounded explicit guest-memory budgets, compatibility defaults and strict overflow rejection");
}
