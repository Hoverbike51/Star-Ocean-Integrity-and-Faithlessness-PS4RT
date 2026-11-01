// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <filesystem>
#include <memory>

namespace So5SavePreview {
inline std::unique_ptr<Gdiplus::Image> Load(const std::filesystem::path& path) {
    using namespace Gdiplus;
    std::unique_ptr<Image> source(Image::FromFile(path.c_str()));
    if(!source || source->GetLastStatus()!=Ok || !source->GetWidth() || !source->GetHeight()) return {};
    auto copy=std::make_unique<Bitmap>(source->GetWidth(),source->GetHeight(),PixelFormat32bppPARGB);
    if(copy->GetLastStatus()!=Ok) return {};
    Graphics graphics(copy.get());
    if(graphics.DrawImage(source.get(),0,0,INT(source->GetWidth()),INT(source->GetHeight()))!=Ok) return {};
    return copy; // Independent pixels; source stream/file is released on return.
}
}
