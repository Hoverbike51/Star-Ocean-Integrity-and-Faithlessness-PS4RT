// SPDX-License-Identifier: GPL-2.0-or-later
// Actual Vulkan selected-device rejection test. Never runs NGX reconstruction.
#include <cassert>
#include <cstdio>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_dlss.h"

int main() {
    Vulkan::Instance instance(0,false);
    auto* dlss=Vulkan::Dlss::Get();
    if (!dlss) {
        std::puts("SKIP: optional DLSS DLLs are not beside the test executable");
        return 77;
    }
    if (instance.GetPhysicalDevice().getProperties().vendorID==0x10de) {
        std::puts("SKIP: this negative-control test requires a non-NVIDIA selected GPU");
        return 77;
    }
    assert(!dlss->Available());
    assert(dlss->Problem()=="not an NVIDIA GPU");
    const Vulkan::Dlss::FeatureDesc feature{1280,720,1920,1080,1,false,true};
    assert(!dlss->HasFeature(feature));
    assert(!dlss->CreateFeature({},feature));
    assert(!dlss->Evaluate({},{}));
    dlss->Shutdown();dlss->Shutdown();
    assert(!dlss->Available());
    std::puts("PASS: actual non-NVIDIA GPU cannot initialize/create/evaluate DLSS; shutdown remains safe");
}
