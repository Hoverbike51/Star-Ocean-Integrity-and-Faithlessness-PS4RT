// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Include the vulkan platform specific header

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vector>
#include <fmt/ranges.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "sdl_window.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_dlss.h"

// vk_common.h deliberately uses VK_NO_PROTOTYPES, but Windows builds link the
// loader import library and use this one entry point to seed vulkan-hpp.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                           const char* name);


namespace Vulkan {

static const char* const VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";
static const char* const CRASH_DIAGNOSTIC_LAYER_NAME = "VK_LAYER_LUNARG_crash_diagnostic";

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) {

    spdlog::level level{};
    switch (severity) {
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
        level = spdlog::level::err;
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
        level = spdlog::level::info;
        break;
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
    case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
        level = spdlog::level::debug;
        break;
    default:
        level = spdlog::level::info;
    }

    LOG_GENERIC(Common::Log::Class::Render_Vulkan, level, "{}: {}",
                callback_data->pMessageIdName ? callback_data->pMessageIdName : "<null>",
                callback_data->pMessage ? callback_data->pMessage : "<null>");

    return VK_FALSE;
}

vk::SurfaceKHR CreateSurface(vk::Instance instance, const Frontend::WindowSDL& emu_window) {
    const auto& window_info = emu_window.GetWindowInfo();
    vk::SurfaceKHR surface{};

    if (window_info.type == Frontend::WindowSystemType::Windows) {
        const vk::Win32SurfaceCreateInfoKHR win32_ci = {
            .hinstance = nullptr,
            .hwnd = static_cast<HWND>(window_info.render_surface),
        };

        if (instance.createWin32SurfaceKHR(&win32_ci, nullptr, &surface) != vk::Result::eSuccess) {
            LOG_CRITICAL(Render_Vulkan, "Failed to initialize Win32 surface");
            UNREACHABLE();
        }
    }

    if (!surface) {
        LOG_CRITICAL(Render_Vulkan, "Presentation not supported on this platform");
        UNREACHABLE();
    }

    return surface;
}

static auto GetLayerExtensions(std::vector<const char*>&& extensions,
                               const std::vector<const char*>& layers) {
    auto all_missing_vk_settings = true;

    for (const auto& layer_name : layers) {
        const auto [layer_properties_result, layer_extensions] =
            vk::enumerateInstanceExtensionProperties(std::string(layer_name));
        if (layer_properties_result != vk::Result::eSuccess) {
            LOG_ERROR(Render_Vulkan, "Failed to query extension properties of {}: {}", layer_name,
                      vk::to_string(layer_properties_result));
        }
        auto found = false;
        for (const auto& extension : layer_extensions) {
            if (extension.extensionName == std::string_view(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME)) {
                found = true;
                all_missing_vk_settings = false;
                break;
            }
        }
        if (!found) {
            LOG_ERROR(Render_Vulkan, "Settings for layer {} not available.", layer_name);
        }
    }

    if (!all_missing_vk_settings) {
        extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
    }

    return extensions;
}

