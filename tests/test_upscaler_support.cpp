// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <array>
#include <fstream>
#include <iterator>
#include "ps4rt_upscaler_support.h"
#include "ps4rt_fsr3_policy.h"
#include "ps4rt_scene_preset.h"
#include "ps4rt_title_screen.h"

int main(int argc,char** argv) {
    using namespace Ps4RtUpscaler;
    // Regression: a depth-only draw during save loading has no fragment shader.
    // This executes the same recognition predicate as Rasterizer::DrawRecord.
    struct Stage {uint64_t pgm_hash;};
    const Stage vertex{0x1234},title{0x4eab1bc3},scene{0x13d64dc6};
    const std::array<const Stage*,2> depth_only{&vertex,nullptr};
    assert(!Ps4RtTitle::UiShader(depth_only[1]));
    assert(!Ps4RtTitle::UiShader(&scene));
    assert(Ps4RtTitle::UiShader(&title));
    assert(!Ps4RtTitle::ConsumeFrame());
    std::puts("PASS: title recognition accepts a missing fragment shader on depth-only draws");
    if(argc>1) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> fixture{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        assert(Ps4RtTitle::Atlas(2048,2048,37,13,2048,fixture.data(),fixture.size()));
        fixture[10263040]^=1;
        assert(!Ps4RtTitle::Atlas(2048,2048,37,13,2048,fixture.data(),fixture.size()));
        std::puts("PASS: local title-atlas fixture recognized; changed artwork rejected");
    }
    assert(!Ps4RtTitle::ConsumeFrame()); Ps4RtTitle::MarkDraw();assert(Ps4RtTitle::ConsumeFrame());assert(!Ps4RtTitle::ConsumeFrame());
    std::vector<uint8_t> blank(16777216);
    assert(!Ps4RtTitle::Atlas(2048,2048,37,13,2048,blank.data(),blank.size()));
    assert(!Ps4RtTitle::Atlas(1920,1080,37,13,1920,nullptr,0));
    for(const auto output:{Ps4RtScene::Size{3840,2160},Ps4RtScene::Size{1280,720}}) for(int preset=0;preset<5;++preset) {
        const auto input=Ps4RtScene::Input(preset,output.width,output.height);
        assert(Ps4RtScene::ModelForInput(input.width,input.height,output.width,output.height)==preset);
    }
    assert(Ps4RtScene::ModelForInput(1920,1080,2560,1440)==5);
    assert(Ps4RtScene::ForOutput(-1,1280,720)==1);
    assert(Ps4RtScene::ForOutput(-1,1920,1080)==-1);
    assert(Ps4RtScene::ForOutput(-1,1440,1080,true)==-1);
    assert(Ps4RtScene::ForOutput(-1,1280,960,true)==1);
    assert((Ps4RtScene::RequestedInput(true,-1,1080,1440,1080,true)==Ps4RtScene::Size{1440,1080}));
    assert((Ps4RtScene::NativeInput(2160,1600,1200,true)==Ps4RtScene::Size{2880,2160}));
    assert((Ps4RtScene::NativeInput(2160,1920,1200,true)==Ps4RtScene::Size{3456,2160}));
    for(int preset=0;preset<5;++preset) {
        const auto input=Ps4RtScene::Input(preset,1280,720);assert(input.width<=1280 && input.height<=720);
    }
    for(const auto name:{"NVIDIA GeForce GTX 1080","NVIDIA GeForce GTX 1080 Ti","NVIDIA GeForce RTX 3070","NVIDIA GeForce RTX 5070"}) assert(Fsr3Recommended(0x10de,name));
    for(const auto name:{"NVIDIA GeForce GTX 1050","NVIDIA GeForce GTX 1070","NVIDIA GeForce GTX 1660","NVIDIA GeForce RTX 3050","NVIDIA TITAN X","Unknown"}) assert(!Fsr3Recommended(0x10de,name));
    for(const auto name:{"AMD Radeon RX 5700","AMD Radeon RX 6600","AMD Radeon RX 9070 XT"}) assert(Fsr3Recommended(0x1002,name));
    assert(!Fsr3Recommended(0x1002,"AMD Radeon RX 5600 XT"));
    assert(Fsr3Recommended(0x8086,"Intel(R) Arc(TM) A750 Graphics"));
    assert(!Fsr3Recommended(0x8086,"Intel(R) Arc(TM) A380 Graphics"));
    const uint8_t uuid[16]={1,2,3,4};
    const auto key=Fsr3DeviceKey(0x10de,0x1234,uuid);
    assert(Fsr3Allowed(true,false,key,key));
    assert(!Fsr3Allowed(false,true,key,key));
    assert(!Fsr3Allowed(true,false,key,"different"));
    assert(!Fsr3Allowed(true,false,"",""));
    assert((Ps4RtScene::Input(-1,3840,2160)==Ps4RtScene::Size{1920,1080}));
    assert((Ps4RtScene::Input(0,3840,2160)==Ps4RtScene::Size{3840,2160}));
    assert((Ps4RtScene::Input(1,3840,2160)==Ps4RtScene::Size{2560,1440}));
    assert((Ps4RtScene::Input(2,3840,2160)==Ps4RtScene::Size{2258,1270}));
    assert((Ps4RtScene::Input(3,3840,2160)==Ps4RtScene::Size{1920,1080}));
    assert((Ps4RtScene::Input(4,3840,2160)==Ps4RtScene::Size{1280,720}));
    using Ps4RtUpscaler::Fsr4Requirements;
    Fsr4Requirements complete{true,true,true,true,true,true,true,true};
    assert(complete.Supported());
    const std::array fields{&Fsr4Requirements::float16,&Fsr4Requirements::int8,&Fsr4Requirements::int16,
        &Fsr4Requirements::dot_product,&Fsr4Requirements::extended_storage,
        &Fsr4Requirements::linear_derivatives,&Fsr4Requirements::compute_quads,&Fsr4Requirements::workgroup_256};
    for(auto field:fields) { auto missing=complete; missing.*field=false; assert(!missing.Supported()); }
    assert(!Fsr4Requirements{}.Supported());
    // A real read-only device probe. No window, logical device, shaders or game files.
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
    VkInstance instance{};
    if(vkCreateInstance(&info,nullptr,&instance)==VK_SUCCESS) {
        uint32_t count=0;
        if(vkEnumeratePhysicalDevices(instance,&count,nullptr)==VK_SUCCESS) {
            std::vector<VkPhysicalDevice> devices(count);
            if(vkEnumeratePhysicalDevices(instance,&count,devices.data())==VK_SUCCESS)
                for(auto device:devices) {
                    VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(device,&properties);
                    if(properties.apiVersion<VK_API_VERSION_1_3) continue;
                    const auto supported=Ps4RtUpscaler::ProbeFsr4(device);
                    std::printf("GPU capability probe: %s, FSR 4 v07 %s\n",properties.deviceName,
                                supported.Supported()?"eligible":"refused");
                }
        }
        vkDestroyInstance(instance,nullptr);
    }
    std::puts("PASS: all eight FSR 4 prerequisites are mandatory, independent of GPU vendor");
}
