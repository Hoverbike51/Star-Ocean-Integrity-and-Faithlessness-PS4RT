# ps4rt GPU library: shadPS4 video core + shader recompiler + Gnm/VideoOut
# drivers, adapted to the PS4RT runtime through shims in shim/.
cmake_minimum_required(VERSION 3.24)
project(ps4rtgpu LANGUAGES C CXX)
if (NOT WIN32)
    message(FATAL_ERROR "This source checkout targets native Windows 10/11 x64. Use MSYS2 CLANG64.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" PS4RT_VERSION)
string(STRIP "${PS4RT_VERSION}" PS4RT_VERSION)
if (NOT PS4RT_VERSION MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)$")
    message(FATAL_ERROR "VERSION must contain major.minor.patch")
endif()
set(PS4RT_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PS4RT_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PS4RT_VERSION_PATCH "${CMAKE_MATCH_3}")
configure_file(../packaging/windows/version.h.in include/ps4rt_version.h @ONLY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/include")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Frame pointers keep the loader's thread dumps (watchdog, SIGUSR2 sampling) walkable.
add_compile_options(-fno-omit-frame-pointer)
if (WIN32)
    # Release binaries must not expose the absolute path of the developer's checkout through
    # __FILE__, assertions, diagnostics, or debug metadata. Keep stable project-relative paths
    # regardless of where the repository is cloned or built.
    get_filename_component(PS4RT_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    file(TO_CMAKE_PATH "${PS4RT_REPOSITORY_ROOT}" PS4RT_REPOSITORY_ROOT)
    add_compile_options(
        "-ffile-prefix-map=${PS4RT_REPOSITORY_ROOT}=PS4RT"
        "-fmacro-prefix-map=${PS4RT_REPOSITORY_ROOT}=PS4RT"
        "-fdebug-prefix-map=${PS4RT_REPOSITORY_ROOT}=PS4RT")

    # FidelityFX sources use a few MSVC CRT spellings even when built with clang/MinGW.
    # Force the compatibility header and a single vulkan-hpp platform layout for this
    # directory and all third-party subdirectories. Mixing layouts lets a COMDAT copy of
    # DispatchLoaderDynamic::init use the wrong member offsets at run time.
    add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/shim/ps4rt_win_compat.h)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR VK_ENABLE_BETA_EXTENSIONS
                            WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ps4rt: link-time optimization (PS4RT_LTO, default on) and profile-guided optimization
# (PS4RT_PGO: off, generate = instrumented build writing into PS4RT_PGO_DIR, use = build with that
# profile). Covers ps4rtgpu and the static libraries linked into it. No -march: retain
# compatibility across supported Windows x64 processors.
option(PS4RT_LTO "Link-time optimization of ps4rtgpu" ON)
set(PS4RT_PGO "off" CACHE STRING "Profile-guided optimization: off, generate, use")
set(PS4RT_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo" CACHE PATH "Profile directory")
if (PS4RT_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ps4rt_ipo_ok OUTPUT ps4rt_ipo_error)
    if (ps4rt_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(WARNING "LTO unavailable: ${ps4rt_ipo_error}")
    endif()
endif()
if (PS4RT_PGO STREQUAL "generate")
    # Atomic counters: the GPU, recording, preparation and copy threads run the same code.
    add_compile_options(-fprofile-generate=${PS4RT_PGO_DIR} -fprofile-update=atomic)
    add_link_options(-fprofile-generate=${PS4RT_PGO_DIR})
    add_compile_definitions(PS4RT_PGO_GENERATE)
elseif (PS4RT_PGO STREQUAL "use")
    # Partial training keeps unprofiled code optimized normally; counters from concurrent
    # threads can be slightly inconsistent (correction). Functions changed since the profile
    # (or built differently for it, ps4rtgpu_init) are compiled without it.
    add_compile_options(-fprofile-use=${PS4RT_PGO_DIR} -fprofile-partial-training
                        -fprofile-correction -Wno-missing-profile -Wno-coverage-mismatch)
endif()

find_package(Vulkan REQUIRED)
find_package(fmt REQUIRED)
find_package(Boost REQUIRED)
# ps4rt (Windows): MSYS2 has no magic_enum/miniz packages; the submodules are used instead.
find_package(magic_enum QUIET)
if (NOT magic_enum_FOUND)
    add_library(magic_enum INTERFACE)
    add_library(magic_enum::magic_enum ALIAS magic_enum)
    target_include_directories(magic_enum INTERFACE third_party/magic_enum/include
                                                    third_party/magic_enum/include/magic_enum)
endif()
find_package(tsl-robin-map REQUIRED)
find_package(VulkanMemoryAllocator CONFIG QUIET)
if (NOT VulkanMemoryAllocator_FOUND)
    # MSYS2 ships the header (include/vma/vk_mem_alloc.h) without a CMake package.
    find_path(VMA_INCLUDE_DIR vk_mem_alloc.h PATH_SUFFIXES vma REQUIRED)
    add_library(VulkanMemoryAllocator INTERFACE)
    add_library(GPUOpen::VulkanMemoryAllocator ALIAS VulkanMemoryAllocator)
    target_include_directories(VulkanMemoryAllocator INTERFACE ${VMA_INCLUDE_DIR})
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
if (WIN32)
    # The MSYS2 SDL3 pkg-config file adds -mwindows.  ps4rt is also a command-line
    # loader whose diagnostics must remain visible, so retain the console subsystem;
    # SDL still creates the native Win32 game window normally.
    get_target_property(SDL3_LINK_OPTIONS PkgConfig::SDL3 INTERFACE_LINK_OPTIONS)
    if (SDL3_LINK_OPTIONS)
        list(FILTER SDL3_LINK_OPTIONS EXCLUDE REGEX "^-mwindows$")
        set_property(TARGET PkgConfig::SDL3 PROPERTY INTERFACE_LINK_OPTIONS "${SDL3_LINK_OPTIONS}")
    endif()
endif()
find_package(miniz QUIET)
if (NOT miniz_FOUND)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(third_party/miniz EXCLUDE_FROM_ALL)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    add_library(miniz::miniz ALIAS miniz)
endif()
find_package(Zydis CONFIG QUIET)
if (NOT TARGET Zydis::Zydis)
    # Fedora's zydis-devel supplies pkg-config metadata, without a CMake config.
    pkg_check_modules(ZYDIS REQUIRED IMPORTED_TARGET zydis)
    add_library(Zydis::Zydis ALIAS PkgConfig::ZYDIS)
endif()
find_program(GLSLANG_COMPILER NAMES glslang glslangValidator REQUIRED)

set(SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/sirit EXCLUDE_FROM_ALL)

# ps4rt: FSR 3.1 temporal upscaling / frame generation, native Vulkan (MIT, FireBurn/FSR-Vulkan).
set(FFX_VK_PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FFX_VK_PORTABLE_BUILD_PROBE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fsr-vulkan EXCLUDE_FROM_ALL)

# Host shaders -> headers (same scheme as shadPS4's host_shaders/CMakeLists.txt).
set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shadps4/video_core/host_shaders)
set(SHADER_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
set(SHADER_DIR ${SHADER_INCLUDE}/video_core/host_shaders)
set(SHADER_HEADERS "")
function(add_host_shader FILENAME SHADER_NAME)
    set(OUT ${SHADER_DIR}/${SHADER_NAME}.h)
    string(TOUPPER ${SHADER_NAME} SYMBOL)
    set(DEFINES ${ARGN})
    list(TRANSFORM DEFINES PREPEND "-D")
    add_custom_command(OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DIR}
        COMMAND ${GLSLANG_COMPILER} --target-env vulkan1.3 -o ${OUT} --vn ${SYMBOL} ${DEFINES} ${SHADER_SRC}/${FILENAME}
        COMMAND ${CMAKE_COMMAND} "-DHEADER_FILE=${OUT}" -P ${SHADER_SRC}/AddShaderHeaderInclude.cmake
        DEPENDS ${SHADER_SRC}/${FILENAME} VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${OUT} PARENT_SCOPE)
endfunction()
add_host_shader(color_to_ms_depth.frag color_to_ms_depth_frag)
add_host_shader(color_to_ms_depth.frag color_to_ms_depth_fallback_frag SAMPLE_FALLBACK=1)
add_host_shader(ms_image_blit.frag ms_image_blit_frag)
add_host_shader(ms_image_blit.frag ms_image_blit_msaa_frag SRC_MSAA=1)
add_host_shader(fault_buffer_process.comp fault_buffer_process_comp)
add_host_shader(fs_tri.vert fs_tri_vert)
add_host_shader(fsr.comp fsr_easu_comp SAMPLE_EASU=1)
add_host_shader(fsr.comp fsr_rcas_comp SAMPLE_RCAS=1)
add_host_shader(fsr4_rcas.comp fsr4_rcas_comp)
add_host_shader(spatial_aa.comp spatial_aa_comp)
add_host_shader(post_process.frag post_process_frag)
add_host_shader(camera_motion.comp camera_motion_comp)
add_host_shader(transition_alpha.comp transition_alpha_comp)
add_host_shader(upscale_merge.comp upscale_merge_comp)
add_host_shader(upscale_reactive.comp upscale_reactive_comp)
add_host_shader(taa.comp taa_comp)
add_host_shader(taa_sharpen.comp taa_sharpen_comp)
add_host_shader(taa_sharpen.comp taa_sharpen_ldr_comp LDR_OUTPUT=1)
add_host_shader(buffer_multi_copy.comp buffer_multi_copy_comp)
add_host_shader(depth_resample.frag depth_resample_frag)
add_host_shader(depth_resample.frag depth_stencil_resample_frag HAS_STENCIL=1)
add_host_shader(depth_resample.frag depth_stencil_bits_frag STENCIL_BITS=1)
foreach(BPP IN ITEMS 8 16 32 64 96 128)
    add_host_shader(tiling.comp tiling_micro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=0)
    add_host_shader(tiling.comp tiling_macro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=1)
endforeach()
add_custom_target(host_shaders DEPENDS ${SHADER_HEADERS})

# Verify exact licensed binaries before staging; source ZIP line endings cannot alter DLLs.
set(FSR_SDK24_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amd-fsr-sdk24")
file(READ "${FSR_SDK24_ROOT}/binaries.json" FSR_SDK24_MANIFEST)
string(JSON FSR_SDK24_COUNT LENGTH "${FSR_SDK24_MANIFEST}" binaries)
math(EXPR FSR_SDK24_LAST "${FSR_SDK24_COUNT}-1")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FSR_SDK24_ROOT}/binaries.json")
foreach(FSR_INDEX RANGE ${FSR_SDK24_LAST})
    string(JSON FSR_DLL MEMBER "${FSR_SDK24_MANIFEST}" binaries ${FSR_INDEX})
    string(JSON FSR_HASH GET "${FSR_SDK24_MANIFEST}" binaries "${FSR_DLL}")
    file(SHA256 "${FSR_SDK24_ROOT}/bin/${FSR_DLL}" FSR_ACTUAL)
    if(NOT FSR_ACTUAL STREQUAL FSR_HASH)
        message(FATAL_ERROR "AMD SDK 2.4 binary verification failed: ${FSR_DLL}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FSR_SDK24_ROOT}/bin/${FSR_DLL}")
endforeach()
# Official signed provider binaries and their app-local Microsoft dependencies.
# No import library: an unavailable SDK cannot prevent the application starting.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amd-fsr-sdk24/bin/"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/amd-fsr-sdk24")
file(GLOB_RECURSE VIDEO_CORE CONFIGURE_DEPENDS shadps4/video_core/*.cpp)
file(GLOB_RECURSE RECOMPILER CONFIGURE_DEPENDS shadps4/shader_recompiler/*.cpp)
list(FILTER VIDEO_CORE EXCLUDE REGEX "renderdoc\\.cpp$")
set(COMMON
    shadps4/common/decoder.cpp shadps4/common/error.cpp
    shadps4/common/io_file.cpp shadps4/common/native_clock.cpp shadps4/common/number_utils.cpp
    shadps4/common/path_util.cpp shadps4/common/rdtsc.cpp shadps4/common/spin_lock.cpp shadps4/common/recursive_lock.cpp
    shadps4/common/string_util.cpp shadps4/common/thread.cpp shadps4/common/signal_context.cpp
    shim/common/logging/log.cpp)
file(GLOB AVPLAYER CONFIGURE_DEPENDS shadps4/core/libraries/avplayer/*.cpp)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
file(GLOB SHIM_SOURCES CONFIGURE_DEPENDS shim/*.cpp shim/*/*.cpp)
# Runtime symbols resolve inside the native executable through this static library.
add_library(ps4rtgpu STATIC ${VIDEO_CORE} ${RECOMPILER} ${COMMON} ${SHIM_SOURCES} ../launcher/localization.cpp
    shadps4/core/libraries/gnmdriver/gnmdriver.cpp
    shadps4/core/libraries/videoout/driver.cpp shadps4/core/libraries/videoout/video_out.cpp
    shadps4/core/libraries/kernel/equeue.cpp shadps4/core/libraries/kernel/event_flag.cpp ${AVPLAYER}
    shadps4/core/libraries/videodec/video_utils.cpp)
add_dependencies(ps4rtgpu host_shaders)
# In-game settings menu: Dear ImGui (third_party/imgui, MIT) with its Vulkan backend, and an
# embedded Cyrillic font (third_party/fonts).
target_sources(ps4rtgpu PRIVATE third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp)
target_include_directories(ps4rtgpu PRIVATE third_party/imgui third_party/imgui/backends)
target_compile_definitions(ps4rtgpu PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    PS4RT_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf")
set_source_files_properties(shim/ps4rt_overlay.cpp PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf)
target_include_directories(ps4rtgpu PRIVATE shim shadps4 ${SHADER_INCLUDE} third_party/gcn/include third_party/half/include)
target_compile_definitions(ps4rtgpu PRIVATE BOOST_ASIO_STANDALONE NDEBUG)
target_compile_options(ps4rtgpu PRIVATE -Wno-deprecated-declarations -Wno-missing-field-initializers)
target_link_libraries(ps4rtgpu PRIVATE ffx-vulkan::portable ffx-vulkan::fsr4-v07-vulkan)
target_link_libraries(ps4rtgpu PUBLIC Vulkan::Headers fmt::fmt Boost::headers magic_enum::magic_enum
    tsl::robin_map GPUOpen::VulkanMemoryAllocator PkgConfig::XXHASH PkgConfig::SDL3 PkgConfig::FFMPEG miniz::miniz Zydis::Zydis sirit)
target_include_directories(ps4rtgpu PRIVATE third_party/xbyak)
if (WIN32)
    # Guest code is entered with the System V ABI; NOMINMAX/WIN32_LEAN_AND_MEAN keep
    # <windows.h> out of the way of std::min/max and the Vulkan/SDL headers.
    # Every translation unit must see the same vulkan-hpp dispatcher layout.
    # Defining the Win32 platform only in vk_platform.cpp corrupts access to the
    # global dynamic dispatcher stored in vk_common.cpp.
    target_compile_definitions(ps4rtgpu PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ps4rtgpu PUBLIC Vulkan::Vulkan onecore ws2_32 bcrypt ntdll synchronization winmm pdh shell32)
endif()

# Native Windows loader. MSVC cannot compile the System V ABI guest entry points or the GNU
# x86-64 assembly used by the fault recovery bridge, so Windows builds intentionally use
# clang/MinGW-w64 (normally the MSYS2 CLANG64 environment) with CMake/Ninja.
if (WIN32)
    enable_language(RC)
    # The MinGW resource compiler does not track these included files through
    # Ninja depfiles. An incremental VERSION/icon change must rebuild every PE
    # resource, including the static root bootstrap and updater test fixture.
    set_property(SOURCE ../packaging/windows/ps4rt.rc ../launcher/launcher.rc
        ../launcher/pkg_extractor.rc ../launcher/updater.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/include/ps4rt_version.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../launcher/resource.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../launcher/assets/ps4rt-star-ocean.ico"
        "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/windows/ps4rt.manifest")
    file(GLOB PS4RT_RUNTIME CONFIGURE_DEPENDS ../src/runtime*.c)
    file(GLOB PS4RT_ATRAC9 CONFIGURE_DEPENDS ../third_party/LibAtrac9/C/src/*.c)

    add_library(atrac9 STATIC ${PS4RT_ATRAC9})
    target_compile_options(atrac9 PRIVATE -w)
    set_target_properties(atrac9 PROPERTIES C_STANDARD 99 INTERPROCEDURAL_OPTIMIZATION OFF)

    add_executable(ps4rt ../src/loader.c ${PS4RT_RUNTIME} ../src/compat_win.c
        ../src/cpu_check.c ../src/isa_rewrite.c ../src/isa_emulate.c
        ../packaging/windows/ps4rt.rc)
    set_target_properties(ps4rt PROPERTIES C_STANDARD 11 C_EXTENSIONS OFF OUTPUT_NAME "Star Ocean 5")
    target_compile_definitions(ps4rt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN PS4RT_RUNTIME)
    target_compile_options(ps4rt PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror>)
    set_source_files_properties(../src/isa_emulate.c PROPERTIES COMPILE_OPTIONS "-mgeneral-regs-only")
    target_include_directories(ps4rt PRIVATE .. ../src)
    target_link_libraries(ps4rt PRIVATE ps4rtgpu atrac9 onecore bcrypt ws2_32 -pthread)
    # Keep ordinary heaps/stacks out of the fixed 64 GiB-1 TiB guest address range. Guest code
    # has no Windows stack probes, therefore the main stack is committed in full.
    target_link_options(ps4rt PRIVATE -Wl,--disable-high-entropy-va
        "SHELL:-Xlinker --stack -Xlinker 16777216,16777216")

    # Native, dependency-light graphical launcher.  It deliberately remains a separate
    # executable so the runtime keeps its console diagnostics while normal users see no shell.
    add_library(so5-update STATIC ../launcher/updater.cpp)
    target_compile_definitions(so5-update PRIVATE UNICODE _UNICODE)
    target_link_libraries(so5-update PUBLIC Boost::headers miniz::miniz winhttp bcrypt version ole32 shell32)
    add_executable(ps4rt-launcher WIN32 ../launcher/main.cpp ../launcher/native_preparer.cpp ../launcher/cheat_profiles.cpp ../launcher/localization.cpp
        ../launcher/launcher.rc)
    set_target_properties(ps4rt-launcher PROPERTIES OUTPUT_NAME "Star Ocean 5 Launcher")
    target_compile_definitions(ps4rt-launcher PRIVATE UNICODE _UNICODE)
    target_link_libraries(ps4rt-launcher PRIVATE Boost::headers so5-update PkgConfig::SDL3 Vulkan::Vulkan gdiplus ole32 shell32 comdlg32 shlwapi user32 gdi32 winhttp dwmapi comctl32 uxtheme)

    # Root player entry point: static CRT, Windows-only imports, same branded PE resources.
    add_executable(so5-bootstrap WIN32 ../launcher/bootstrap.c ../launcher/launcher.rc)
    set_target_properties(so5-bootstrap PROPERTIES OUTPUT_NAME "Star Ocean 5 Launcher"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bootstrap")
    target_link_libraries(so5-bootstrap PRIVATE user32)
    target_link_options(so5-bootstrap PRIVATE -municode -static)

    # Standalone native installer: no CLR, Python, shell, or application DLLs are needed
    # after the launcher exits. A temporary copy can replace the installed helper safely.
    add_executable(so5-updater WIN32 ../launcher/updater_main.cpp ../launcher/updater.rc)
    set_target_properties(so5-updater PROPERTIES OUTPUT_NAME "SO5-Updater")
    target_compile_definitions(so5-updater PRIVATE UNICODE _UNICODE)
    target_link_libraries(so5-updater PRIVATE so5-update)
    target_link_options(so5-updater PRIVATE -static)

    # Independent native package reader; no upstream extractor subprocess or CLR.
    add_library(hmv-pkg STATIC ../launcher/pkg/archive.cpp)
    target_include_directories(hmv-pkg PUBLIC ../launcher/pkg)
    target_compile_definitions(hmv-pkg PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(hmv-pkg PUBLIC miniz::miniz bcrypt)
    add_executable(ps4rt-pkg-extractor ../launcher/pkg/main.cpp ../launcher/pkg_extractor.rc)
    set_target_properties(ps4rt-pkg-extractor PROPERTIES OUTPUT_NAME "PkgExtractor")
    target_compile_definitions(ps4rt-pkg-extractor PRIVATE UNICODE _UNICODE)
    target_link_libraries(ps4rt-pkg-extractor PRIVATE hmv-pkg)
    target_link_options(ps4rt-pkg-extractor PRIVATE -municode -static)
endif()


# Focused regressions for the shipped title's rendering and transition fixes.
enable_testing()
add_executable(hud-standalone-test EXCLUDE_FROM_ALL ../tests/test_hud_standalone.cpp)
target_compile_options(hud-standalone-test PRIVATE -UNDEBUG)
target_link_libraries(hud-standalone-test PRIVATE so5-hud-layout)
add_test(NAME hud-standalone COMMAND hud-standalone-test)
# Shared strict HUD preset model used by the runtime and launcher.
add_library(so5-hud-layout STATIC EXCLUDE_FROM_ALL ../tools/hud/hud_layout.cpp ../tools/hud/hud_runtime_model.cpp ../tools/hud/hud_live.cpp ../tools/hud/hud_preset_library.cpp)
target_include_directories(so5-hud-layout PUBLIC ../tools/hud)
target_link_libraries(so5-hud-layout PUBLIC Boost::headers ole32 advapi32)
target_link_libraries(ps4rt-launcher PRIVATE so5-hud-layout)
target_link_libraries(ps4rtgpu PUBLIC so5-hud-layout)
add_executable(hud-live-test EXCLUDE_FROM_ALL ../tests/test_hud_live.cpp)
target_compile_options(hud-live-test PRIVATE -UNDEBUG)
target_link_libraries(hud-live-test PRIVATE so5-hud-layout)
add_test(NAME hud-live COMMAND hud-live-test)
add_executable(hud-projection-test EXCLUDE_FROM_ALL ../tests/test_hud_projection.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(hud-projection-test PRIVATE shim shadps4)
target_compile_options(hud-projection-test PRIVATE -UNDEBUG)
target_link_libraries(hud-projection-test PRIVATE ps4rtgpu)
add_test(NAME hud-projection COMMAND hud-projection-test "${CMAKE_CURRENT_BINARY_DIR}")
find_program(SPIRV_VALIDATOR spirv-val)
if(SPIRV_VALIDATOR)
    foreach(index RANGE 0 9)
        add_test(NAME hud-spirv-${index} COMMAND "${SPIRV_VALIDATOR}" --target-env vulkan1.3 "${CMAKE_CURRENT_BINARY_DIR}/hud-projection-${index}.spv")
        set_tests_properties(hud-spirv-${index} PROPERTIES DEPENDS hud-projection)
    endforeach()
endif()
add_executable(ultrawide-test EXCLUDE_FROM_ALL ../tests/test_ultrawide.cpp)
target_include_directories(ultrawide-test PRIVATE shim shadps4)
target_compile_options(ultrawide-test PRIVATE -UNDEBUG)
add_test(NAME ultrawide COMMAND ultrawide-test)
add_executable(upscaler-support-test EXCLUDE_FROM_ALL ../tests/test_upscaler_support.cpp)
target_include_directories(upscaler-support-test PRIVATE shim)
target_compile_options(upscaler-support-test PRIVATE -UNDEBUG)
target_link_libraries(upscaler-support-test PRIVATE Vulkan::Vulkan)
add_test(NAME upscaler-support COMMAND upscaler-support-test)
add_executable(window-presentation-test EXCLUDE_FROM_ALL ../tests/test_window_presentation.cpp)
target_include_directories(window-presentation-test PRIVATE shim)
target_compile_options(window-presentation-test PRIVATE -UNDEBUG)
target_link_libraries(window-presentation-test PRIVATE PkgConfig::SDL3)
add_test(NAME window-presentation COMMAND window-presentation-test)
set_tests_properties(window-presentation PROPERTIES SKIP_RETURN_CODE 77)
if (WIN32)
    add_executable(native-pkg-test EXCLUDE_FROM_ALL ../tests/test_native_pkg.cpp)
    target_compile_options(native-pkg-test PRIVATE -UNDEBUG)
    target_link_libraries(native-pkg-test PRIVATE hmv-pkg)
    add_test(NAME native-pkg COMMAND native-pkg-test)
    add_executable(runtime-time-test EXCLUDE_FROM_ALL ../tests/test_runtime_time.c ../src/runtime_time.c ../src/compat_win.c)
    target_include_directories(runtime-time-test PRIVATE ../src)
    target_compile_options(runtime-time-test PRIVATE -UNDEBUG)
    target_link_libraries(runtime-time-test PRIVATE bcrypt -pthread)
    add_test(NAME runtime-time COMMAND runtime-time-test)
    add_executable(trophy-file-test EXCLUDE_FROM_ALL ../tests/test_trophy_file.c)
    target_compile_definitions(trophy-file-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(trophy-file-test PRIVATE -UNDEBUG)
    add_test(NAME trophy-file COMMAND trophy-file-test)
    add_executable(savedata-backups-test EXCLUDE_FROM_ALL ../tests/test_savedata_backups.c ../src/compat_win.c)
    target_include_directories(savedata-backups-test PRIVATE .. ../src)
    target_compile_definitions(savedata-backups-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(savedata-backups-test PRIVATE -UNDEBUG)
    target_link_libraries(savedata-backups-test PRIVATE bcrypt -pthread)
    add_test(NAME savedata-backups COMMAND savedata-backups-test)
    add_executable(save-backup-guard-test EXCLUDE_FROM_ALL ../tests/test_save_backup_guard.cpp)
    target_compile_definitions(save-backup-guard-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(save-backup-guard-test PRIVATE -UNDEBUG)
    target_link_libraries(save-backup-guard-test PRIVATE gdiplus -pthread)
    add_test(NAME save-backup-guard COMMAND save-backup-guard-test)
    add_executable(launcher-updater-test EXCLUDE_FROM_ALL ../tests/test_launcher_updater.cpp ../launcher/updater.rc)
    target_include_directories(launcher-updater-test PRIVATE ../launcher)
    target_compile_definitions(launcher-updater-test PRIVATE UNICODE _UNICODE)
    target_compile_options(launcher-updater-test PRIVATE -UNDEBUG)
    target_link_libraries(launcher-updater-test PRIVATE so5-update)
    add_dependencies(launcher-updater-test so5-updater)
    add_test(NAME launcher-updater COMMAND launcher-updater-test "${CMAKE_CURRENT_SOURCE_DIR}/../out")
endif()
add_executable(display-sample-gpu-test EXCLUDE_FROM_ALL ../tests/test_display_sample_gpu.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(display-sample-gpu-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(display-sample-gpu-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(display-sample-gpu-test PRIVATE ps4rtgpu)
add_test(NAME display-sample-gpu COMMAND display-sample-gpu-test)
add_executable(fsr4-rcas-gpu-test EXCLUDE_FROM_ALL ../tests/test_fsr4_rcas_gpu.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(fsr4-rcas-gpu-test PRIVATE shim shadps4 third_party/gcn/include ${SHADER_INCLUDE})
target_compile_options(fsr4-rcas-gpu-test PRIVATE -UNDEBUG -fPIC)
set_source_files_properties(../tests/test_fsr4_rcas_gpu.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")
target_link_libraries(fsr4-rcas-gpu-test PRIVATE ps4rtgpu)
add_dependencies(fsr4-rcas-gpu-test host_shaders)
add_test(NAME fsr4-rcas-gpu COMMAND fsr4-rcas-gpu-test "${CMAKE_CURRENT_SOURCE_DIR}/../fsr4_shaders/rcas.spv")
add_executable(dlss-validation-test EXCLUDE_FROM_ALL ../tests/test_dlss_validation.cpp)
target_compile_options(dlss-validation-test PRIVATE -UNDEBUG)
add_test(NAME dlss-validation COMMAND dlss-validation-test)
add_executable(dlss-device-gate-test EXCLUDE_FROM_ALL ../tests/test_dlss_device_gate.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(dlss-device-gate-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(dlss-device-gate-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(dlss-device-gate-test PRIVATE ps4rtgpu)
add_test(NAME dlss-device-gate COMMAND dlss-device-gate-test)
set_tests_properties(dlss-device-gate PROPERTIES SKIP_RETURN_CODE 77)
add_executable(transition-snapshot-gpu-test EXCLUDE_FROM_ALL ../tests/test_transition_snapshot_gpu.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(transition-snapshot-gpu-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(transition-snapshot-gpu-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(transition-snapshot-gpu-test PRIVATE ps4rtgpu)
add_test(NAME transition-snapshot-gpu COMMAND transition-snapshot-gpu-test)
add_executable(cpu-image-writeback-test EXCLUDE_FROM_ALL ../tests/test_cpu_image_writeback.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(cpu-image-writeback-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(cpu-image-writeback-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(cpu-image-writeback-test PRIVATE ps4rtgpu)
add_test(NAME cpu-image-writeback COMMAND cpu-image-writeback-test)
add_executable(color-export-test EXCLUDE_FROM_ALL ../tests/test_color_export.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(color-export-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(color-export-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(color-export-test PRIVATE ps4rtgpu)
add_test(NAME color-export COMMAND color-export-test)
add_executable(image-subresource-test EXCLUDE_FROM_ALL ../tests/test_image_subresource.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(image-subresource-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(image-subresource-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(image-subresource-test PRIVATE ps4rtgpu)
add_test(NAME image-subresource COMMAND image-subresource-test)
add_executable(gpu-workarounds-test EXCLUDE_FROM_ALL ../tests/test_gpu_workarounds.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(gpu-workarounds-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(gpu-workarounds-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(gpu-workarounds-test PRIVATE ps4rtgpu)
add_test(NAME gpu-workarounds COMMAND gpu-workarounds-test)
add_executable(camera-layout-test EXCLUDE_FROM_ALL ../tests/test_camera_layout.cpp)
target_include_directories(camera-layout-test PRIVATE shim shadps4)
target_compile_options(camera-layout-test PRIVATE -UNDEBUG)
add_test(NAME camera-layout COMMAND camera-layout-test)
add_executable(camera-probe-test EXCLUDE_FROM_ALL ../tests/test_camera_probe.cpp)
target_include_directories(camera-probe-test PRIVATE shim shadps4)
target_compile_definitions(camera-probe-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(camera-probe-test PRIVATE -UNDEBUG)
add_test(NAME camera-probe COMMAND camera-probe-test)
add_executable(camera-comfort-test EXCLUDE_FROM_ALL ../tests/test_camera_comfort.cpp)
target_include_directories(camera-comfort-test PRIVATE shim shadps4)
target_compile_definitions(camera-comfort-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(camera-comfort-test PRIVATE -UNDEBUG)
add_test(NAME camera-comfort COMMAND camera-comfort-test)
add_executable(native-tweaks-test EXCLUDE_FROM_ALL ../tests/test_native_tweaks.cpp)
target_compile_definitions(native-tweaks-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(native-tweaks-test PRIVATE -UNDEBUG)
target_link_libraries(native-tweaks-test PRIVATE ole32)
add_test(NAME native-tweaks COMMAND native-tweaks-test)
  add_executable(exploration-leader-test EXCLUDE_FROM_ALL ../tests/test_exploration_leader.cpp shim/ps4rt_settings.cpp)
  target_include_directories(exploration-leader-test PRIVATE shim shadps4)
  target_compile_definitions(exploration-leader-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(exploration-leader-test PRIVATE -UNDEBUG)
  add_test(NAME exploration-leader COMMAND exploration-leader-test)
add_executable(scene-camera-history-test EXCLUDE_FROM_ALL ../tests/test_scene_camera_history.cpp)
target_include_directories(scene-camera-history-test PRIVATE shim shadps4)
target_compile_options(scene-camera-history-test PRIVATE -UNDEBUG)
add_test(NAME scene-camera-history COMMAND scene-camera-history-test)
add_executable(menu-input-test EXCLUDE_FROM_ALL ../tests/test_menu_input.cpp)
target_include_directories(menu-input-test PRIVATE shim shadps4)
target_compile_options(menu-input-test PRIVATE -UNDEBUG)
add_test(NAME menu-input COMMAND menu-input-test)
add_executable(pad-input-test EXCLUDE_FROM_ALL ../tests/test_pad_input.c)
target_include_directories(pad-input-test PRIVATE .. ../src)
target_compile_options(pad-input-test PRIVATE -UNDEBUG)
target_link_libraries(pad-input-test PRIVATE PkgConfig::SDL3 -pthread)
add_test(NAME pad-input COMMAND pad-input-test)
add_executable(menu-sounds-test EXCLUDE_FROM_ALL ../tests/test_menu_sounds.cpp)
target_include_directories(menu-sounds-test PRIVATE shim)
target_link_libraries(menu-sounds-test PRIVATE PkgConfig::SDL3)
target_compile_options(menu-sounds-test PRIVATE -UNDEBUG)
add_test(NAME menu-sounds COMMAND menu-sounds-test)
add_executable(graphics-settings-test EXCLUDE_FROM_ALL ../tests/test_graphics_settings.cpp shim/ps4rt_settings.cpp)
target_include_directories(graphics-settings-test PRIVATE shim)
target_compile_options(graphics-settings-test PRIVATE -UNDEBUG)
add_test(NAME graphics-settings COMMAND graphics-settings-test)
add_executable(perf-sampling-test EXCLUDE_FROM_ALL ../tests/test_perf_sampling.cpp)
target_include_directories(perf-sampling-test PRIVATE shim)
target_compile_options(perf-sampling-test PRIVATE -UNDEBUG)
add_test(NAME perf-sampling COMMAND perf-sampling-test)
set_tests_properties(perf-sampling PROPERTIES TIMEOUT 30)

add_executable(memory-budget-test EXCLUDE_FROM_ALL ../tests/test_memory_budget.cpp)
target_include_directories(memory-budget-test PRIVATE ../src)
target_compile_options(memory-budget-test PRIVATE -UNDEBUG)
add_test(NAME guest-memory-budgets COMMAND memory-budget-test)
add_executable(cockpit-test EXCLUDE_FROM_ALL ../tests/test_cockpit.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(cockpit-test PRIVATE shim shadps4 third_party/imgui third_party/imgui/backends)
target_compile_options(cockpit-test PRIVATE -UNDEBUG)
target_compile_definitions(cockpit-test PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    PS4RT_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf")
target_link_libraries(cockpit-test PRIVATE ps4rtgpu)
foreach(language RANGE 0 11)
    add_test(NAME cockpit-language-${language} COMMAND cockpit-test ${language})
endforeach()

# Developer-only AHSL cache validation. Neither target is part of the player build or package.
add_executable(ahsl-cache-test EXCLUDE_FROM_ALL ../tests/test_ahsl_cache.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(ahsl-cache-test PRIVATE shim shadps4)
target_compile_options(ahsl-cache-test PRIVATE -UNDEBUG)
target_link_libraries(ahsl-cache-test PRIVATE ps4rtgpu)
add_test(NAME ahsl-cache COMMAND ahsl-cache-test)

add_executable(shader-capture-test EXCLUDE_FROM_ALL ../tests/test_shader_capture.cpp
    shim/ps4rt_shader_capture.cpp)
target_include_directories(shader-capture-test PRIVATE shim)
target_compile_options(shader-capture-test PRIVATE -UNDEBUG)
add_test(NAME shader-capture COMMAND shader-capture-test)

add_executable(ps4rt-shader-capture-inspector EXCLUDE_FROM_ALL
    ../tools/shader_capture_inspector.cpp shim/ps4rt_shader_capture.cpp)
target_include_directories(ps4rt-shader-capture-inspector PRIVATE shim)

add_executable(ps4rt-ahsl-inspector EXCLUDE_FROM_ALL ../tools/ahsl_cache_native.cpp
    ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(ps4rt-ahsl-inspector PRIVATE shim shadps4)
target_link_libraries(ps4rt-ahsl-inspector PRIVATE ps4rtgpu)

set_source_files_properties(../tests/test_display_sample_gpu.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")

set_source_files_properties(../tests/test_transition_snapshot_gpu.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")

set_source_files_properties(../tests/test_cpu_image_writeback.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")

set_source_files_properties(../tests/test_color_export.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")

set_source_files_properties(../tests/test_image_subresource.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")

set_source_files_properties(../tests/test_gpu_workarounds.cpp PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden;-fvisibility-inlines-hidden")


add_executable(scene-targets-gpu-test EXCLUDE_FROM_ALL ../tests/test_scene_targets_gpu.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(scene-targets-gpu-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(scene-targets-gpu-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(scene-targets-gpu-test PRIVATE ps4rtgpu)
add_test(NAME scene-targets-gpu COMMAND scene-targets-gpu-test)

add_executable(launcher-shortcuts-test EXCLUDE_FROM_ALL ../tests/test_launcher_shortcuts.cpp ../launcher/localization.cpp)
target_compile_options(launcher-shortcuts-test PRIVATE -UNDEBUG)
target_link_libraries(launcher-shortcuts-test PRIVATE ole32 shell32)
add_test(NAME launcher-shortcuts COMMAND launcher-shortcuts-test)
add_executable(log-console-test EXCLUDE_FROM_ALL ../tests/test_log_console.cpp)
target_compile_definitions(log-console-test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(log-console-test PRIVATE -UNDEBUG)
target_link_libraries(log-console-test PRIVATE user32 gdi32 comctl32 uxtheme)
add_test(NAME log-console COMMAND log-console-test)

# Selective Windows integration: production fiber and shader optimizer regressions.
add_executable(fiber-test EXCLUDE_FROM_ALL ../tests/test_fiber.cpp ../src/runtime_fiber.c)
target_include_directories(fiber-test PRIVATE ../src)
target_compile_options(fiber-test PRIVATE -UNDEBUG)
target_link_libraries(fiber-test PRIVATE -pthread)
add_test(NAME windows-fibers COMMAND fiber-test)
add_executable(half-precision-test EXCLUDE_FROM_ALL ../tests/test_half_precision.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(half-precision-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(half-precision-test PRIVATE -UNDEBUG)
target_link_libraries(half-precision-test PRIVATE ps4rtgpu)
add_test(NAME half-precision COMMAND half-precision-test)

add_executable(native-cheats-test EXCLUDE_FROM_ALL ../tests/test_native_cheats.cpp ../launcher/native_preparer.cpp ../launcher/cheat_profiles.cpp ../src/runtime_cheats.c)
target_compile_definitions(native-cheats-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SO5_CHEATS_TEST)
target_compile_options(native-cheats-test PRIVATE -UNDEBUG)
target_link_libraries(native-cheats-test PRIVATE Boost::headers bcrypt)
add_test(NAME native-cheats COMMAND native-cheats-test)
add_test(NAME native-cheats-rollback COMMAND native-cheats-test rollback)
add_test(NAME native-cheats-integrity COMMAND native-cheats-test integrity)
add_executable(game-languages-test EXCLUDE_FROM_ALL ../tests/test_game_languages.cpp)
target_compile_options(game-languages-test PRIVATE -UNDEBUG)
add_test(NAME game-languages COMMAND game-languages-test)
add_executable(fast-forward-test EXCLUDE_FROM_ALL ../tests/test_fast_forward.cpp shim/ps4rt_speed.cpp)
target_compile_definitions(fast-forward-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(fast-forward-test PRIVATE -UNDEBUG)
target_link_libraries(fast-forward-test PRIVATE PkgConfig::SDL3)
add_test(NAME fast-forward COMMAND fast-forward-test)
add_test(NAME fast-forward-conflicts COMMAND fast-forward-test conflict)

# The official SDK is optional at runtime; exercise its actual Vulkan dispatch separately.
add_executable(fsr-sdk24-gpu-test EXCLUDE_FROM_ALL ../tests/test_fsr_sdk24_gpu.cpp ../tests/test_gpu_runtime_stubs.cpp)
target_include_directories(fsr-sdk24-gpu-test PRIVATE shim shadps4 third_party/gcn/include)
target_compile_options(fsr-sdk24-gpu-test PRIVATE -UNDEBUG -fPIC)
target_link_libraries(fsr-sdk24-gpu-test PRIVATE ps4rtgpu)
add_test(NAME fsr-sdk24-gpu COMMAND fsr-sdk24-gpu-test "${CMAKE_CURRENT_SOURCE_DIR}/third_party/amd-fsr-sdk24/bin")
set_tests_properties(fsr-sdk24-gpu PROPERTIES SKIP_RETURN_CODE 77)

# Capture-only synchronization telemetry, shared by C runtime and C++ report writer.
add_library(so5-diagnostic-trace STATIC ../src/diagnostic_trace.c)
set_target_properties(so5-diagnostic-trace PROPERTIES C_STANDARD 11 C_EXTENSIONS OFF)
target_compile_definitions(so5-diagnostic-trace PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(so5-diagnostic-trace PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps4rtgpu PUBLIC so5-diagnostic-trace)
add_executable(diagnostic-trace-test EXCLUDE_FROM_ALL ../tests/test_diagnostic_trace.cpp)
target_compile_definitions(diagnostic-trace-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(diagnostic-trace-test PRIVATE -UNDEBUG)
add_test(NAME diagnostic-trace COMMAND diagnostic-trace-test)
set_tests_properties(diagnostic-trace PROPERTIES TIMEOUT 30)
# Exercise the actual CSV/JSON writer without linking the renderer implementation.
add_executable(performance-report-test EXCLUDE_FROM_ALL ../tests/test_performance_report.cpp)
target_include_directories(performance-report-test PRIVATE
    $<TARGET_PROPERTY:ps4rtgpu,INCLUDE_DIRECTORIES>)
target_compile_definitions(performance-report-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(performance-report-test PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
target_link_options(performance-report-test PRIVATE -Wl,--gc-sections)
target_link_libraries(performance-report-test PRIVATE Vulkan::Headers fmt::fmt Boost::headers
    magic_enum::magic_enum tsl::robin_map GPUOpen::VulkanMemoryAllocator
    PkgConfig::XXHASH PkgConfig::SDL3 PkgConfig::FFMPEG miniz::miniz Zydis::Zydis sirit)
add_test(NAME performance-report COMMAND performance-report-test "${CMAKE_CURRENT_BINARY_DIR}/diagnostic-test-reports")
set_tests_properties(performance-report PROPERTIES TIMEOUT 30)
# Some isolated runtime tests compile the C services directly, without ps4rtgpu.
get_property(so5_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(so5_target IN LISTS so5_directory_targets)
    get_target_property(so5_type ${so5_target} TYPE)
    get_target_property(so5_sources ${so5_target} SOURCES)
    if(so5_type STREQUAL "EXECUTABLE" AND
       (so5_sources MATCHES "runtime_(mutex|sema|thread)[.]c" OR
        so5_target MATCHES "^(diagnostic-trace-test|performance-report-test)$"))
        target_link_libraries(${so5_target} PRIVATE so5-diagnostic-trace)
    endif()
endforeach()
