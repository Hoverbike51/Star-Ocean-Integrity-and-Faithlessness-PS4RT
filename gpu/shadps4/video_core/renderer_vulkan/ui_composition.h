// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace Vulkan::UiComposition {

enum class Background { None, Copy, Temporal };

// A current-frame camera is enough to identify 3D before camera history is
// committed. Proxy validity only chooses the pixel source, never its aspect.
constexpr bool NativeMenuBackground(bool frame_camera,bool tonemapped_scene) {
    return !frame_camera && !tonemapped_scene;
}
constexpr bool WideSceneBackground(bool frame_camera,bool matching_scene) {
    // The exact tonemap target is authoritative even when a transition has
    // invalidated temporal camera history. Never reuse last-frame evidence:
    // the caller resets the target at every display boundary.
    (void)frame_camera;
    return matching_scene;
}

inline bool PerspectiveMenuBackground(bool native_gui, bool offscreen_rgba,
                                      bool native_viewport, float frame_aspect,
                                      float output_aspect) {
    // A real, current-frame wide projection is required. A logo/atlas or a
    // previous frame alone cannot turn a movie or a flat menu into a 3D scene.
    return native_gui && offscreen_rgba && native_viewport &&
           std::isfinite(frame_aspect) && std::isfinite(output_aspect) &&
           frame_aspect>0 && output_aspect>0 && std::abs(output_aspect-16.f/9)>.001f &&
           std::abs(frame_aspect-output_aspect)<.025f;
}

inline bool WideDisplayScene(bool frame_camera,float frame_aspect,float output_aspect) {
    // A final scene-copy can bypass the usual tonemap during a zone transition.
    // Route its ACTUAL current-frame wide projection to an output-size display,
    // independently of temporal reconstruction, depth history or a scene snapshot.
    // Missing, previous-frame or 16:9 camera evidence must not stretch a movie.
    return frame_camera&&std::isfinite(frame_aspect)&&std::isfinite(output_aspect)&&
        frame_aspect>0&&output_aspect>0&&std::abs(output_aspect-16.f/9)>.001f&&
        std::abs(frame_aspect-output_aspect)<.025f;
}

struct DisplayRoute {bool redirect;unsigned width,height;bool sample_composed;};
constexpr DisplayRoute RouteDisplay(bool composed,bool current_wide_scene,
                                    unsigned composed_width,unsigned composed_height,
                                    unsigned output_width,unsigned output_height) {
    if(composed)return {true,composed_width,composed_height,true};
    return {current_wide_scene,output_width,output_height,false};
}

// UI composition must not require scene depth/camera or an active FSR context.
constexpr Background Choose(bool scaled, bool ui_draw, bool scene_ready, bool fsr_active) {
    if (!scaled || !ui_draw) {
        return Background::None;
    }
    return scene_ready && fsr_active ? Background::Temporal : Background::Copy;
}

inline bool NativeViewport(float width, float height) {
    return std::abs(std::abs(width) - 1920.0f) < 0.5f &&
           std::abs(std::abs(height) - 1080.0f) < 0.5f;
}

// Scaleform draws, including the first stencil/movie pass. A native-size viewport
// alone also matches every fullscreen post pass when guest targets stay at 1080p.
constexpr bool MovieShader(uint64_t hash) {
    return hash == 0x34e8a281 || hash == 0x81d336ce || hash == 0x09957251 ||
           hash == 0x24042a9b || hash == 0xa400228b;
}

inline std::array<float, 2> Scale(uint32_t guest_width, uint32_t guest_height,
                                uint32_t output_width, uint32_t output_height,
                                bool native_coordinates) {
    return {float(output_width) / float(native_coordinates ? 1920 : guest_width),
            float(output_height) / float(native_coordinates ? 1080 : guest_height)};
}

} // namespace Vulkan::UiComposition
