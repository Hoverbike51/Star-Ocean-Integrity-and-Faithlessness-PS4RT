// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace Ps4RtScene {
inline constexpr int Original=-1;
struct Size {uint32_t width=1920,height=1080;bool operator==(const Size&) const=default;};
inline int ForOutput(int preset,uint32_t width,uint32_t height) {
    // Original is a fixed 1080p input, not a ratio. Smaller outputs need a real
    // scaled input; choose Quality rather than disabling a supported provider.
    return preset<0 && (width<1920 || height<1080)?1:std::clamp(preset,-1,4);
}
inline Size Input(int preset,uint32_t width,uint32_t height) {
    if(preset==Original) return {};
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
}
