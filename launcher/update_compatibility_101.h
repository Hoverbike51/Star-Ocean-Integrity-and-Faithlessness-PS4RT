// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <string>

namespace So5Update::Legacy101 {
// Frozen compatibility contract from v1.0.1 launcher/updater.cpp (c9cfe1c).
// This does not authorize installation: the current validator still checks every
// file, executable resource, dependency and ZIP entry. Do not extend this contract
// when adding payloads: installed v1.0.1 clients cannot acquire new allowlist rules.
inline bool ProgramFile(const std::filesystem::path& relative) {
    const auto lower = [](std::string value) {
        for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        return value;
    };
    const std::string name=lower(relative.generic_string());
    if (name.starts_with("docs/third-party/") && (name.ends_with(".txt") || name.ends_with(".md"))) return true;
    const std::string parent=lower(relative.parent_path().generic_string());
    if ((parent=="docs/images/readme/launcher" || parent=="docs/images/readme/custom_menu") && name.ends_with(".png")) return true;
    if (name=="docs/launcher_guide.md" || name=="docs/in_game_menu_guide.md") return true;
    if (relative.parent_path()=="bin")
        return name=="bin/star ocean 5.exe" || name=="bin/star ocean 5 launcher.exe" ||
               name=="bin/pkgextractor.exe" || name=="bin/so5-updater.exe" || name.ends_with(".dll");
    if (name.find('/')==std::string::npos)
        return name=="ps4rt.exe" || name=="ps4rt-launcher.exe" || name=="pkgextractor.exe" ||
               name=="star ocean 5.exe" || name=="star ocean 5 launcher.exe" || name=="license" ||
               name=="changelog.md" || name=="readme.md" || name=="readme-windows.txt" || name=="version" || name.ends_with(".dll");
    return name.starts_with("gpu/assets/") || name.starts_with("launcher/assets/") || name.starts_with("fsr4_shaders/") ||
           name=="tools/updater/so5-updater.exe" || name=="docs/upstream_credits.md" || name=="docs/windows_updates.md" ||
           name=="docs/native_pkg.md" || name=="docs/miniz_license.txt" || name=="docs/libcxx_license.txt" || name=="docs/window_presentation.md";
}
} // namespace So5Update::Legacy101
