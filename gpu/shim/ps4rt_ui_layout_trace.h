// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <ostream>

namespace Ps4RtUiLayout {
// Registers only: no guest-memory sampling, textures, GPU readbacks or disk I/O
// during gameplay. The existing performance report writes the bounded snapshot.
struct Draw {
    std::uint64_t vertex=0,pixel=0;
    std::array<int,4> screen{},window{},generic{},combined{};
    std::array<float,4> viewport{}; // x/y scale, x/y offset in guest coordinates
    std::array<float,4> transform{};
    int width=0,height=0;
    bool display=false,clip_disabled=false;
    bool operator==(const Draw&) const=default;
};
class Collector {
public:
    static constexpr std::size_t Capacity=128;
    void Reset() {std::scoped_lock lock{mutex};count=0;overflow=0;}
    bool Note(const Draw& draw) {
        for(float value:draw.viewport) if(!std::isfinite(value)) return false;
        for(float value:draw.transform) if(!std::isfinite(value)) return false;
        std::scoped_lock lock{mutex};
        for(std::size_t i=0;i<count;++i) if(entries[i].draw==draw) {++entries[i].hits;return true;}
        if(count==Capacity) {++overflow;return false;}
        entries[count++]={draw,1};return true;
    }
    void Write(std::ostream& out) {
        std::scoped_lock lock{mutex};
        out<<"{\"capacity\":"<<Capacity<<",\"overflow_draws\":"<<overflow<<",\"draws\":[";
        const auto array=[&](const auto& values) {
            out<<'[';for(std::size_t i=0;i<values.size();++i) out<<(i?",":"")<<values[i];out<<']';
        };
        for(std::size_t i=0;i<count;++i) {
            const auto& e=entries[i];const auto& d=e.draw;
            out<<(i?",":"")<<"\n{\"vertex_shader\":\""<<d.vertex<<"\",\"pixel_shader\":\""<<d.pixel
               <<"\",\"hits\":"<<e.hits<<",\"target\":["<<d.width<<','<<d.height
               <<"],\"display\":"<<(d.display?"true":"false")
               <<",\"clip_disabled\":"<<(d.clip_disabled?"true":"false")<<",\"screen_scissor\":";
            array(d.screen);out<<",\"window_scissor\":";array(d.window);
            out<<",\"generic_scissor\":";array(d.generic);out<<",\"combined_scissor\":";array(d.combined);
            out<<",\"viewport\":";array(d.viewport);out<<",\"transform\":";array(d.transform);out<<'}';
        }
        out<<"]}";
    }
private:
    struct Entry {Draw draw;std::uint64_t hits=0;};
    std::array<Entry,Capacity> entries{};
    std::size_t count=0;
    std::uint64_t overflow=0;
    std::mutex mutex;
};
inline Collector capture;
}
