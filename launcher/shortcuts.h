// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <filesystem>
#include <string_view>

namespace So5Shortcuts {
namespace fs = std::filesystem;
enum class LaunchMode { Interactive, Autoplay, QuickPlay };
inline LaunchMode Mode(int count, wchar_t* const* arguments) {
    LaunchMode mode = LaunchMode::Interactive;
    for (int i = 1; arguments && i < count; ++i) {
        const std::wstring_view argument(arguments[i]);
        if (argument == L"--quick-play") return LaunchMode::QuickPlay;
        if (argument == L"--play") mode = LaunchMode::Autoplay;
    }
    return mode;
}
inline constexpr const wchar_t* LauncherName = L"Star Ocean 5 Launcher.lnk";
inline constexpr const wchar_t* GameName = L"Star Ocean Integrity and faithlessness.lnk";

inline HRESULT Save(const fs::path& file, const fs::path& executable,
                    const fs::path& root, const wchar_t* arguments,
                    const wchar_t* description, const fs::path& icon) {
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(result)) return result;
    result = link->SetPath(executable.c_str());
    if (SUCCEEDED(result)) result = link->SetArguments(arguments);
    if (SUCCEEDED(result)) result = link->SetWorkingDirectory(root.c_str());
    if (SUCCEEDED(result)) result = link->SetDescription(description);
    if (SUCCEEDED(result)) result = link->SetIconLocation(icon.c_str(), 0);
    if (SUCCEEDED(result)) result = link->SetShowCmd(SW_SHOWNORMAL);
    IPersistFile* persist = nullptr;
    if (SUCCEEDED(result)) result = link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(result)) result = persist->Save(file.c_str(), TRUE);
    if (persist) persist->Release();
    link->Release();
    return result;
}

// Paths are resolved when creating the links, never from the caller's working directory.
// Both links use the stable entry point so subsequent application updates keep working.
inline HRESULT Create(const fs::path& desktop, const fs::path& root,
                      const fs::path& actual_launcher, const fs::path& runtime) {
    std::error_code error;
    const fs::path bootstrap = root / L"Star Ocean 5 Launcher.exe";
    const fs::path launcher = fs::is_regular_file(bootstrap, error) ? bootstrap : actual_launcher;
    if (!fs::is_regular_file(launcher, error)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    const fs::path icon = fs::is_regular_file(runtime, error) ? runtime : launcher;
    HRESULT result = Save(desktop / LauncherName, launcher, root, L"",
        L"Star Ocean 5 Launcher", launcher);
    if (SUCCEEDED(result)) result = Save(desktop / GameName, launcher, root, L"--quick-play",
        L"STAR OCEAN: Integrity and Faithlessness", icon);
    return result;
}
}
