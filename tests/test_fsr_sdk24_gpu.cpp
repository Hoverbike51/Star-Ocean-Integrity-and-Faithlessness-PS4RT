// SPDX-License-Identifier: GPL-2.0-or-later
// Real signed-provider dispatch, preset changes, output changes and readback.
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "video_core/renderer_vulkan/vk_fsr_sdk24.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include <vk_mem_alloc.h>
static float Half(uint16_t h) {
    unsigned e=(h>>10)&31;float v=e?(e==31?INFINITY:std::ldexp(1.f+float(h&1023)/1024,int(e)-15)):std::ldexp(float(h&1023),-24);
    return h&32768?-v:v;
}
int main(int argc,char** argv) {
    using namespace Vulkan;
    Instance instance(0,false);Scheduler scheduler(instance);
    vk::detail::DynamicLoader loader;vk::detail::DispatchLoaderDynamic api;
    api.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    api.init(instance.GetInstance());api.init(instance.GetDevice());
    const auto device=instance.GetDevice();const auto allocator=instance.GetAllocator();
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
    auto aspects=[](vk::Format format) {
        if(format==vk::Format::eD32SfloatS8Uint)
            return vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth|vk::ImageAspectFlagBits::eStencil);
        if(format==vk::Format::eD32Sfloat) return vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth);
        return vk::ImageAspectFlags(vk::ImageAspectFlagBits::eColor);
    };
    auto barrier=[&](vk::Image image,vk::ImageLayout from,vk::ImageLayout to,
                     vk::Format format=vk::Format::eR16G16B16A16Sfloat) {
        const vk::ImageMemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
            .oldLayout=from,.newLayout=to,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={aspects(format),0,1,0,1}};
        scheduler.CommandBuffer().pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b},api);
    };
    {
        const auto missing=std::filesystem::temp_directory_path()/
            ("so5-sdk-missing-"+std::to_string(GetCurrentProcessId()));
        assert(!std::filesystem::exists(missing));
        FsrSdk24 absent(instance,scheduler,missing);
        assert(!absent.Available() && !absent.Problem().empty());
    }
    FsrSdk24 sdk(instance,scheduler,argc>1?std::filesystem::path(argv[1]):std::filesystem::path{});
    if(!sdk.Available()) {std::printf("SKIP: %s\n",sdk.Problem().c_str());return 77;}
    assert(!sdk.Record(Fsr4Upscaler::Frame{}));
    assert(sdk.Available()); // Invalid input is rejected without poisoning subsequent valid work.
    // Re-create contexts on output changes; vary input size without allocating a context each frame.
    for(const auto output:{vk::Extent2D{1280,720},{1440,1080},{1728,1080},{1920,1080},{2560,1080},{2560,720},{3440,1440},{3840,2160}}) {
        // Include native attachment depth and tonemapped color, not just float color images.
        for(int path=0;path<3;++path) {
        const bool hdr=path!=2;
        const auto depth_format=path==0?vk::Format::eR32Sfloat:
            path==1?vk::Format::eD32SfloatS8Uint:vk::Format::eD32Sfloat;
        std::array<VideoCore::UniqueImage,4> images{VideoCore::UniqueImage(device,allocator),VideoCore::UniqueImage(device,allocator),
            VideoCore::UniqueImage(device,allocator),VideoCore::UniqueImage(device,allocator)};
        std::array<vk::ImageView,4> views;
        const std::array formats{hdr?vk::Format::eR16G16B16A16Sfloat:vk::Format::eR8G8B8A8Unorm,
            depth_format,vk::Format::eR16G16Sfloat,vk::Format::eR16G16B16A16Sfloat};
        for(size_t n=0;n<4;++n) {
            const bool native_depth=n==1&&path!=0;
            const auto usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|
                vk::ImageUsageFlagBits::eTransferDst|(native_depth?vk::ImageUsageFlagBits::eDepthStencilAttachment:vk::ImageUsageFlagBits::eStorage);
            images[n].Create({.imageType=vk::ImageType::e2D,.format=formats[n],.extent={output.width,output.height,1},
                .mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
                .usage=usage});
            views[n]=Check(device.createImageView({.image=images[n].image,.viewType=vk::ImageViewType::e2D,.format=formats[n],.subresourceRange={native_depth?vk::ImageAspectFlagBits::eDepth:vk::ImageAspectFlagBits::eColor,0,1,0,1}},nullptr,api));
            barrier(images[n].image,vk::ImageLayout::eUndefined,vk::ImageLayout::eGeneral,formats[n]);
            if(native_depth) scheduler.CommandBuffer().clearDepthStencilImage(images[n].image,
                vk::ImageLayout::eGeneral,vk::ClearDepthStencilValue{.5f,0},vk::ImageSubresourceRange{aspects(formats[n]),0,1,0,1},api);
            else
            scheduler.CommandBuffer().clearColorImage(images[n].image,vk::ImageLayout::eGeneral,
                vk::ClearColorValue(std::array<float,4>{n==0?.25f:n==1?.5f:0.f,n==0?.5f:0.f,n==0?.75f:0.f,1.f}),range,api);
            barrier(images[n].image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral,formats[n]);
        }
        const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=VkDeviceSize(output.width)*output.height*8,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        const VmaAllocationCreateInfo ai{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
        VkBuffer buffer{};VmaAllocation allocation{};VmaAllocationInfo info{};
        assert(vmaCreateBuffer(allocator,&bi,&ai,&buffer,&allocation,&info)==VK_SUCCESS);
        for(int preset=0;preset<5;++preset) {
            const float divisor=std::array{1.f,1.5f,1.7f,2.f,3.f}[preset];
            const auto w=u32(output.width/divisor),h=u32(output.height/divisor);
            Fsr4Upscaler::Frame frame{.cmdbuf=scheduler.CommandBuffer(),
                .color={images[0].image,views[0],output.width,output.height,formats[0]},
                .depth={images[1].image,views[1],output.width,output.height,formats[1]},
                .motion={images[2].image,views[2],output.width,output.height,formats[2]},
                .output={images[3].image,views[3],output.width,output.height,formats[3]},
                .render_width=w,.render_height=h,.preset=preset,.jitter={0,0},.frame_ms=16.667f,
                .near_plane=.1f,.far_plane=1000.f,.vertical_fov=1.f,.sharpness=.2f,.sharpen=true,.reset=true,.auto_exposure=true,.depth_inverted=path!=0,.hdr=hdr};
            if(!sdk.Record(frame)) {std::printf("FAIL: %s\n",sdk.Problem().c_str());return 1;}
            // Keep the context/history alive for a following frame, with no CPU queue wait.
            frame.cmdbuf=scheduler.CommandBuffer();frame.reset=false;
            if(!sdk.Record(frame)) {std::printf("FAIL temporal continuation: %s\n",sdk.Problem().c_str());return 1;}
            barrier(images[3].image,vk::ImageLayout::eGeneral,vk::ImageLayout::eTransferSrcOptimal);
            const vk::BufferImageCopy copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={output.width,output.height,1}};
            scheduler.CommandBuffer().copyImageToBuffer(images[3].image,vk::ImageLayout::eTransferSrcOptimal,buffer,copy,api);
            barrier(images[3].image,vk::ImageLayout::eTransferSrcOptimal,vk::ImageLayout::eGeneral);
            scheduler.Finish();vmaInvalidateAllocation(allocator,allocation,0,VK_WHOLE_SIZE);
            const auto* pixels=static_cast<const uint16_t*>(info.pMappedData);
            for(uint64_t n=0;n<uint64_t(output.width)*output.height*4;++n) assert(std::isfinite(Half(pixels[n])));
            const auto center=(uint64_t(output.height/2)*output.width+output.width/2)*4;
            assert(Half(pixels[center])>.01f && Half(pixels[center+2])>.01f);
            std::printf("SDK reset + continuation/readback passed: %ux%u -> %ux%u, %s, %s (%s)\n",
                w,h,output.width,output.height,hdr?"HDR":"SDR",vk::to_string(depth_format).c_str(),sdk.Provider().c_str());
        }
        scheduler.Finish();vmaDestroyBuffer(allocator,buffer,allocation);
        for(auto view:views) device.destroyImageView(view,nullptr,api);
        }
    }
}
