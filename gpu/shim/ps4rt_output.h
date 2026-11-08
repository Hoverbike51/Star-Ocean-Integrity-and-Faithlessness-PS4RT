// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <string_view>
#include <cstdint>
#include <cmath>
#include <cstdlib>

namespace Ps4RtOutput {
// Keep the first five persisted indices unchanged. New entries describe output
// surfaces, never a patch to the game's fixed 1080p scene.
inline constexpr int Widths[] = {1280,1920,2560,3840,854,640,800,1024,1280,1600,1920,2560,1280,1440,1680,1920,2560,3840,2560,3440,3840,5120,3840,5120,7680,960,1600,2880,1280,1366,3000,2256,1600,1600,2560,3200};
inline constexpr int Heights[] = {720,1080,1440,2160,480,480,600,768,960,1200,1440,1920,800,900,1050,1200,1600,2400,1080,1440,1600,2160,1080,1440,2160,540,900,1620,1024,768,2000,1504,686,450,720,900};
inline constexpr int Count = 36;
static_assert(Count < 64);
inline constexpr const wchar_t* Labels[] = {L"1280 × 720",L"1920 × 1080",L"2560 × 1440",L"3840 × 2160",L"854 × 480",L"640 × 480",L"800 × 600",L"1024 × 768",L"1280 × 960",L"1600 × 1200",L"1920 × 1440",L"2560 × 1920",L"1280 × 800",L"1440 × 900",L"1680 × 1050",L"1920 × 1200",L"2560 × 1600",L"3840 × 2400",L"2560 × 1080",L"3440 × 1440",L"3840 × 1600",L"5120 × 2160",L"3840 × 1080",L"5120 × 1440",L"7680 × 2160",L"960 × 540",L"1600 × 900",L"2880 × 1620",L"1280 × 1024",L"1366 × 768",L"3000 × 2000",L"2256 × 1504",L"1600 × 686",L"1600 × 450",L"2560 × 720",L"3200 × 900"};
inline constexpr const char* Names[] = {"1280 x 720","1920 x 1080","2560 x 1440","3840 x 2160","854 x 480","640 x 480","800 x 600","1024 x 768","1280 x 960","1600 x 1200","1920 x 1440","2560 x 1920","1280 x 800","1440 x 900","1680 x 1050","1920 x 1200","2560 x 1600","3840 x 2400","2560 x 1080","3440 x 1440","3840 x 1600","5120 x 2160","3840 x 1080","5120 x 1440","7680 x 2160","960 x 540","1600 x 900","2880 x 1620","1280 x 1024","1366 x 768","3000 x 2000","2256 x 1504","1600 x 686","1600 x 450","2560 x 720","3200 x 900"};
inline constexpr const char* Arguments[] = {"1280x720","1920x1080","2560x1440","3840x2160","854x480","640x480","800x600","1024x768","1280x960","1600x1200","1920x1440","2560x1920","1280x800","1440x900","1680x1050","1920x1200","2560x1600","3840x2400","2560x1080","3440x1440","3840x1600","5120x2160","3840x1080","5120x1440","7680x2160","960x540","1600x900","2880x1620","1280x1024","1366x768","3000x2000","2256x1504","1600x686","1600x450","2560x720","3200x900"};
inline constexpr auto Order=[] {
    std::array<int,Count> order{}; for(int i=0;i<Count;++i) order[i]=i;
    for(int i=0;i<Count;++i) for(int j=i+1;j<Count;++j)
        if(std::int64_t(Widths[order[j]])*Heights[order[j]] < std::int64_t(Widths[order[i]])*Heights[order[i]])
            std::swap(order[i],order[j]);
    return order;
}();
enum Aspect { All, Ratio4_3, Ratio16_9, Ratio16_10, Ratio21_9, Ratio32_9, AspectCount };
inline constexpr const char* AspectNames[] = {"All","4:3","16:9","16:10","21:9","32:9"};
inline int Group(int index) {
    if(index<0 || index>=Count) return All;
    const double ratio=double(Widths[index])/Heights[index];
    if(std::abs(ratio-4.0/3)<.015) return Ratio4_3;
    if(std::abs(ratio-16.0/9)<.02) return Ratio16_9;
    if(std::abs(ratio-16.0/10)<.015) return Ratio16_10;
    // 2560x1080, 3440x1440 and 3840x1600 are the 21:9 family.
    if(ratio>=2.30 && ratio<=2.42) return Ratio21_9;
    if(std::abs(ratio-32.0/9)<.025) return Ratio32_9;
    return All;
}
inline bool Matches(int index,int aspect) { return aspect==All || Group(index)==aspect; }
inline bool Fits(int index,int width,int height) {
    return index>=0 && index<Count && Widths[index]<=width && Heights[index]<=height;
}
inline std::uint64_t Fitting(int width,int height) {
    std::uint64_t mask=0; for(int i=0;i<Count;++i) if(Fits(i,width,height)) mask|=1ull<<i; return mask;
}
inline std::uint64_t Filter(std::uint64_t mask,int aspect) {
    std::uint64_t result=0;
    for(int i=0;i<Count;++i) if((mask&(1ull<<i)) && Matches(i,aspect)) result|=1ull<<i;
    return result;
}
inline int Select(int current,std::uint64_t mask,int aspect=All) {
    mask=Filter(mask,aspect);
    if(current>=0 && current<Count && (mask&(1ull<<current))) return current;
    const auto target=(current>=0 && current<Count)?std::int64_t(Widths[current])*Heights[current]:1920ll*1080;
    int best=-1; std::int64_t difference=INT64_MAX;
    for(int i:Order) if(mask&(1ull<<i)) {
        const auto d=std::llabs(std::int64_t(Widths[i])*Heights[i]-target);
        if(d<difference) { best=i; difference=d; }
    }
    return best;
}
inline int Limit(int index,int width,int height) { return Select(index,Fitting(width,height)); }
inline int Parse(std::wstring_view label) {
    for(int i=0;i<Count;++i) if(label==Labels[i]) return i;
    return 1;
}
inline int Find(int width,int height) {
    for(int i=0;i<Count;++i) if(Widths[i]==width && Heights[i]==height) return i;
    return -1;
}
struct Size { int width,height; };
inline Size Content(int index) {
    index=std::clamp(index,0,Count-1);
    const int w=Widths[index],h=Heights[index];
    if(Group(index)==Ratio16_9) return {w,h};
    if(std::int64_t(w)*9>=std::int64_t(h)*16) return {((h*16/9)&~1),h};
    return {w,((w*9/16)&~1)};
}
inline bool TemporalAllowed(int index) {
    const auto size=Content(index); return size.width>=640 && size.height>=360;
}
}
