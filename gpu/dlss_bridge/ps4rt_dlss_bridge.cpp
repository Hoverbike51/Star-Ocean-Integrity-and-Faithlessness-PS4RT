// SPDX-FileCopyrightText: Copyright 2026 IFreemz (shadps4_dlss bridge), bbport contributors, ps4rt contributors
// SPDX-License-Identifier: MIT
//
// ps4rt_dlss.dll: the only part of the project that uses the NVIDIA DLSS (NGX) SDK. Built with
// MSVC (the SDK's libraries are MSVC only); the port loads it at run time.
// Adapted from Supermedo/bloodborne_pc @ 41fa920 (gpu/dlss_bridge/bbport_dlss_bridge.cpp), itself
// from IFreemz/shadPS4-Bloodborne-DLSS-FSR (dlss_bridge). Frame Generation is left out.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ps4rt_dlss_bridge.h" // Vulkan first: the NGX headers expect its types
#include "dlss_validation.h"

#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

namespace {

constexpr char ProjectId[] = "91a99667-060d-4d9b-93cb-f5df776a3710";
constexpr char EngineVersion[] = "star-ocean-5-windows-" SO5_DLSS_VERSION;

struct State {
    Ps4RtDlssLogFn log{};
    std::wstring dll_directory;
    std::wstring data_directory;
    const wchar_t* dll_path{};
    NVSDK_NGX_FeatureCommonInfo common{};
    NVSDK_NGX_FeatureDiscoveryInfo discovery{};
    NVSDK_NGX_Parameter* capabilities{};
    NVSDK_NGX_Parameter* parameters{};
    NVSDK_NGX_Handle* feature{};
    Ps4RtDlssFeature feature_desc{};
    std::vector<VkExtensionProperties> instance_extensions, device_extensions;
    VkDevice device{};
    VkPhysicalDevice approved_physical{};
    bool configured{};
    bool initialized{};
    bool available{};
} state;

template <typename... Args>
void Log(int warning, const char* format, Args... args) {
    if (!state.log)
        return;
    std::array<char, 1024> text{};
    std::snprintf(text.data(), text.size(), format, args...);
    state.log(warning, text.data());
}

bool Check(const char* operation, NVSDK_NGX_Result result) {
    if (NVSDK_NGX_FAILED(result)) {
        Log(1, "%s failed: 0x%08x", operation, static_cast<unsigned>(result));
        return false;
    }
    return true;
}

void NVSDK_CONV NgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    Log(0, "NGX: %s", message ? message : "");
}

int32_t Configure(const wchar_t* dll_directory, const wchar_t* data_directory, Ps4RtDlssLogFn log) {
    if (state.initialized) return 0; // Do not invalidate SDK-owned path pointers.
    state.approved_physical=VK_NULL_HANDLE;
    state.available=false;
    state.log = log;
    state.dll_directory = dll_directory ? dll_directory : L".";
    state.data_directory = data_directory ? data_directory : L".";
    state.dll_path = state.dll_directory.c_str();
    state.common.PathListInfo = {&state.dll_path, 1};
    state.common.LoggingInfo = {NgxLog, NVSDK_NGX_LOGGING_LEVEL_OFF, false};
    state.discovery.SDKVersion = NVSDK_NGX_Version_API;
    state.discovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    state.discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    state.discovery.Identifier.v.ProjectDesc = {ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                                                EngineVersion};
    state.discovery.ApplicationDataPath = state.data_directory.c_str();
    state.discovery.FeatureInfo = &state.common;
    state.configured = true;
    return 1;
}

int32_t InstanceExtensions(uint32_t* count, const VkExtensionProperties** extensions) {
    if (!count || !extensions) return 0;
    *count=0; *extensions=nullptr;
    VkExtensionProperties* required{};
    uint32_t required_count{};
    if (!state.configured ||
        !Check("Instance extension query",
               NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
                   &state.discovery, &required_count, &required)))
        return 0;
    if (required_count && !required) return 0;
    state.instance_extensions.clear();
    if (required_count) state.instance_extensions.assign(required, required + required_count);
    *count = uint32_t(state.instance_extensions.size());
    *extensions = state.instance_extensions.data();
    return 1;
}

