// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include "ps4rt_dlss_bridge.h"

namespace So5Dlss {
// Validate the shared ABI before either side records work in the NVIDIA SDK.
// Hardware and driver support still come from the selected device and NGX.
inline const char* FeatureProblem(const Ps4RtDlssFeature& f) {
    if (!f.input_width || !f.input_height || !f.output_width || !f.output_height)
        return "empty render or output dimensions";
    if (f.input_width > f.output_width || f.input_height > f.output_height)
        return "DLSS input exceeds the output dimensions";
    if (f.quality < 0 || f.quality > 4)
        return "unknown DLSS quality mode";
    if (f.quality == 0 && (f.input_width != f.output_width || f.input_height != f.output_height))
        return "DLAA requires equal render and output dimensions";
    return nullptr;
}

inline bool ImageValid(const Ps4RtDlssImage& i, VkImageAspectFlags aspect,
                       uint32_t width, uint32_t height) {
    // A larger allocation may hold an input subrectangle. NGX receives the
    // actual allocation extent; the feature determines the active rectangle.
    return i.image && i.view && i.width >= width && i.height >= height &&
        i.range.aspectMask == aspect && i.range.levelCount == 1 &&
        i.range.layerCount == 1 && !i.range.baseMipLevel && !i.range.baseArrayLayer;
}

inline const char* FrameProblem(const Ps4RtDlssFeature& f, const Ps4RtDlssEvaluate& e) {
    if (const auto reason=FeatureProblem(f)) return reason;
    if (!ImageValid(e.color,VK_IMAGE_ASPECT_COLOR_BIT,f.input_width,f.input_height) ||
        !ImageValid(e.depth,VK_IMAGE_ASPECT_DEPTH_BIT,f.input_width,f.input_height) ||
        !ImageValid(e.motion,VK_IMAGE_ASPECT_COLOR_BIT,f.input_width,f.input_height) ||
        !ImageValid(e.output,VK_IMAGE_ASPECT_COLOR_BIT,f.output_width,f.output_height))
        return "missing image, invalid view or undersized DLSS input/output";
    if (e.color.image == e.output.image || e.depth.image == e.output.image ||
        e.motion.image == e.output.image)
        return "DLSS output aliases an input image";
    if (e.motion.format != VK_FORMAT_R16G16_SFLOAT || e.output.format != VK_FORMAT_R16G16B16A16_SFLOAT)
        return "unexpected DLSS motion/output format";
    if (e.color.format == VK_FORMAT_UNDEFINED)
        return "undefined DLSS colour format";
    if (e.depth.format != VK_FORMAT_D32_SFLOAT && e.depth.format != VK_FORMAT_D32_SFLOAT_S8_UINT)
        return "unexpected DLSS depth format";
    if (!std::isfinite(e.jitter_x) || !std::isfinite(e.jitter_y) ||
        !std::isfinite(e.frame_ms) || e.frame_ms <= 0 ||
        !std::isfinite(e.sharpness) || e.sharpness < 0 || e.sharpness > 1)
        return "invalid DLSS timing, jitter or sharpness";
    return nullptr;
}
} // namespace So5Dlss
