// SPDX-License-Identifier: GPL-2.0-or-later
// Real Vulkan allocation, proxy writes, resolves and view retirement. No game data.
#include <cassert>
#include <cstdio>
#include <array>
#include <memory>
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/texture_cache/blit_helper.h"
#include "video_core/texture_cache/image_view.h"
#include "ps4rt_scene_preset.h"
#include <vk_mem_alloc.h>
int main() {
    using namespace Vulkan;using namespace VideoCore;
    Instance instance(0,false);Scheduler scheduler(instance);Runtime runtime(instance,scheduler);
    static vk::detail::DynamicLoader loader;vk::detail::DispatchLoaderDynamic dispatch;
    dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    dispatch.init(instance.GetInstance());dispatch.init(instance.GetDevice());
    Common::SlotVector<ImageView> views;
    ImageInfo info{};info.type=AmdGpu::ImageType::Color2D;info.size={1920,1080,1};
    info.resources={1,1};info.num_samples=info.guest_samples=1;info.num_bits=32;
    info.pixel_format=vk::Format::eR8G8B8A8Unorm;info.pitch=1920;
    info.guest_address=0x10000;info.guest_size=1920*1080*4;
    info.mips_layout[0]={info.guest_size,1920,1080,0};
    Image color(instance,runtime,views,info);
    auto half_info=info;half_info.size={960,540,1};half_info.pitch=960;half_info.guest_size=960*540*4;
    half_info.mips_layout[0]={half_info.guest_size,960,540,0};
    Image half(instance,runtime,views,half_info);
    auto depth_info=info;depth_info.props.is_depth=true;depth_info.pixel_format=vk::Format::eD32Sfloat;
    Image depth(instance,runtime,views,depth_info);
    std::array<Image*,3> images{&color,&half,&depth};
    SceneTargets targets(instance,scheduler,runtime,[&](ImageId id,u64 uid)->Image* {
        if(!id || id.index>=images.size()) return nullptr;
        auto* image=images[id.index];return !uid || image->image_uid==uid?image:nullptr;
    });
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
    for(unsigned i=0;i<images.size();++i) {
        auto* image=images[i];targets.Observe(ImageId{i});
        runtime.Transit(image,vk::ImageLayout::eTransferDstOptimal,vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferWrite);
        runtime.FlushBarriers();
        if(i==2) scheduler.CommandBuffer().clearDepthStencilImage(image->GetImage(),vk::ImageLayout::eTransferDstOptimal,{.depth=.5f},
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth,0,1,0,1},dispatch);
        else scheduler.CommandBuffer().clearColorImage(image->GetImage(),vk::ImageLayout::eTransferDstOptimal,
            vk::ClearColorValue(std::array{.1f,.2f,.3f,1.f}),range,dispatch);
    }
    constexpr size_t bytes=1920*1080*4;VkBuffer buffer{};VmaAllocation allocation{};VmaAllocationInfo mapped{};
    const VkBufferCreateInfo bc{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo ac{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    assert(vmaCreateBuffer(instance.GetAllocator(),&bc,&ac,&buffer,&allocation,&mapped)==VK_SUCCESS);
    for(const auto output:std::array<Ps4RtScene::Size,6>{{{3840,2160},{3440,1440},{5120,1440},{1920,1080},{1600,1200},{1920,1200}}})
    for(const auto preset:{1,2,3,4,0,-1,720,1080,1440,2160}) {
        const bool wide=output.width*9!=output.height*16; // any adjusted 3D projection
        const auto requested=preset>4?Ps4RtScene::NativeInput(preset,output.width,output.height,wide):
            Ps4RtScene::Input(preset,output.width,output.height,wide);
        const auto generation=targets.Generation();const auto old=targets.Size();
        assert(targets.TrySetSize({requested.width,requested.height}));
        assert(targets.Size().width==requested.width && targets.Size().height==requested.height);
        assert(targets.Generation()==generation+(old!=targets.Size()));
        ImageViewInfo view;view.format=info.pixel_format;
        const auto proxy=targets.Attachment(ImageId{0},view);
        assert(proxy.view && proxy.image!=color.GetImage() && targets.ProxyCurrent(color));
        assert(targets.ProvenRasterCurrent(color));
        assert(targets.ProxySize(half).width==requested.width/2);
        const auto depth_size=targets.ProxySize(depth);assert(depth_size.width==requested.width);
        ImageViewInfo depth_view;depth_view.format=depth_info.pixel_format;
        const auto dp=targets.Attachment(ImageId{2},depth_view);assert(dp.view && targets.RasterCurrent(depth) && !targets.ProxyCurrent(depth));
        targets.NativeAccess(depth,vk::AccessFlagBits2::eShaderRead); // depth resolve is a real blit
        assert(targets.ProvenRasterCurrent(depth));
        const auto read=targets.Read(ImageId{0},view,vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferWrite);
        scheduler.CommandBuffer().clearColorImage(read.image,read.layout,vk::ClearColorValue(std::array{.25f,.5f,.75f,1.f}),range,dispatch);
        runtime.Transit(&color,vk::ImageLayout::eTransferSrcOptimal,vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
        runtime.FlushBarriers();
        const vk::BufferImageCopy copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={1920,1080,1}};
        scheduler.CommandBuffer().copyImageToBuffer(color.GetImage(),vk::ImageLayout::eTransferSrcOptimal,buffer,copy,dispatch);
        scheduler.Finish();assert(vmaInvalidateAllocation(instance.GetAllocator(),allocation,0,VK_WHOLE_SIZE)==VK_SUCCESS);
        const auto* data=static_cast<const unsigned char*>(mapped.pMappedData);
        for(const size_t pixel:{size_t(0),size_t(541*1920+971),size_t(1920*1080-1)}) {
            assert(data[pixel*4]>=63 && data[pixel*4]<=64);assert(data[pixel*4+1]>=127 && data[pixel*4+1]<=128);
            assert(data[pixel*4+2]>=191 && data[pixel*4+2]<=192);assert(data[pixel*4+3]==255);
        }
        // Filling a 4K image from guest 1080p pixels must never count as native raster proof.
        targets.NativeAccess(color,vk::AccessFlagBits2::eShaderWrite);
        assert(!targets.ProvenRasterCurrent(color));
        (void)targets.Read(ImageId{0},view);
        assert(targets.RasterCurrent(color) && !targets.ProvenRasterCurrent(color));
        const auto before=targets.Generation();assert(!targets.TrySetSize({0,1}));
        assert(!targets.TrySetSize({instance.GetMaxFramebufferWidth()+1,1080}));
        assert(targets.Generation()==before && targets.Size().width==requested.width);
        std::printf("PASS: real color/depth %ux%u, native resolve and rejected invalid transition\n",requested.width,requested.height);
    }
    // Render a one-pixel marker outside the original guest extent, then copy
    // it 1:1 to a real 4K output. A 1080p intermediate cannot preserve this.
    assert(targets.TrySetSize({3840,2160}));
    ImageViewInfo view;view.format=info.pixel_format;
    const auto native4k=targets.Attachment(ImageId{0},view);
    scheduler.EndRendering();
    const vk::RenderingAttachmentInfo attachment{.imageView=native4k.view,.imageLayout=native4k.layout,
        .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore,
        .clearValue=vk::ClearValue(vk::ClearColorValue(std::array{0.f,0.f,0.f,1.f}))};
    const vk::RenderingInfo rendering{.renderArea={{0,0},{3840,2160}},.layerCount=1,
        .colorAttachmentCount=1,.pColorAttachments=&attachment};
    auto cmd=scheduler.CommandBuffer();cmd.beginRendering(rendering,dispatch);
    const vk::ClearAttachment white{.aspectMask=vk::ImageAspectFlagBits::eColor,.colorAttachment=0,
        .clearValue=vk::ClearValue(vk::ClearColorValue(std::array{1.f,1.f,1.f,1.f}))};
    const vk::ClearRect corner{.rect={{3839,2159},{1,1}},.baseArrayLayer=0,.layerCount=1};
    cmd.clearAttachments(white,corner,dispatch);cmd.endRendering(dispatch);
    UniqueImage output(instance.GetDevice(),instance.GetAllocator());
    output.Create({.imageType=vk::ImageType::e2D,.format=info.pixel_format,.extent={3840,2160,1},
        .mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
        .usage=vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst});
    const auto transition=[&](vk::Image image,vk::ImageLayout before,vk::ImageLayout after) {
        const vk::ImageMemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask=vk::AccessFlagBits2::eTransferRead|vk::AccessFlagBits2::eTransferWrite,
            .oldLayout=before,.newLayout=after,.image=image,.subresourceRange=range};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b},dispatch);
    };
    transition(native4k.image,native4k.layout,vk::ImageLayout::eTransferSrcOptimal);
    transition(output.image,vk::ImageLayout::eUndefined,vk::ImageLayout::eTransferDstOptimal);
    const vk::ImageCopy exact{.srcSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
        .dstSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.extent={3840,2160,1}};
    cmd.copyImage(native4k.image,vk::ImageLayout::eTransferSrcOptimal,output.image,
        vk::ImageLayout::eTransferDstOptimal,exact,dispatch);
    transition(output.image,vk::ImageLayout::eTransferDstOptimal,vk::ImageLayout::eTransferSrcOptimal);
    auto readback_info=bc;readback_info.size=3840ull*2160*4;
    VkBuffer full{};VmaAllocation full_alloc{};VmaAllocationInfo full_map{};
    assert(vmaCreateBuffer(instance.GetAllocator(),&readback_info,&ac,&full,&full_alloc,&full_map)==VK_SUCCESS);
    const vk::BufferImageCopy full_copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={3840,2160,1}};
    cmd.copyImageToBuffer(output.image,vk::ImageLayout::eTransferSrcOptimal,full,full_copy,dispatch);
    scheduler.Finish();assert(vmaInvalidateAllocation(instance.GetAllocator(),full_alloc,0,VK_WHOLE_SIZE)==VK_SUCCESS);
    const auto* pixels=static_cast<const unsigned char*>(full_map.pMappedData);
    const size_t last=(3840ull*2160-1)*4;
    assert(pixels[last]==255 && pixels[last+1]==255 && pixels[last+2]==255);
    assert(pixels[last-4]==0 && pixels[last-3]==0 && pixels[last-2]==0);
    assert(pixels[0]==0 && pixels[3]==255);
    vmaDestroyBuffer(instance.GetAllocator(),full,full_alloc);
    std::puts("PASS: real 3840x2160 attachment rendering and 1:1 output preserve an isolated bottom-right pixel (no upscaler)");
    // Restore the proxy layout contract before its normal resolve/destruction.
    cmd=scheduler.CommandBuffer();
    transition(native4k.image,vk::ImageLayout::eTransferSrcOptimal,native4k.layout);
    scheduler.Finish();
    // Resolving a single 4K colour pixel into its 1080p compute intermediate
    // must average the four source pixels, not select one with a point filter.
    runtime.Transit(&color,vk::ImageLayout::eTransferSrcOptimal,
        vk::PipelineStageFlagBits2::eTransfer,vk::AccessFlagBits2::eTransferRead);
    runtime.FlushBarriers();
    const vk::BufferImageCopy native_copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
        .imageExtent={1920,1080,1}};
    scheduler.CommandBuffer().copyImageToBuffer(color.GetImage(),vk::ImageLayout::eTransferSrcOptimal,
        buffer,native_copy,dispatch);
    scheduler.Finish();
    assert(vmaInvalidateAllocation(instance.GetAllocator(),allocation,0,VK_WHOLE_SIZE)==VK_SUCCESS);
    const auto* resolved=static_cast<const unsigned char*>(mapped.pMappedData);
    const size_t native_last=(1920ull*1080-1)*4;
    for(size_t c=0;c<3;++c) assert(resolved[native_last+c]>=63 && resolved[native_last+c]<=65);
    assert(resolved[native_last+3]==255 && resolved[native_last-4]==0);
    std::puts("PASS: 4K colour resolve filters subpixel coverage instead of point sampling");
    vmaDestroyBuffer(instance.GetAllocator(),buffer,allocation);
}