int32_t DeviceExtensions(VkInstance instance, VkPhysicalDevice physical, uint32_t* count,
                         const VkExtensionProperties** extensions) {
    if (!count || !extensions) return 0;
    *count=0; *extensions=nullptr;
    if (!state.configured || !instance || !physical || state.initialized)
        return 0;
    state.approved_physical=VK_NULL_HANDLE;
    NVSDK_NGX_FeatureRequirement support{};
    if (!Check("Feature requirement query",
               NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, physical, &state.discovery,
                                                       &support)))
        return 0;
    if (support.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported) {
        Log(1, "DLSS is not supported on this GPU or driver (code 0x%x)",
            static_cast<unsigned>(support.FeatureSupported));
        return 0;
    }
    VkExtensionProperties* required{};
    uint32_t required_count{};
    if (!Check("Device extension query",
               NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
                   instance, physical, &state.discovery, &required_count, &required)))
        return 0;
    if (required_count && !required) return 0;
    state.device_extensions.clear();
    if (required_count) state.device_extensions.assign(required, required + required_count);
    state.approved_physical=physical;
    *count = uint32_t(state.device_extensions.size());
    *extensions = state.device_extensions.data();
    return 1;
}

void Shutdown();

int32_t Initialize(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                   PFN_vkGetInstanceProcAddr get_instance_proc,
                   PFN_vkGetDeviceProcAddr get_device_proc) {
    if (!state.configured || state.initialized || !instance || !physical || !device ||
        physical != state.approved_physical || !get_instance_proc || !get_device_proc)
        return 0;
    const auto get_properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        get_instance_proc(instance,"vkGetPhysicalDeviceProperties"));
    if (!get_properties) return 0;
    VkPhysicalDeviceProperties properties{};
    get_properties(physical,&properties);
    if (properties.vendorID != 0x10de) {
        Log(1,"DLSS requires a compatible NVIDIA GPU; selected vendor 0x%x refused",properties.vendorID);
        return 0;
    }
    if (!Check("NGX initialization",
               NVSDK_NGX_VULKAN_Init_with_ProjectID(
                   ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion,
                   state.data_directory.c_str(), instance, physical, device, get_instance_proc,
                   get_device_proc, &state.common)))
        return 0;
    state.initialized = true;
    state.device = device;
    if (!Check("Capability query", NVSDK_NGX_VULKAN_GetCapabilityParameters(&state.capabilities)) || !state.capabilities) {
        Shutdown();
        return 0;
    }
    int available{}, needs_driver{};
    if (!Check("DLSS availability query", NVSDK_NGX_Parameter_GetI(state.capabilities,
            NVSDK_NGX_Parameter_SuperSampling_Available,&available)) ||
        !Check("DLSS driver query", NVSDK_NGX_Parameter_GetI(state.capabilities,
            NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,&needs_driver))) {
        Shutdown();
        return 0;
    }
    if (needs_driver) {
        Log(1, "DLSS needs a newer NVIDIA driver");
        Shutdown();
        return 0;
    }
    if (!available) {
        Log(1, "DLSS Super Resolution is not available on this system");
        Shutdown();
        return 0;
    }
    state.available=true;
    return 1;
}

void ReleaseFeature() {
    if (state.feature) {
        NVSDK_NGX_VULKAN_ReleaseFeature(state.feature);
        state.feature = nullptr;
    }
    if (state.parameters) {
        NVSDK_NGX_VULKAN_DestroyParameters(state.parameters);
        state.parameters = nullptr;
    }
}

