// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_fsr_sdk24.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "ps4rt_settings.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <deque>
#include <vector>

// Keep AMD's public C ABI in its own translation unit and namespace. The
// existing source-v07 provider uses similarly named, incompatible descriptors.
namespace Sdk24 {
#include "../../../third_party/amd-fsr-sdk24/upscalers/include/ffx_upscale.h"
#include "../../../third_party/amd-fsr-sdk24/api/include/vk/ffx_api_vk.h"
}

namespace Vulkan {
struct FsrSdk24::Impl {
    const Instance& instance;
    Scheduler& scheduler;
    HMODULE effect{}, loader{};
    Sdk24::PfnFfxCreateContext create{};
    Sdk24::PfnFfxDestroyContext destroy{};
    Sdk24::PfnFfxQuery query{};
    Sdk24::PfnFfxDispatch dispatch{};
    Sdk24::ffxContext context{};
    Sdk24::ffxCreateBackendVKDesc backend{};
    Sdk24::ffxCreateContextDescUpscale desc{};
    Sdk24::ffxCreateContextDescUpscaleVersion api_version{};
    vk::detail::DynamicLoader vulkan;
    vk::detail::DispatchLoaderDynamic api;
    std::deque<u64> ticks;
    std::string problem, provider;
    bool available{}, inverted{}, exposure{}, hdr{};
    u32 width{}, height{};