std::vector<const char*> GetInstanceExtensions(Frontend::WindowSystemType window_type,
                                               bool enable_debug_utils) {
    const auto [properties_result, properties] = vk::enumerateInstanceExtensionProperties();
    if (properties_result != vk::Result::eSuccess || properties.empty()) {
        LOG_ERROR(Render_Vulkan, "Failed to query extension properties: {}",
                  vk::to_string(properties_result));
        return {};
    }

    // Add the windowing system specific extension
    std::vector<const char*> extensions;
    extensions.reserve(7);

    switch (window_type) {
    case Frontend::WindowSystemType::Headless:
        break;
    case Frontend::WindowSystemType::Windows:
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
        break;
    default:
        LOG_ERROR(Render_Vulkan, "Presentation not supported on this platform");
        break;
    }

    if (window_type != Frontend::WindowSystemType::Headless) {
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }

    if (EmulatorSettings.IsHdrAllowed()) {
        extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
    }

    if (enable_debug_utils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Sanitize extension list
    std::erase_if(extensions, [&](const char* extension) -> bool {
        const auto it =
            std::find_if(properties.begin(), properties.end(), [extension](const auto& prop) {
                return std::strcmp(extension, prop.extensionName) == 0;
            });

        if (it == properties.end()) {
            LOG_INFO(Render_Vulkan, "Candidate instance extension {} is not available", extension);
            return true;
        }
        return false;
    });

    return extensions;
}

std::vector<const char*> GetInstanceLayers(bool enable_validation, bool enable_crash_diagnostic) {
    const auto [properties_result, properties] = vk::enumerateInstanceLayerProperties();
    if (properties_result != vk::Result::eSuccess || properties.empty()) {
        LOG_ERROR(Render_Vulkan, "Failed to query layer properties: {}",
                  vk::to_string(properties_result));
        return {};
    }

    std::vector<const char*> layers;
    layers.reserve(2);

    if (enable_validation) {
        layers.push_back(VALIDATION_LAYER_NAME);
    }
    if (enable_crash_diagnostic) {
        layers.push_back(CRASH_DIAGNOSTIC_LAYER_NAME);
    }

    // Sanitize layer list
    std::erase_if(layers, [&](const char* layer) -> bool {
        const auto it = std::ranges::find_if(properties, [layer](const auto& prop) {
            return std::strcmp(layer, prop.layerName) == 0;
        });
        if (it == properties.end()) {
            LOG_ERROR(Render_Vulkan, "Requested layer {} is not available", layer);
            return true;
        }
        return false;
    });

    return layers;
}

vk::UniqueInstance CreateInstance(Frontend::WindowSystemType window_type, bool enable_validation,
                                  bool enable_crash_diagnostic) {
    LOG_INFO(Render_Vulkan, "Creating vulkan instance");


    // Vulkan::Vulkan links the Windows loader explicitly. Using its import avoids a
    // DynamicLoader/LoadLibrary lookup that can return null in MinGW GUI executables.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    const auto [available_version_result, available_version] =
        VULKAN_HPP_DEFAULT_DISPATCHER.vkEnumerateInstanceVersion
            ? vk::enumerateInstanceVersion()
            : vk::ResultValue(vk::Result::eSuccess, VK_API_VERSION_1_0);
    ASSERT_MSG(available_version_result == vk::Result::eSuccess,
               "Failed to query Vulkan API version: {}", vk::to_string(available_version_result));
    ASSERT_MSG(available_version >= TargetVulkanApiVersion,
               "Vulkan {}.{} is required, but only {}.{} is supported by instance!",
               VK_VERSION_MAJOR(TargetVulkanApiVersion), VK_VERSION_MINOR(TargetVulkanApiVersion),
               VK_VERSION_MAJOR(available_version), VK_VERSION_MINOR(available_version));

    const auto layers = GetInstanceLayers(enable_validation, enable_crash_diagnostic);
    auto extensions = GetLayerExtensions(GetInstanceExtensions(window_type, true), layers);
    // ps4rt: DLSS (optional bridge DLL) needs its own instance extensions.
    if (Dlss* dlss = Dlss::Get()) {
        dlss->AppendInstanceExtensions(extensions);
    }

    const vk::ApplicationInfo application_info = {
        .pApplicationName = "shadPS4",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "shadPS4 Vulkan",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = available_version,
    };

    const std::string extensions_string = fmt::format("{}", fmt::join(extensions, ", "));
    const std::string layers_string = fmt::format("{}", fmt::join(layers, ", "));
    LOG_INFO(Render_Vulkan, "Enabled instance extensions: {}", extensions_string);
    LOG_INFO(Render_Vulkan, "Enabled instance layers: {}", layers_string);

    // Validation settings
    vk::Bool32 enable_core = EmulatorSettings.IsVkValidationCoreEnabled() ? vk::True : vk::False;
    vk::Bool32 enable_sync = EmulatorSettings.IsVkValidationSyncEnabled() ? vk::True : vk::False;
    vk::Bool32 enable_gpuav = EmulatorSettings.IsVkValidationGpuEnabled() ? vk::True : vk::False;

    // Crash diagnostics settings
    static const auto crash_diagnostic_path =
        Common::FS::GetUserPathString(Common::FS::PathType::LogDir);
    const char* log_path = crash_diagnostic_path.c_str();
    vk::Bool32 enable_force_barriers = vk::True;

    const std::array layer_setings = {
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "validate_core",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_core,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "validate_sync",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_sync,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "syncval_submit_time_validation",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_sync,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "gpuav_enable",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_gpuav,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "gpuav_descriptor_checks",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_gpuav,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "gpuav_buffers_validation",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_gpuav,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "gpuav_indirect_draws_buffers",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_gpuav,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "gpuav_indirect_dispatches_buffers",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_gpuav,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "gpuav_indirect_trace_rays_buffers",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_gpuav,
        },
        vk::LayerSettingEXT{
            .pLayerName = VALIDATION_LAYER_NAME,
            .pSettingName = "gpuav_buffer_copies",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_gpuav,
        },
        vk::LayerSettingEXT{
            .pLayerName = "lunarg_crash_diagnostic",
            .pSettingName = "output_path",
            .type = vk::LayerSettingTypeEXT::eString,
            .valueCount = 1,
            .pValues = &log_path,
        },
        vk::LayerSettingEXT{
            .pLayerName = "lunarg_crash_diagnostic",
            .pSettingName = "sync_after_commands",
            .type = vk::LayerSettingTypeEXT::eBool32,
            .valueCount = 1,
            .pValues = &enable_force_barriers,
        },
    };

    vk::StructureChain<vk::InstanceCreateInfo, vk::LayerSettingsCreateInfoEXT> instance_ci_chain = {
        vk::InstanceCreateInfo{
            .pApplicationInfo = &application_info,
            .enabledLayerCount = static_cast<u32>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<u32>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
        },
        vk::LayerSettingsCreateInfoEXT{
            .settingCount = layer_setings.size(),
            .pSettings = layer_setings.data(),
        },
    };

    auto [instance_result, instance] = vk::createInstanceUnique(instance_ci_chain.get());
    ASSERT_MSG(instance_result == vk::Result::eSuccess, "Failed to create instance: {}",
               vk::to_string(instance_result));

    // Pass the entry point again: some MinGW link combinations do not preserve
    // the member set by the global-only init across createInstanceUnique().
    VULKAN_HPP_DEFAULT_DISPATCHER.init(static_cast<VkInstance>(*instance), vkGetInstanceProcAddr);

    return std::move(instance);
}

vk::UniqueDebugUtilsMessengerEXT CreateDebugCallback(vk::Instance instance) {
    const vk::DebugUtilsMessengerCreateInfoEXT msg_ci = {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = DebugUtilsCallback,
    };
    auto [messenger_result, messenger] = instance.createDebugUtilsMessengerEXTUnique(msg_ci);
    ASSERT_MSG(messenger_result == vk::Result::eSuccess, "Failed to create debug callback: {}",
               vk::to_string(messenger_result));
    return std::move(messenger);
}

} // namespace Vulkan
