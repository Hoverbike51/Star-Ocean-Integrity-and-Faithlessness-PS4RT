// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

namespace Ps4RtScreenMask {
struct Vertex { std::array<float,4> clip{}; std::uint32_t colour=0; };
inline std::atomic<std::uint64_t> candidates=0,expanded=0;
enum class Topology { Triangles, Strip, Quads };
enum class Rejection { Draw, Fetch, Format, Constants, Vertex, Coverage, Count };
inline std::array<std::atomic<std::uint64_t>,static_cast<unsigned>(Rejection::Count)> rejected{};
inline bool Reject(Rejection reason) {
    ++rejected[static_cast<unsigned>(reason)];
    return false;
}
// This untextured native GUI pair exports interpolated vertex colour directly.
// Textured UI, movies, scene geometry and partial panels never qualify by colour alone.
constexpr bool SolidShader(std::uint64_t vs,std::uint64_t ps) {
    return vs==0x59cb445a && ps==0xf8591c05;
}
inline bool FullFrame(std::span<const Vertex> vertices,Topology topology) {
    if(vertices.size()!=(topology==Topology::Triangles?6u:4u)) return false;
    unsigned corners=0;std::array<unsigned,6> corner{};
    const auto colour=vertices[0].colour;
    if(colour&0xffffffu) return false;
    float depth=0;
    for(unsigned i=0;i<vertices.size();++i) {
        const auto& v=vertices[i];
        if(v.colour!=colour) return false; // Uniform black/alpha, never a gradient.
        for(float value:v.clip) if(!std::isfinite(value)) return false;
        if(v.clip[3]<=0) return false;
        const float x=v.clip[0]/v.clip[3],y=v.clip[1]/v.clip[3],z=v.clip[2]/v.clip[3];
        if(std::abs(std::abs(x)-1.f)>.001f || std::abs(std::abs(y)-1.f)>.001f || z<0 || z>1) return false;
        if(i==0)depth=z;else if(std::abs(depth-z)>.00001f)return false;
        corner[i]=1u<<((x>0?1:0)+(y>0?2:0));corners|=corner[i];
    }
    if(corners!=15) return false;
    const auto triangle=[&](unsigned a,unsigned b,unsigned c){return corner[a]!=corner[b]&&corner[a]!=corner[c]&&corner[b]!=corner[c];};
    if(topology==Topology::Quads) {
        // Gnm QuadList visits the perimeter (the tessellator reorders 1,2,0,3).
        // Opposite corners must be diagonal; reject crossed or repeated points.
        const auto diagonal=[](unsigned pair){return pair==6 || pair==9;};
        return diagonal(corner[0]|corner[2]) && diagonal(corner[1]|corner[3]);
    }
    if(topology==Topology::Strip) return triangle(0,1,2)&&triangle(1,2,3)&&((corner[1]|corner[2])==6 || (corner[1]|corner[2])==9);
    const unsigned shared=(corner[0]|corner[1]|corner[2])&(corner[3]|corner[4]|corner[5]);
    return triangle(0,1,2)&&triangle(3,4,5)&&(shared==6||shared==9);
}
} // namespace Ps4RtScreenMask
