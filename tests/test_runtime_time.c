/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include "runtime_time_windows.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    DYNAMIC_TIME_ZONE_INFORMATION zone={0};zone.Bias=300;zone.DaylightBias=-60;
    zone.StandardDate=(SYSTEMTIME){.wMonth=11,.wDay=1,.wHour=2};
    zone.DaylightDate=(SYSTEMTIME){.wMonth=3,.wDay=2,.wHour=2};
    int64_t local=0;GuestTimesec st={0};
    struct {uint64_t value,guard;} dst={UINT64_MAX,0x123456789abcdef0ULL};
    assert(convert_utc_to_local(1704067200,&local,&st,&dst.value,&zone)==0);
    assert(local==1704067200-18000 && (int32_t)st.west_sec==-18000 && st.dst_sec==0 && dst.value==0);
    assert(convert_utc_to_local(1719792000,&local,&st,&dst.value,&zone)==0);
    assert(local==1719792000-14400 && st.t==1719792000 && st.dst_sec==3600 && dst.value==3600);
    assert(dst.guard==0x123456789abcdef0ULL);
    assert(convert_utc_to_local(1704067200,NULL,NULL,NULL,&zone)==0);
    local=123;assert(convert_utc_to_local(INT64_MAX,&local,&st,&dst.value,&zone)!=0 && local==123);
    assert(convert_utc_to_local(INT64_MIN,&local,&st,&dst.value,&zone)!=0 && local==123);
    typedef int32_t (__attribute__((sysv_abi)) *Convert)(int64_t,int64_t*,GuestTimesec*,uint64_t*);
    Convert imported=(Convert)runtime_time_resolve("-o5uEDpN+oY#libkernel");assert(imported);
    assert(imported(1794149760,&local,&st,&dst.value)==0 && st.t==1794149760);
    assert(runtime_time_resolve("unknown#libkernel")==0);
    puts("PASS: trophy time import, guest ABI, nullable outputs, winter/summer offsets and invalid dates");
    return 0;
}
