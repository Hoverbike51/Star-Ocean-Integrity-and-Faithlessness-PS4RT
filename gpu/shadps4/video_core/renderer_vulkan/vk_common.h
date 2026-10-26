// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Include vulkan-hpp header
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_STRUCT_SETTERS
#define VULKAN_HPP_HAS_SPACESHIP_OPERATOR
#define VULKAN_HPP_NO_EXCEPTIONS
// Define assert-on-result to nothing to instead return the result for our handling.
#define VULKAN_HPP_ASSERT_ON_RESULT

// vulkan-hpp changes DispatchLoaderBase's object layout when NDEBUG is absent.
// ps4rtgpu is a Release library, while the regression executables deliberately
// re-enable their own assertions.  Keeping NDEBUG stable just for this header
// prevents inline DispatchLoaderDynamic methods from different translation units
// from using different member offsets on Windows (an ODR/COMDAT crash).
#if !defined(NDEBUG)
#define PS4RT_RESTORE_NDEBUG_AFTER_VULKAN_HPP
#define NDEBUG
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
#include <vulkan/vulkan.hpp>
#pragma clang diagnostic pop
#if defined(PS4RT_RESTORE_NDEBUG_AFTER_VULKAN_HPP)
#undef NDEBUG
#undef PS4RT_RESTORE_NDEBUG_AFTER_VULKAN_HPP
#endif

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
