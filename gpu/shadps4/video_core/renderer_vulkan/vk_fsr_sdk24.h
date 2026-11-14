// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/vk_fsr4.h"
#include <filesystem>
#include <string>

namespace Vulkan {
// Official signed AMD Vulkan provider; isolated from the source-v07 API/ABI.
class FsrSdk24 {
public:
    FsrSdk24(const Instance&, Scheduler&, const std::filesystem::path& directory = {});
    ~FsrSdk24();
    bool Available() const;
    bool Record(const Fsr4Upscaler::Frame&);
    const std::string& Problem() const;
    const std::string& Provider() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
