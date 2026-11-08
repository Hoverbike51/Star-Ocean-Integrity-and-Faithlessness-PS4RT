// SPDX-License-Identifier: GPL-2.0-or-later
// Real Vulkan regression: compare unchanged interior against the shipped v07
// shader, then check every border pixel against a constant-colour reference.
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include "video_core/host_shaders/fsr4_rcas_comp.h"
#include <vk_mem_alloc.h>

float halfFloat(uint16_t h) {
    const float value = std::ldexp(float(h & 1023), -24);
    const unsigned exponent = (h >> 10) & 31;
    const float result = exponent ? (exponent == 31 ? INFINITY :
        std::ldexp(1.0f + float(h & 1023) / 1024, int(exponent) - 15)) : value;
    return h & 32768 ? -result : result;
}
int main(int argc, char** argv) {
    using namespace Vulkan;
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    assert(file);
    std::vector<uint32_t> original(size_t(file.tellg()) / 4);
    file.seekg(0); file.read(reinterpret_cast<char*>(original.data()), original.size() * 4);
    assert(file);
    Instance instance(0, false); Scheduler scheduler(instance);
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic api;
    api.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    api.init(instance.GetInstance()); api.init(instance.GetDevice());
    const auto device = instance.GetDevice(); const auto allocator = instance.GetAllocator();
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    const std::array bindings{
        vk::DescriptorSetLayoutBinding{6, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{18, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{32, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{43, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}};
    auto layout = Check(device.createDescriptorSetLayout({.bindingCount=4, .pBindings=bindings.data()}, nullptr, api));
    auto pipelineLayout = Check(device.createPipelineLayout({.setLayoutCount=1, .pSetLayouts=&layout}, nullptr, api));
    const std::array poolSizes{vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 2},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1}};
    auto pool = Check(device.createDescriptorPool({.maxSets=1, .poolSizeCount=3, .pPoolSizes=poolSizes.data()}, nullptr, api));
    auto set = Check(device.allocateDescriptorSets({.descriptorPool=pool, .descriptorSetCount=1, .pSetLayouts=&layout}, api))[0];
    std::array<vk::Pipeline, 2> pipelines;
    for (unsigned i=0; i<2; ++i) {
        const auto* code = i ? FSR4_RCAS_COMP : original.data();
        const size_t bytes = i ? sizeof(FSR4_RCAS_COMP) : original.size()*4;
        auto module = Check(device.createShaderModule({.codeSize=bytes, .pCode=code}, nullptr, api));
        pipelines[i] = Check(device.createComputePipeline({}, {
            .stage={.stage=vk::ShaderStageFlagBits::eCompute, .module=module, .pName="main"},
            .layout=pipelineLayout}, nullptr, api));
        device.destroyShaderModule(module, nullptr, api);
    }
    const auto barrier=[&](vk::Image image, vk::ImageLayout from, vk::ImageLayout to) {
        const vk::ImageMemoryBarrier2 b{
            .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask=vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask=vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .oldLayout=from, .newLayout=to,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=image, .subresourceRange=range};
        scheduler.CommandBuffer().pipelineBarrier2({.imageMemoryBarrierCount=1, .pImageMemoryBarriers=&b}, api);
    };
    for (auto size : {vk::Extent2D{32,32}, {31,17}, {1920,1080}, {3440,1440}, {3840,2160}}) {
        const unsigned w=size.width, h=size.height, sw=(w+7)&~7u, sh=(h+7)&~7u;
        const VkBufferCreateInfo bi{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=VkDeviceSize(sw)*sh*16+256,
            .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
        const VmaAllocationCreateInfo ai{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
        VkBuffer staging{}, readback{}; VmaAllocation sa{}, ra{}; VmaAllocationInfo si{}, ri{};
        assert(vmaCreateBuffer(allocator,&bi,&ai,&staging,&sa,&si)==VK_SUCCESS);
        assert(vmaCreateBuffer(allocator,&bi,&ai,&readback,&ra,&ri)==VK_SUCCESS);
        auto* pixels=reinterpret_cast<float*>(static_cast<char*>(si.pMappedData)+256);
        struct Constants { std::array<uint32_t,4> config; float preExposure; std::array<uint32_t,3> pad; };
        *static_cast<Constants*>(si.pMappedData)={{std::bit_cast<uint32_t>(std::exp2(-1.6f)),0,0,0},1.25f,{}};
        VideoCore::UniqueImage input(device,allocator), output(device,allocator), exposure(device,allocator);
        std::array<vk::ImageView,3> views;
        std::array<VideoCore::UniqueImage*,3> images{&input,&output,&exposure};
        for(unsigned i=0;i<3;++i) {
            images[i]->Create({.imageType=vk::ImageType::e2D,
                .format=i==1?vk::Format::eR16G16B16A16Sfloat:vk::Format::eR32G32B32A32Sfloat,
                .extent={i==2?1:i==1?w:sw,i==2?1:i==1?h:sh,1}, .mipLevels=1,.arrayLayers=1,
                .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
                .usage=vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst|
                       vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage});
            views[i]=Check(device.createImageView({.image=images[i]->image,.viewType=vk::ImageViewType::e2D,
                .format=i==1?vk::Format::eR16G16B16A16Sfloat:vk::Format::eR32G32B32A32Sfloat,
                .subresourceRange=range},nullptr,api));
            barrier(images[i]->image,vk::ImageLayout::eUndefined,vk::ImageLayout::eGeneral);
        }
        const std::array infos{vk::DescriptorImageInfo{{},views[2],vk::ImageLayout::eGeneral},
            vk::DescriptorImageInfo{{},views[0],vk::ImageLayout::eGeneral},
            vk::DescriptorImageInfo{{},views[1],vk::ImageLayout::eGeneral}};
        const vk::DescriptorBufferInfo ubo{staging,0,32};
        std::array<vk::WriteDescriptorSet,4> writes;
        for(unsigned i=0;i<4;++i) writes[i]={.dstSet=set,.dstBinding=bindings[i].binding,.descriptorCount=1,
            .descriptorType=bindings[i].descriptorType,.pImageInfo=i<3?&infos[i]:nullptr,.pBufferInfo=i==3?&ubo:nullptr};
        device.updateDescriptorSets(writes,{},api);
        scheduler.CommandBuffer().clearColorImage(exposure.image,vk::ImageLayout::eGeneral,
            vk::ClearColorValue(std::array<float,4>{0.75f,0,0,0}),range,api);
        barrier(exposure.image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral);
        for(unsigned pattern=0;pattern<5;++pattern) {
            static_cast<Constants*>(si.pMappedData)->config[0]=std::bit_cast<uint32_t>(
                std::exp2(pattern==4?0.0f:pattern==3?-2.0f:-1.6f));
            for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x) {
                const auto offset=(size_t(y)*sw+x)*4;
                for(unsigned c=0;c<3;++c) pixels[offset+c]=(x>=w||y>=h)?4.0f:
                    pattern==2?0.0f:pattern==3?1.0f:pattern==4?(((x/8+y/8)&1)?0.7f:0.05f):
                    pattern==1?0.1f+0.4f*float(x+y+c)/float(w+h+2):0.2f+0.1f*c;
                pixels[offset+3]=1;
            }
            assert(vmaFlushAllocation(allocator,sa,0,VK_WHOLE_SIZE)==VK_SUCCESS);
            barrier(input.image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral);
            scheduler.CommandBuffer().copyBufferToImage(staging,input.image,vk::ImageLayout::eGeneral,
                vk::BufferImageCopy{.bufferOffset=256,.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={sw,sh,1}},api);
            barrier(input.image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral);
            std::vector<uint16_t> reference;
            unsigned oldBadBorders=0;
            for(unsigned shader=0;shader<2;++shader) {
                barrier(output.image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral);
                scheduler.CommandBuffer().clearColorImage(output.image,vk::ImageLayout::eGeneral,
                    vk::ClearColorValue(std::array<float,4>{-9,-9,-9,-9}),range,api);
                barrier(output.image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral);
                auto cmd=scheduler.CommandBuffer();
                cmd.bindPipeline(vk::PipelineBindPoint::eCompute,pipelines[shader],api);
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,pipelineLayout,0,set,{},api);
                cmd.dispatch((w+15)/16,(h+15)/16,1,api);
                barrier(output.image,vk::ImageLayout::eGeneral,vk::ImageLayout::eGeneral);
                cmd.copyImageToBuffer(output.image,vk::ImageLayout::eGeneral,readback,
                    vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={w,h,1}},api);
                scheduler.Finish();
                assert(vmaInvalidateAllocation(allocator,ra,0,VK_WHOLE_SIZE)==VK_SUCCESS);
                const auto* result=static_cast<const uint16_t*>(ri.pMappedData);
                if(!shader)reference.assign(result,result+size_t(w)*h*4);
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(unsigned c=0;c<4;++c) {
                    const auto offset=(size_t(y)*w+x)*4+c;
                    float value=halfFloat(result[offset]);
                    if(shader)assert(std::isfinite(value)&&value>=0);
                    if(shader&&x&&y&&x+1<w&&y+1<h) {
                        const auto expected=halfFloat(reference[offset]);
                        assert(std::abs(value-expected)<=0.002f*std::max(1.0f,std::abs(expected)));
                    }
                    if(pattern==0) {
                        float expected=c==3?1.0f:(std::exp((0.2f+0.1f*c)*8.5178804397583f)-1)*0.006666666828095913f/0.6f;
                        bool bad=!std::isfinite(value)||std::abs(value-expected)>0.002f;
                        if(shader)assert(!bad);
                        else if(!x||!y||x+1==w||y+1==h)oldBadBorders+=bad;
                    }
                }
            }
            if(!pattern) { assert(oldBadBorders); std::printf("%ux%u: repaired %u original bad border components\n",w,h,oldBadBorders); }
        }
        for(auto view:views)device.destroyImageView(view,nullptr,api);
        vmaDestroyBuffer(allocator,staging,sa);vmaDestroyBuffer(allocator,readback,ra);
    }
    for(auto pipeline:pipelines)device.destroyPipeline(pipeline,nullptr,api);
    device.destroyDescriptorPool(pool,nullptr,api);device.destroyPipelineLayout(pipelineLayout,nullptr,api);
    device.destroyDescriptorSetLayout(layout,nullptr,api);
    std::puts("FSR 4 RCAS: constant/black/white/gradient/checkerboard, 3 sharpness levels, all edges, poisoned padding, interior reference passed");
}
