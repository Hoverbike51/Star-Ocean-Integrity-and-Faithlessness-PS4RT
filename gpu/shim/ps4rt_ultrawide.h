// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include "ps4rt_output.h"

namespace Ps4RtUltrawide {
// One executable revision only. A title/version string alone never authorizes a
// write. The screen descriptor constructor and its aspect consumer must match.
inline constexpr std::size_t AspectAddress=0x851098;
inline constexpr unsigned char Constructor[]={
    0x48,0x8d,0x0d,0xe1,0x0b,0x85,0x00,0xc5,0xf8,0x57,0xc0,
    0xc5,0xf8,0xc2,0x05,0xec,0x0a,0x85,0x00,0x04,
    0x48,0x8d,0x05,0xa5,0x91,0xa3,0x00,0xc5,0xfa,0x10,0x09,
    0x66,0xc7,0x00,0x80,0x07,0x66,0xc7,0x40,0x02,0x38,0x04,
    0xc5,0xf8,0x29,0x05,0x9e,0x91,0xa3,0x00,0xc5,0xfa,0x11,0x48,0x04};
inline constexpr unsigned char Consumer[]={0xc5,0xfa,0x10,0x43,0x04,0xc4,0xc1,0x7a,0x11,0x46,0x5c};
inline bool Matches(std::span<const unsigned char> image,std::string_view serial,std::string_view version) {
    constexpr std::uint32_t original=0x3fe38e39;
    std::uint32_t value=0;
    if(serial!="CUSA03219" || version!="01.00" || image.size()<AspectAddress+4) return false;
    std::memcpy(&value,image.data()+AspectAddress,4);
    return value==original && !std::memcmp(image.data()+0x4b0,Constructor,sizeof Constructor) &&
           !std::memcmp(image.data()+0x1eef4,Consumer,sizeof Consumer);
}
inline bool Wide(int output) {
    return Ps4RtOutput::Group(output)==Ps4RtOutput::Ratio21_9 ||
           Ps4RtOutput::Group(output)==Ps4RtOutput::Ratio32_9;
}
inline float Ratio(int output) {
    return output>=0 && output<Ps4RtOutput::Count?
        float(Ps4RtOutput::Widths[output])/Ps4RtOutput::Heights[output]:16.f/9;
}
// Called before relocation protection and guest initializers. Never touches the
// on-disk eboot, target allocation sizes, save data or game logic.
inline bool Patch(std::span<unsigned char> image,std::string_view serial,std::string_view version,int output) {
    if(!Wide(output) || !Matches(image,serial,version)) return false;
    const float aspect=Ratio(output);
    std::memcpy(image.data()+AspectAddress,&aspect,sizeof aspect);
    return true;
}
inline std::atomic<int> session_output{-1};
inline bool Enabled() {return Wide(session_output.load());}
inline Ps4RtOutput::Size Content(int output) {
    if(Enabled() && Wide(output)) return {Ps4RtOutput::Widths[output],Ps4RtOutput::Heights[output]};
    return Ps4RtOutput::Content(output);
}
// The engine copies the descriptor at startup. Keep live resolution changes in
// the same exact projection ratio; changing aspect requires a new game launch.
inline std::uint64_t SessionModes(std::uint64_t modes) {
    const int startup=session_output.load();
    if(startup<0) return modes; // launcher and pre-initialization enumeration
    for(int i=0;i<Ps4RtOutput::Count;++i) {
        const bool allowed=Enabled()?std::abs(Ratio(i)-Ratio(startup))<.001f:!Wide(i);
        if(!allowed) modes&=~(1ull<<i);
    }
    return modes;
}
struct Layout {float scale_x,scale_y,offset_x,offset_y;};
inline Layout Hud(unsigned width,unsigned height) {
    const float scale=std::min(width/1920.f,height/1080.f);
    return {scale,scale,(width-1920*scale)*.5f,(height-1080*scale)*.5f};
}
struct Box {int left,top,right,bottom;};
inline Box Background(unsigned width,unsigned height,bool scene) {
    if(scene) return {0,0,int(width),int(height)};
    const auto fit=Hud(width,height);
    const int left=int(std::lround(fit.offset_x)),top=int(std::lround(fit.offset_y));
    return {left,top,left+int(std::lround(1920*fit.scale_x)),top+int(std::lround(1080*fit.scale_y))};
}
inline bool CameraMatches(float xscale,float yscale,int output) {
    return std::isfinite(xscale) && std::isfinite(yscale) && std::abs(xscale)>.00001f &&
        std::abs(std::abs(yscale/xscale)-Ratio(output))<.025f;
}
}
