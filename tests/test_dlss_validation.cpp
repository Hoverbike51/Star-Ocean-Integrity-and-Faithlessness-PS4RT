// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include "../gpu/dlss_bridge/dlss_validation.h"
#include "../gpu/shim/ps4rt_scene_preset.h"

static Ps4RtDlssImage Image(uintptr_t id, unsigned w, unsigned h, VkFormat format,
                           VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT) {
    // Opaque handles are inspected only by the validation contract, never Vulkan.
    return {reinterpret_cast<VkImage>(id),reinterpret_cast<VkImageView>(id+16),
        {aspect,0,1,0,1},format,w,h};
}

int main() {
    using namespace So5Dlss;
    unsigned cases=0;
    for (auto output : {Ps4RtScene::Size{1280,720},{1920,1080},{2560,1440},
                        {3840,2160},{3440,1440},{5120,1440}}) {
        for (int quality=0;quality<5;++quality) {
            const auto input=Ps4RtScene::Input(quality,output.width,output.height);
            Ps4RtDlssFeature feature{input.width,input.height,output.width,output.height,quality,1,0,1};
            Ps4RtDlssEvaluate frame{
                Image(1,input.width,input.height,VK_FORMAT_R16G16B16A16_SFLOAT),
                Image(2,input.width,input.height,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_IMAGE_ASPECT_DEPTH_BIT),
                Image(3,input.width,input.height,VK_FORMAT_R16G16_SFLOAT),
                Image(4,output.width,output.height,VK_FORMAT_R16G16B16A16_SFLOAT),
                .25f,-.25f,1,16.667f,.2f};
            assert(!FeatureProblem(feature));assert(!FrameProblem(feature,frame));
            auto bad=feature;bad.input_width=output.width+1;assert(FeatureProblem(bad));
            bad=feature;bad.input_height=0;assert(FeatureProblem(bad));
            bad=feature;bad.quality=5;assert(FeatureProblem(bad));
            bad=feature;bad.quality=-1;assert(FeatureProblem(bad));
            if(quality) {bad=feature;bad.quality=0;assert(FeatureProblem(bad));}
            // Check every input/output independently; a valid neighbour cannot
            // conceal a missing or undersized resource during a live switch.
            for(unsigned which=0;which<4;++which) {
                const auto image=[&](Ps4RtDlssEvaluate& f)->Ps4RtDlssImage& {
                    return which==0?f.color:which==1?f.depth:which==2?f.motion:f.output;
                };
                auto changed=frame;image(changed).image=VK_NULL_HANDLE;assert(FrameProblem(feature,changed));
                changed=frame;image(changed).view=VK_NULL_HANDLE;assert(FrameProblem(feature,changed));
                changed=frame;--image(changed).height;assert(FrameProblem(feature,changed));
                changed=frame;--image(changed).width;assert(FrameProblem(feature,changed));
                changed=frame;image(changed).range.levelCount=2;assert(FrameProblem(feature,changed));
                changed=frame;image(changed).range.baseArrayLayer=1;assert(FrameProblem(feature,changed));
                changed=frame;image(changed).range.aspectMask=0;assert(FrameProblem(feature,changed));
                changed=frame;image(changed).width+=8;image(changed).height+=8;assert(!FrameProblem(feature,changed));
            }
            for(auto input_image:{frame.color.image,frame.depth.image,frame.motion.image}) {
                auto changed=frame;changed.output.image=input_image;assert(FrameProblem(feature,changed));
            }
            for(float nonfinite:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                auto changed=frame;changed.jitter_x=nonfinite;assert(FrameProblem(feature,changed));
                changed=frame;changed.jitter_y=nonfinite;assert(FrameProblem(feature,changed));
                changed=frame;changed.frame_ms=nonfinite;assert(FrameProblem(feature,changed));
                changed=frame;changed.sharpness=nonfinite;assert(FrameProblem(feature,changed));
            }
            auto changed=frame;changed.frame_ms=0;assert(FrameProblem(feature,changed));
            changed=frame;changed.sharpness=1.1f;assert(FrameProblem(feature,changed));
            changed=frame;changed.motion.format=VK_FORMAT_R8G8_UNORM;assert(FrameProblem(feature,changed));
            changed=frame;changed.output.format=VK_FORMAT_R8G8B8A8_UNORM;assert(FrameProblem(feature,changed));
            changed=frame;changed.depth.format=VK_FORMAT_R32_SFLOAT;assert(FrameProblem(feature,changed));
            changed=frame;changed.depth.format=VK_FORMAT_D32_SFLOAT;assert(!FrameProblem(feature,changed));
            changed=frame;changed.color.format=VK_FORMAT_R8G8B8A8_UNORM;assert(!FrameProblem(feature,changed));
            ++cases;
        }
    }
    std::printf("PASS: %u DLSS preset/extent contracts, live resource changes, aliasing and nonfinite guards; no NGX/GPU calls\n",cases);
}
