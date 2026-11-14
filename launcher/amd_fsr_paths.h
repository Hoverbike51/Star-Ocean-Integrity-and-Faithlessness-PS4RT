// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>

namespace So5AmdFsr {
inline bool ContainsSdk(const std::filesystem::path& directory) {
    std::error_code error;
    return std::filesystem::is_regular_file(directory / L"amd_fidelityfx_loader_vk.dll",error) &&
           std::filesystem::is_regular_file(directory / L"amd_fidelityfx_upscaler_vk.dll",error);
}
inline std::filesystem::path SdkDirectory(const std::filesystem::path& program_directory) {
    // v1.0.1 accepts gpu/assets/* but rejects bin/* subdirectories. Keep AMD's
    // app-local dependencies isolated, without searching the working directory/PATH.
    auto folder=program_directory.filename().wstring();
    for (auto& c : folder) if (c>=L'A' && c<=L'Z') c+=L'a'-L'A';
    const auto root = folder == L"bin" ? program_directory.parent_path() : program_directory;
    const auto packaged = root / L"gpu/assets/amd-fsr-sdk24";
    if (ContainsSdk(packaged)) return packaged;
    // Existing installations and developer builds keep their previous layout.
    return program_directory / L"amd-fsr-sdk24";
}
} // namespace So5AmdFsr
