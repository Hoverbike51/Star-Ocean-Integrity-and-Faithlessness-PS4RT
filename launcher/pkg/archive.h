// Copyright (c) 2026 HoverMods Vault
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace HmvPkg {
struct Summary {
    std::uint64_t files = 0, bytes = 0;
    std::string title_id, staging_name;
};
struct Options {
    std::string passcode = std::string(32, '0');
    bool list = false;
    bool verify = false;
    bool title_folder = false; // Publish a complete extraction as DIRECTORY/TITLE_ID.
    std::vector<std::string> only; // Developer verification may restrict the files read.
    std::function<void(std::uint64_t, std::uint64_t)> progress;
    std::function<void(const Summary&)> planned;
};
// Extract into a new/empty directory, or compare against an existing tree in verify mode.
// No caller-owned files are replaced and no external extractor process is invoked.
Summary Process(const std::filesystem::path& package, const std::filesystem::path& directory,
                const Options& options = {});
std::filesystem::path EntryPath(const std::string& name);
}
