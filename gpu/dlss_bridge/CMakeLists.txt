# SPDX-FileCopyrightText: Copyright 2026 IFreemz (shadps4_dlss bridge), bbport contributors, ps4rt contributors
# SPDX-License-Identifier: MIT
#
# ps4rt_dlss.dll links the NVIDIA DLSS SDK (MSVC only) and is built separately from the port
# (build-windows.ps1 -Dlss does this; adapted from Supermedo/bloodborne_pc @ 41fa920):
#   cmake -S gpu/dlss_bridge -B out/dlss-bridge -G "Visual Studio 17 2022" -A x64 \
#         -DDLSS_SDK_ROOT=<github.com/NVIDIA/DLSS checkout> -DVULKAN_INCLUDE=<Vulkan-Headers include>
#   cmake --build out/dlss-bridge --config Release

cmake_minimum_required(VERSION 3.20)
project(ps4rt_dlss LANGUAGES CXX)

set(DLSS_SDK_ROOT "" CACHE PATH "NVIDIA DLSS SDK root (contains include/ and lib/)")
set(VULKAN_INCLUDE "" CACHE PATH "Vulkan-Headers include directory")

if (NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The DLSS bridge is Windows x64 only")
endif()
if (NOT EXISTS "${DLSS_SDK_ROOT}/include/nvsdk_ngx_vk.h")
    message(FATAL_ERROR "Set DLSS_SDK_ROOT to the NVIDIA DLSS SDK (github.com/NVIDIA/DLSS)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# nvsdk_ngx_s.lib is the static-CRT (/MT) variant: no Visual C++ runtime install needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_library(ps4rt_dlss SHARED ps4rt_dlss_bridge.cpp ps4rt_dlss_bridge.h)
# Do not put the MinGW system include root on MSVC's search path. Only Vulkan
# headers are toolchain-neutral; MinGW CRT headers would shadow MSVC's CRT.
if(NOT EXISTS "${VULKAN_INCLUDE}/vulkan/vulkan.h")
    message(FATAL_ERROR "VULKAN_INCLUDE must contain vulkan/vulkan.h")
endif()
file(COPY "${VULKAN_INCLUDE}/vulkan" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/vulkan-sdk/include")
if(EXISTS "${VULKAN_INCLUDE}/vk_video")
    file(COPY "${VULKAN_INCLUDE}/vk_video" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/vulkan-sdk/include")
endif()
target_include_directories(ps4rt_dlss PRIVATE "${DLSS_SDK_ROOT}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan-sdk/include")
target_link_libraries(ps4rt_dlss PRIVATE "${DLSS_SDK_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
target_compile_definitions(ps4rt_dlss PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" SO5_DLSS_VERSION LIMIT_COUNT 1)
string(REPLACE "." ";" SO5_VERSION_PARTS "${SO5_DLSS_VERSION}")
list(GET SO5_VERSION_PARTS 0 SO5_MAJOR)
list(GET SO5_VERSION_PARTS 1 SO5_MINOR)
list(GET SO5_VERSION_PARTS 2 SO5_PATCH)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
configure_file(bridge.rc.in "${CMAKE_CURRENT_BINARY_DIR}/bridge.rc" @ONLY)
target_sources(ps4rt_dlss PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/bridge.rc")
target_compile_definitions(ps4rt_dlss PRIVATE SO5_DLSS_VERSION="${SO5_DLSS_VERSION}")

include(CTest)
if(BUILD_TESTING)
    add_executable(dlss-bridge-guards-test EXCLUDE_FROM_ALL ../../tests/test_dlss_bridge_guards.cpp)
    target_include_directories(dlss-bridge-guards-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vulkan-sdk/include")
    target_compile_definitions(dlss-bridge-guards-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(dlss-bridge-guards-test PRIVATE /UNDEBUG)
    add_dependencies(dlss-bridge-guards-test ps4rt_dlss)
    add_test(NAME dlss-bridge-guards COMMAND dlss-bridge-guards-test "$<TARGET_FILE:ps4rt_dlss>")
endif()
