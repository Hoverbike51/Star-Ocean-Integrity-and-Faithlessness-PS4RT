// SPDX-FileCopyrightText: Copyright 2026 IFreemz, PS4RT contributors
// SPDX-License-Identifier: MIT
//
// Stable C interface between PS4RT and the optional ps4rt_dlss.dll. The main
// executable contains no NVIDIA SDK code and continues to use FSR when the
// bridge or the NVIDIA runtime is not installed.

#pragma once

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4RT_DLSS_BRIDGE_ABI 2

typedef void (*Ps4RtDlssLogFn)(int warning, const char* message);

typedef struct Ps4RtDlssImage {
    VkImage image;
    VkImageView view;
    VkImageSubresourceRange range;
    VkFormat format;
    uint32_t width;
    uint32_t height;
} Ps4RtDlssImage;

typedef struct Ps4RtDlssFeature {
    uint32_t input_width;
    uint32_t input_height;
    uint32_t output_width;
    uint32_t output_height;
    int32_t quality;
    int32_t depth_inverted;
    uint32_t preset;
    int32_t hdr;
} Ps4RtDlssFeature;

typedef struct Ps4RtDlssEvaluate {
    Ps4RtDlssImage color;
    Ps4RtDlssImage depth;
    Ps4RtDlssImage motion;
    Ps4RtDlssImage output;
    float jitter_x;
    float jitter_y;
    int32_t reset;
    float frame_ms;
    float sharpness;
} Ps4RtDlssEvaluate;

typedef struct Ps4RtDlssApi {
    uint32_t abi;
    int32_t (*Configure)(const wchar_t* dll_directory, const wchar_t* data_directory,
                         Ps4RtDlssLogFn log);
    int32_t (*InstanceExtensions)(uint32_t* count, const VkExtensionProperties** extensions);
    int32_t (*DeviceExtensions)(VkInstance instance, VkPhysicalDevice physical, uint32_t* count,
                                const VkExtensionProperties** extensions);
    int32_t (*Initialize)(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                          PFN_vkGetInstanceProcAddr get_instance_proc,
                          PFN_vkGetDeviceProcAddr get_device_proc);
    int32_t (*CreateFeature)(VkCommandBuffer command, const Ps4RtDlssFeature* feature);
    int32_t (*Evaluate)(VkCommandBuffer command, const Ps4RtDlssEvaluate* evaluate);
    void (*ReleaseFeature)(void);
    void (*Shutdown)(void);
} Ps4RtDlssApi;

typedef const Ps4RtDlssApi* (*Ps4RtDlssGetApiFn)(void);

#ifdef __cplusplus
}
#endif
