// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace Ps4RtTitle {
// Fingerprints of four logo landmarks in the title-only GUI atlas. No texture
// pixels, paths, guest addresses, frame timers or gameplay assumptions are stored.
// Unrecognized regional artwork fails closed (no label).
inline bool Atlas(uint32_t width,uint32_t height,uint32_t format,uint32_t tile,uint32_t pitch,
                  const void* allocation,size_t size) {
    if(width!=2048 || height!=2048 || format!=37 || tile!=13 || pitch!=2048 || !allocation || size<16777216) return false;
    constexpr size_t offsets[]={10263040,11321856,12376576,14205440};
    const auto* bytes=static_cast<const uint8_t*>(allocation);uint64_t hash=14695981039346656037ull;
    for(const auto offset:offsets) for(size_t i=0;i<256;++i) hash=(hash^bytes[offset+i])*1099511628211ull;
    return hash==0xb06cd74027263887ull;
}
// Depth-only graphics pipelines legitimately have no fragment program. Keep
// the optional-stage check inside the recognition predicate, before reading it.
template<class Stage>
inline bool UiShader(const Stage* fragment) {
    return fragment && fragment->pgm_hash==0x4eab1bc3;
}
inline std::atomic<bool> drawn=false;
inline void MarkDraw() {drawn.store(true,std::memory_order_relaxed);}
inline bool ConsumeFrame() {return drawn.exchange(false,std::memory_order_relaxed);}
}
