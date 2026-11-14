// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace Ps4RtScene {
inline constexpr int Original=-1;
struct Size {uint32_t width=1920,height=1080;bool operator==(const Size&) const=default;};
inline constexpr std::array<int,4> NativeHeights{720,1080,1440,2160};
inline int NativeHeight(int height) {
    return std::find(NativeHeights.begin(),NativeHeights.end(),height)!=NativeHeights.end()?height:1080;
}
inline Size NativeInput(int height,uint32_t output_width,uint32_t output_height,bool ultrawide=false) {
    height=NativeHeight(height);
    const double aspect=ultrawide && output_height?double(output_width)/output_height:16.0/9.0;
    return {uint32_t(std::max(1l,std::lround(height*aspect/2))*2),uint32_t(height)};
}
inline int ForOutput(int preset,uint32_t width,uint32_t height,bool native_aspect=false) {
    // Original keeps a 1080-line input at the actual projection ratio. A 4:3
    // 1440x1080 input is valid; it must not be rejected for being under 1920 wide.
    const auto original=NativeInput(1080,width,height,native_aspect);
    return preset<0 && (width<original.width || height<original.height)?1:std::clamp(preset,-1,4);
}
inline Size Input(int preset,uint32_t width,uint32_t height,bool ultrawide=false) {
    if(preset==Original) return ultrawide && height?
        Size{uint32_t(std::lround(double(width)*1080/height/2))*2,1080}:Size{};
    constexpr double scales[]={1.0,1.5,1.7,2.0,3.0};
    const auto scale=scales[std::clamp(preset,0,4)];
    return {uint32_t(std::max(1l,std::lround(width/scale/2))*2),uint32_t(std::max(1l,std::lround(height/scale/2))*2)};
}
inline int ModelForInput(uint32_t width,uint32_t height,uint32_t output_width,uint32_t output_height) {
    for(int preset=0;preset<5;++preset) {
        const auto input=Input(preset,output_width,output_height);
        if(input.width==width && input.height==height) return preset;
    }
    return 5; // variable-ratio model for the original input at nonstandard ratios
}
inline Size RequestedInput(bool temporal,int preset,int native_height,uint32_t width,uint32_t height,bool ultrawide=false) {
    return temporal?Input(ForOutput(preset,width,height,ultrawide),width,height,ultrawide):NativeInput(native_height,width,height,ultrawide);
}
}
