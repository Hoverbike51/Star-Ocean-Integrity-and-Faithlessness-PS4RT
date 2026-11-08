// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

namespace Ps4RtCameraProbe {
inline constexpr std::size_t FollowUpdateAddress = 0x1b6090;
inline constexpr std::size_t SceneGlobal = 0xb83a10;
inline constexpr std::size_t RendererGlobal = 0xb839e8;
inline constexpr std::size_t ControlSwitchAddress=0x1a12d0;
inline constexpr std::array<unsigned char,24> ControlSwitchSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
    0x53,0x48,0x83,0xe4,0xe0,0x48,0x81,0xec,0xc0,0,0,0};
inline constexpr std::array<unsigned char,24> FollowSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
    0x53,0x48,0x81,0xec,0x18,0x03,0,0,0x48,0x8b,0x1d,0xf5};
inline constexpr std::array<unsigned char,21> LensSignature{
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,
    0x18,0x48,0x89,0xfb,0xc5,0xfa,0x11,0x45,0xe0};
inline constexpr std::array<unsigned char,19> ManagerSignature{
    0x48,0x8d,0x05,0x99,0xab,0x9f,0,0x48,0x8b,0,0x48,0x8b,0x40,
    0x40,0x48,0x83,0xc0,0x20,0xc3};
template<std::size_t N>
bool Signature(std::span<const unsigned char> image, std::size_t address,
               const std::array<unsigned char,N>& bytes) {
    return address <= image.size() && N <= image.size()-address &&
           std::memcmp(image.data()+address,bytes.data(),N)==0;
}
inline bool ProfileMatches(std::span<const unsigned char> image,
                           std::string_view serial, std::string_view version) {
    return serial=="CUSA03219" && version=="01.00" && image.size()>=SceneGlobal+8 &&
           Signature(image,FollowUpdateAddress,FollowSignature) &&
           Signature(image,0x4fbdb0,LensSignature) &&
           Signature(image,0x188e70,ManagerSignature);
}
}