    Impl(const Instance& i, Scheduler& s, std::filesystem::path directory)
        : instance(i), scheduler(s) {
        api.init(vulkan.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
        api.init(i.GetInstance()); api.init(i.GetDevice());
        if (directory.empty()) {
            std::vector<wchar_t> path(32768);
            const auto n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
            if(!n || n>=path.size()) {Fail("cannot locate application directory");return;}
            directory=std::filesystem::path(std::wstring(path.data(),n)).parent_path()/L"amd-fsr-sdk24";
        }
        // Never search the working directory or PATH for provider binaries.
        effect=LoadLibraryExW((directory/L"amd_fidelityfx_upscaler_vk.dll").c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!effect) {Fail("signed AMD Vulkan upscaler DLL missing or could not be loaded");return;}
        loader=LoadLibraryExW((directory/L"amd_fidelityfx_loader_vk.dll").c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!loader) {Fail("signed AMD Vulkan loader DLL missing or could not be loaded");return;}
        create=reinterpret_cast<Sdk24::PfnFfxCreateContext>(GetProcAddress(loader,"ffxCreateContext"));
        destroy=reinterpret_cast<Sdk24::PfnFfxDestroyContext>(GetProcAddress(loader,"ffxDestroyContext"));
        query=reinterpret_cast<Sdk24::PfnFfxQuery>(GetProcAddress(loader,"ffxQuery"));
        dispatch=reinterpret_cast<Sdk24::PfnFfxDispatch>(GetProcAddress(loader,"ffxDispatch"));
        if(!create||!destroy||!query||!dispatch) {Fail("AMD Vulkan loader API is incomplete");return;}
        backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.deviceContext={VkDevice(i.GetDevice()),VkPhysicalDevice(i.GetPhysicalDevice()),api.vkGetDeviceProcAddr};
        Sdk24::ffxQueryDescGetVersions versions{};
        versions.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versions.device=&backend.deviceContext;
        uint64_t count=0;versions.outputCount=&count;
        const auto result=query(nullptr,&versions.header);
        if(result!=Sdk24::FFX_API_RETURN_OK||!count||count>64) {
            Fail("no compatible AMD upscaling provider (code "+std::to_string(result)+")");return;
        }
        available=true;
        std::printf("Upscaler: AMD FSR SDK 2.4, %llu compatible provider(s) reported by SDK\n",static_cast<unsigned long long>(count));
    }
    void Fail(std::string reason) {
        if(reason!=problem) std::printf("Upscaler: AMD FSR SDK 2.4: %s\n",reason.c_str());
        problem=std::move(reason);
    }
    void Destroy(bool recording) {
        if(!context) return;
        // Only on a provider/size change, never a queue idle on every frame.
        if(recording) scheduler.WaitSubmitted();else scheduler.Finish();
        destroy(&context,nullptr);context=nullptr;ticks.clear();
    }
    ~Impl() {
        Destroy(false);
        if(loader) FreeLibrary(loader);
        if(effect) FreeLibrary(effect);
    }
    bool Create(const Fsr4Upscaler::Frame& f) {
        Destroy(true);
        width=f.output.width;height=f.output.height;inverted=f.depth_inverted;exposure=f.auto_exposure;hdr=f.hdr;
        desc={};desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        desc.header.pNext=&backend.header;
        backend.header.pNext=&api_version.header;
        api_version.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        api_version.version=FFX_UPSCALER_VERSION;
        desc.flags=Sdk24::FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
        if(hdr) desc.flags|=Sdk24::FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
        if(inverted) desc.flags|=Sdk24::FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
        if(exposure) desc.flags|=Sdk24::FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
        desc.maxRenderSize={width,height};desc.maxUpscaleSize={width,height};
        const auto result=create(&context,&desc.header,nullptr);
        if(result!=Sdk24::FFX_API_RETURN_OK||!context) {
            if(context) Destroy(true);
            available=false;Fail("context creation failed (code "+std::to_string(result)+")");return false;
        }
        Sdk24::ffxQueryGetProviderVersion version{};version.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if(query(&context,&version.header)==Sdk24::FFX_API_RETURN_OK&&version.versionName) provider="AMD FSR "+std::string(version.versionName);
        else provider="AMD FSR SDK 2.4";
        std::printf("Upscaler: AMD FSR SDK 2.4 active provider: %s; output %ux%u\n",provider.c_str(),width,height);
        std::printf("Upscaler: AMD FSR SDK inputs: color=%s, depth=%s, %s, %s depth\n",
            vk::to_string(f.color.format).c_str(),vk::to_string(f.depth.format).c_str(),
            hdr?"HDR":"SDR",inverted?"reversed":"forward");
        // Version text is copied and owned by the renderer, not by the unloaded DLL.
        Ps4RtSettings::SetFsrSdkProviderName(provider);
        return true;
    }
    bool Record(const Fsr4Upscaler::Frame& f) {
        if(!available) return false;
        if(!f.render_width||!f.render_height||f.render_width>f.output.width||f.render_height>f.output.height||
            f.color.width<f.render_width||f.color.height<f.render_height||f.depth.width<f.render_width||
            f.depth.height<f.render_height||f.motion.width<f.render_width||f.motion.height<f.render_height) {
            Fail("input dimensions are invalid for the selected output");return false;
        }
        // The SDK receives VkImage rather than the caller's VkImageView. An exact
        // format and depth/stencil usage are necessary for its internal views.
        const std::array inputs{f.color,f.depth,f.motion,f.output};
        for(const auto& image:inputs) {
            if(!image.image || Sdk24::ffxApiGetSurfaceFormatVK(VkFormat(image.format))==Sdk24::FFX_API_SURFACE_FORMAT_UNKNOWN) {
                available=false;Fail("unsupported or missing Vulkan input resource format");return false;
            }
        }
        if(!context||width!=f.output.width||height!=f.output.height||inverted!=f.depth_inverted||exposure!=f.auto_exposure||hdr!=f.hdr)
            if(!Create(f)) return false;
        // Match the SDK's bounded internal frame ring to submitted Vulkan work.
        while(!ticks.empty() && scheduler.IsFree(ticks.front())) ticks.pop_front();
        if(ticks.size()>=3) {scheduler.Wait(ticks.front());ticks.pop_front();}
        auto aspects=[](vk::Format format) {
            vk::ImageAspectFlags result;
            if(Sdk24::ffxApiIsDepthFormat(VkFormat(format))) result|=vk::ImageAspectFlagBits::eDepth;
            if(Sdk24::ffxApiIsStencilFormat(VkFormat(format))) result|=vk::ImageAspectFlagBits::eStencil;
            return result?result:vk::ImageAspectFlags(vk::ImageAspectFlagBits::eColor);
        };
        auto transition=[&](bool read) {
            std::array<vk::ImageMemoryBarrier2,3> barriers;
            const std::array images{f.color,f.depth,f.motion};
            for(size_t n=0;n<images.size();++n) barriers[n]={
                .srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
                .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout=read?vk::ImageLayout::eGeneral:vk::ImageLayout::eShaderReadOnlyOptimal,
                .newLayout=read?vk::ImageLayout::eShaderReadOnlyOptimal:vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                .image=images[n].image,.subresourceRange={aspects(images[n].format),0,1,0,1}};
            f.cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount=3,.pImageMemoryBarriers=barriers.data()},api);
        };
        auto resource=[](const Fsr4Upscaler::Image& image,bool output=false) {
            Sdk24::FfxApiResource r{};r.resource=reinterpret_cast<void*>(VkImage(image.image));
            r.description.type=Sdk24::FFX_API_RESOURCE_TYPE_TEXTURE2D;
            r.description.format=Sdk24::ffxApiGetSurfaceFormatVK(VkFormat(image.format));
            r.description.width=image.width;r.description.height=image.height;r.description.depth=1;r.description.mipCount=1;
            r.description.usage=output?Sdk24::FFX_API_RESOURCE_USAGE_UAV:Sdk24::FFX_API_RESOURCE_USAGE_READ_ONLY;
            if(Sdk24::ffxApiIsDepthFormat(VkFormat(image.format))) r.description.usage|=Sdk24::FFX_API_RESOURCE_USAGE_DEPTHTARGET;
            if(Sdk24::ffxApiIsStencilFormat(VkFormat(image.format))) r.description.usage|=Sdk24::FFX_API_RESOURCE_USAGE_STENCILTARGET;
            r.state=output?Sdk24::FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:Sdk24::FFX_API_RESOURCE_STATE_COMPUTE_READ;
            return r;
        };
        Sdk24::ffxDispatchDescUpscale d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList=reinterpret_cast<void*>(VkCommandBuffer(f.cmdbuf));
        d.color=resource(f.color);
        d.depth=resource(f.depth);
        d.motionVectors=resource(f.motion);
        d.output=resource(f.output,true);
        d.jitterOffset={f.jitter[0],f.jitter[1]};d.motionVectorScale={1,1};
        d.renderSize={f.render_width,f.render_height};d.upscaleSize={f.output.width,f.output.height};
        d.enableSharpening=f.sharpen;d.sharpness=std::clamp(f.sharpness,0.f,1.f);
        d.frameTimeDelta=std::clamp(f.frame_ms,1.f,1000.f);d.preExposure=1;d.reset=f.reset;
        d.cameraNear=f.near_plane;d.cameraFar=f.far_plane;d.cameraFovAngleVertical=f.vertical_fov;d.viewSpaceToMetersFactor=1;
        transition(true);
        const auto result=dispatch(&context,&d.header);
        transition(false);
        ticks.push_back(scheduler.CurrentTick());
        if(result!=Sdk24::FFX_API_RETURN_OK) {
            available=false;Fail("dispatch failed (code "+std::to_string(result)+")");return false;
        }
        problem.clear();return true;
    }
};
FsrSdk24::FsrSdk24(const Instance& i,Scheduler& s,const std::filesystem::path& p):impl(std::make_unique<Impl>(i,s,p)){}
FsrSdk24::~FsrSdk24()=default;
bool FsrSdk24::Available() const{return impl->available;}
bool FsrSdk24::Record(const Fsr4Upscaler::Frame& f){return impl->Record(f);}
const std::string& FsrSdk24::Problem() const{return impl->problem;}
const std::string& FsrSdk24::Provider() const{return impl->provider;}
}
