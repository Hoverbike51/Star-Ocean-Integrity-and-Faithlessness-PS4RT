// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstring>
#include "../gpu/dlss_bridge/ps4rt_dlss_bridge.h"
inline bool So5ProbeDlss(VkInstance instance,VkPhysicalDevice physical,const std::filesystem::path& bin) {
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
    if(properties.vendorID!=0x10de||!std::filesystem::is_regular_file(bin/L"nvngx_dlss.dll"))return false;
    static HMODULE module=nullptr;
    if(!module)module=LoadLibraryExW((bin/L"ps4rt_dlss.dll").c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)return false;
    auto get=reinterpret_cast<Ps4RtDlssGetApiFn>(GetProcAddress(module,"Ps4RtDlssGetApi"));
    const auto* api=get?get():nullptr;bool result=false;
    if(api&&api->abi==PS4RT_DLSS_BRIDGE_ABI&&api->Configure&&api->DeviceExtensions) {
        uint32_t count=0;const VkExtensionProperties* required=nullptr;
        result=api->Configure(bin.c_str(),bin.c_str(),nullptr)&&api->DeviceExtensions(instance,physical,&count,&required)&&count<=128&&(!count||required);
        uint32_t available_count=0;
        if(result&&vkEnumerateDeviceExtensionProperties(physical,nullptr,&available_count,nullptr)==VK_SUCCESS) {
            std::vector<VkExtensionProperties> available(available_count);
            result=vkEnumerateDeviceExtensionProperties(physical,nullptr,&available_count,available.data())==VK_SUCCESS;
            for(uint32_t i=0;i<count&&result;++i)result=std::any_of(available.begin(),available.end(),[&](const auto& e){return !std::strcmp(e.extensionName,required[i].extensionName)&&e.specVersion>=required[i].specVersion;});
        }else result=false;
    }
    // NGX can keep process state after discovery. Retain the module until process exit.
    return result;
}
