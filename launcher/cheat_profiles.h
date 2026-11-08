// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <cstdint>
namespace So5Cheats {
struct Range { std::uint64_t start, end; bool executable, writable; };
struct Entry {
    int id=0; bool builtin=false,available=false;
    std::string name,description,source,problem;
    std::vector<float> choices;
};
struct Result { std::vector<std::string> names, problems; std::vector<Entry> entries; };
// Compiles only built-in definitions matching the detected title/application version.
// External profiles are never scanned, parsed or imported.
// The original game files are never modified. All runtime switches start off.
Result Compile(const std::filesystem::path& table,
               const std::string& title, const std::string& version,
               std::span<const std::uint8_t> image, std::span<const Range> ranges,
               bool publish=true);
}