int32_t CreateFeature(VkCommandBuffer command, const Ps4RtDlssFeature* desc) {
    if (!state.available || !state.initialized || !state.capabilities || !desc || !command)
        return 0;
    if (const char* reason=So5Dlss::FeatureProblem(*desc)) {
        Log(1,"Feature refused: %s",reason);
        return 0;
    }
    ReleaseFeature();
    if (!Check("Parameter allocation", NVSDK_NGX_VULKAN_AllocateParameters(&state.parameters)))
        return 0;
    for (const char* key : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
                            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality})
        NVSDK_NGX_Parameter_SetUI(state.parameters, key, desc->preset);
    static constexpr std::array qualities{
        NVSDK_NGX_PerfQuality_Value_DLAA, NVSDK_NGX_PerfQuality_Value_MaxQuality,
        NVSDK_NGX_PerfQuality_Value_Balanced, NVSDK_NGX_PerfQuality_Value_MaxPerf,
        NVSDK_NGX_PerfQuality_Value_UltraPerformance};
    NVSDK_NGX_DLSS_Create_Params settings{};
    settings.Feature.InWidth = desc->input_width;
    settings.Feature.InHeight = desc->input_height;
    settings.Feature.InTargetWidth = desc->output_width;
    settings.Feature.InTargetHeight = desc->output_height;
    settings.Feature.InPerfQualityValue = qualities[desc->quality];
    settings.InFeatureCreateFlags =
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
        (desc->depth_inverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0) |
        (desc->hdr ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure
                   : 0);
    const auto result = NGX_VULKAN_CREATE_DLSS_EXT1(state.device, command, 1, 1, &state.feature,
                                                   state.parameters, &settings);
    if (!Check("DLSS feature creation", result) || !state.feature) {
        ReleaseFeature();
        return 0;
    }
    state.feature_desc = *desc;
    Log(0, "DLSS %ux%u -> %ux%u, quality %d, preset %u%s%s", desc->input_width,
        desc->input_height, desc->output_width, desc->output_height, desc->quality, desc->preset,
        desc->depth_inverted ? ", inverted depth" : "", desc->hdr ? ", HDR input" : "");
    return 1;
}

NVSDK_NGX_Resource_VK Wrap(const Ps4RtDlssImage& image, bool writable) {
    return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image, image.range,
                                                  image.format, image.width, image.height,
                                                  writable);
}

int32_t Evaluate(VkCommandBuffer command, const Ps4RtDlssEvaluate* evaluate) {
    if (!state.available || !state.feature || !state.parameters || !evaluate || !command)
        return 0;
    if (const char* reason=So5Dlss::FrameProblem(state.feature_desc,*evaluate)) {
        Log(1,"Frame refused: %s",reason);
        return 0;
    }
    auto color = Wrap(evaluate->color, false);
    auto depth = Wrap(evaluate->depth, false);
    auto motion = Wrap(evaluate->motion, false);
    auto output = Wrap(evaluate->output, true);
    NVSDK_NGX_VK_DLSS_Eval_Params parameters{};
    parameters.Feature.pInColor = &color;
    parameters.Feature.pInOutput = &output;
    parameters.Feature.InSharpness = evaluate->sharpness;
    parameters.pInDepth = &depth;
    parameters.pInMotionVectors = &motion;
    parameters.InRenderSubrectDimensions = {state.feature_desc.input_width,
                                            state.feature_desc.input_height};
    parameters.InJitterOffsetX = evaluate->jitter_x;
    parameters.InJitterOffsetY = evaluate->jitter_y;
    parameters.InReset = evaluate->reset;
    parameters.InMVScaleX = parameters.InMVScaleY = 1.0f;
    parameters.InPreExposure = parameters.InExposureScale = 1.0f;
    parameters.InFrameTimeDeltaInMsec = evaluate->frame_ms;
    return Check("DLSS evaluation", NGX_VULKAN_EVALUATE_DLSS_EXT(command, state.feature,
                                                                state.parameters, &parameters))
               ? 1
               : 0;
}

void Shutdown() {
    state.available=false;
    state.approved_physical=VK_NULL_HANDLE;
    ReleaseFeature();
    if (state.capabilities) {
        NVSDK_NGX_VULKAN_DestroyParameters(state.capabilities);
        state.capabilities = nullptr;
    }
    if (state.initialized) {
        NVSDK_NGX_VULKAN_Shutdown1(state.device);
        state.initialized = false;
        state.device = VK_NULL_HANDLE;
    }
}

const Ps4RtDlssApi api{PS4RT_DLSS_BRIDGE_ABI, Configure,     InstanceExtensions,
                       DeviceExtensions,      Initialize,    CreateFeature,
                       Evaluate,              ReleaseFeature, Shutdown};

} // namespace

extern "C" __declspec(dllexport) const Ps4RtDlssApi* Ps4RtDlssGetApi() {
    return &api;
}
