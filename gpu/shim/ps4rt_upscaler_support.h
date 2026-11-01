// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace Ps4RtUpscaler {
// Requirements audited against the shipped v07 SPIR-V, including the SPD
// 256-thread workgroup and the pre-pass's compute subgroup quad operations.
// Capability-based for AMD, NVIDIA and Intel; the vendor name is not sufficient.
struct Fsr4Requirements {
    bool float16{}, int8{}, int16{}, dot_product{}, extended_storage{};
    bool linear_derivatives{}, compute_quads{}, workgroup_256{};
    bool Supported() const {
        return float16 && int8 && int16 && dot_product && extended_storage &&
               linear_derivatives && compute_quads && workgroup_256;
    }
};

// The launcher probes through Vulkan's C API; the renderer uses its already
// collected features and dynamic dispatcher (VK_NO_PROTOTYPES).
#ifndef VK_NO_PROTOTYPES
inline bool ProbeFsr3(VkPhysicalDevice device) {
    VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(device,&features);
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(device,&properties);
    return properties.apiVersion>=VK_API_VERSION_1_3 && properties.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU &&
        features.shaderStorageImageWriteWithoutFormat && properties.limits.maxComputeWorkGroupInvocations>=256;
}
inline Fsr4Requirements ProbeFsr4(VkPhysicalDevice device) {
    uint32_t count=0;
    if (vkEnumerateDeviceExtensionProperties(device,nullptr,&count,nullptr)!=VK_SUCCESS) return {};
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(device,nullptr,&count,extensions.data())!=VK_SUCCESS) return {};
    const bool derivatives=std::any_of(extensions.begin(),extensions.end(),[](const auto& e) {
        return std::strcmp(e.extensionName,VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME)==0;
    });
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivative{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.pNext=derivatives?&derivative:nullptr;
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,&v13};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&v12};
    vkGetPhysicalDeviceFeatures2(device,&features);
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&subgroup};
    vkGetPhysicalDeviceProperties2(device,&properties);
    const auto& limits=properties.properties.limits;
    return {bool(v12.shaderFloat16),bool(v12.shaderInt8),bool(features.features.shaderInt16),
            bool(v13.shaderIntegerDotProduct),bool(features.features.shaderStorageImageExtendedFormats),
            derivatives && bool(derivative.computeDerivativeGroupLinear),
            bool(subgroup.supportedStages&VK_SHADER_STAGE_COMPUTE_BIT) &&
                bool(subgroup.supportedOperations&VK_SUBGROUP_FEATURE_QUAD_BIT),
            limits.maxComputeWorkGroupInvocations>=256 && limits.maxComputeWorkGroupSize[0]>=256 &&
                limits.maxComputeWorkGroupSize[1]>=8};
}
#endif
} // namespace Ps4RtUpscaler
