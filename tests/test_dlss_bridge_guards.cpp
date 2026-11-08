// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the actual optional Windows DLL. No NGX initialization or game launch.
#include <windows.h>
#include <cassert>
#include <cstdio>
#include "../gpu/dlss_bridge/ps4rt_dlss_bridge.h"

int wmain(int argc,wchar_t** argv) {
    assert(argc==2);
    auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(module);
    const auto get=reinterpret_cast<Ps4RtDlssGetApiFn>(GetProcAddress(module,"Ps4RtDlssGetApi"));
    const auto api=get?get():nullptr;
    assert(api && api->abi==PS4RT_DLSS_BRIDGE_ABI);
    unsigned count=9;const VkExtensionProperties* required=reinterpret_cast<const VkExtensionProperties*>(1);
    assert(!api->InstanceExtensions(nullptr,&required));
    assert(!api->InstanceExtensions(&count,nullptr));
    assert(!api->InstanceExtensions(&count,&required));assert(!count && !required);
    assert(!api->Initialize({}, {}, {}, nullptr, nullptr));
    assert(!api->CreateFeature({},nullptr));assert(!api->Evaluate({},nullptr));
    api->ReleaseFeature();api->Shutdown();api->Shutdown();
    assert(api->Configure(L".",L".",nullptr));
    assert(!api->DeviceExtensions({}, {},&count,&required));assert(!count && !required);
    assert(!api->DeviceExtensions({}, {},nullptr,&required));
    assert(!api->Initialize({}, {}, {},nullptr,nullptr));
    Ps4RtDlssFeature feature{1280,720,1920,1080,1,1,0,0};
    Ps4RtDlssEvaluate frame{};
    assert(!api->CreateFeature({},&feature));assert(!api->Evaluate({},&frame));
    api->Shutdown();
    FreeLibrary(module);
    std::puts("PASS: real DLSS bridge ABI, null queries, unavailable feature rejection, idempotent shutdown");
}
