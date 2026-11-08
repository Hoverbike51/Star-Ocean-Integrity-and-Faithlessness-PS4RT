/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SO5_RUNTIME_TIME_WINDOWS_H
#define SO5_RUNTIME_TIME_WINDOWS_H
#include <windows.h>
#include <stdint.h>
#include <limits.h>
/* Guest ABI: seconds and DST output are 64-bit; the two offset fields are 32-bit.
 * Keep this layout independent from the Windows CRT's timezone and time_t. */
typedef struct { int64_t t; uint32_t west_sec, dst_sec; } GuestTimesec;
_Static_assert(sizeof(GuestTimesec)==16, "Guest timesec ABI");
static int32_t convert_utc_to_local(int64_t utc,int64_t *local,GuestTimesec *st,uint64_t *dst,
                                  const DYNAMIC_TIME_ZONE_INFORMATION *zone) {
    const int64_t epoch=11644473600LL;
    if (utc < -epoch || utc > INT64_MAX/10000000LL-epoch) return (int32_t)0x80020016;
    ULARGE_INTEGER value; value.QuadPart=(uint64_t)(utc+epoch)*10000000ULL;
    FILETIME ft={value.LowPart,value.HighPart}, shifted; SYSTEMTIME universal,wall;
    TIME_ZONE_INFORMATION year;
    if (!FileTimeToSystemTime(&ft,&universal))return (int32_t)0x80020016;
    // Fixed/custom zones need no registry lookup. Named Windows zones use the
    // historical rules for this timestamp's year, not today's DST offset.
    year=(TIME_ZONE_INFORMATION){0};year.Bias=zone->Bias;year.StandardBias=zone->StandardBias;year.DaylightBias=zone->DaylightBias;
    year.StandardDate=zone->StandardDate;year.DaylightDate=zone->DaylightDate;
    if(zone->TimeZoneKeyName[0] && !GetTimeZoneInformationForYear(universal.wYear,(PDYNAMIC_TIME_ZONE_INFORMATION)zone,&year))
        return (int32_t)0x80020016;
    if(zone->DynamicDaylightTimeDisabled){year.StandardDate.wMonth=0;year.DaylightDate.wMonth=0;year.DaylightBias=year.StandardBias;}
    if (!SystemTimeToTzSpecificLocalTime(&year,&universal,&wall) || !SystemTimeToFileTime(&wall,&shifted))
        return (int32_t)0x80020016;
    ULARGE_INTEGER local_value;local_value.LowPart=shifted.dwLowDateTime;local_value.HighPart=shifted.dwHighDateTime;
    const int64_t offset=((int64_t)local_value.QuadPart-(int64_t)value.QuadPart)/10000000LL;
    const int64_t standard=-60LL*(year.Bias+year.StandardBias);
    const int64_t daylight=offset-standard;
    if(local)*local=utc+offset;
    if(st){st->t=utc;st->west_sec=(uint32_t)(int32_t)standard;st->dst_sec=(uint32_t)(int32_t)daylight;}
    if(dst)*dst=(uint64_t)daylight;
    return 0;
}
static __attribute__((sysv_abi)) int32_t guest_utc_to_local(int64_t utc,int64_t *local,GuestTimesec *st,uint64_t *dst) {
    DYNAMIC_TIME_ZONE_INFORMATION zone;
    if(GetDynamicTimeZoneInformation(&zone)==TIME_ZONE_ID_INVALID)return (int32_t)0x80020016;
    return convert_utc_to_local(utc,local,st,dst,&zone);
}
#endif
