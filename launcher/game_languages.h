// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace So5Languages {
struct Language { const char* category; const wchar_t* label; int system_code; };
inline constexpr Language Languages[] = {
    {"English", L"English", 1}, {"French", L"Français", 2},
    {"German", L"Deutsch", 4}, {"Italian", L"Italiano", 5},
    {"Spanish", L"Español", 3}, {"Japanese", L"日本語", 0}
};
inline int SystemCode(const std::wstring& label) {
    for (const auto& language : Languages) if (label == language.label) return language.system_code;
    return 1;
}
using Rows = std::vector<std::vector<std::string>>;
inline Rows ReadCsv(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 1024 * 1024) return {};
    std::ifstream input(path);
    Rows rows;
    std::string line;
    while (std::getline(input, line)) {
        if (rows.size() >= 4096 || line.size() > 16384) return {};
        if (rows.empty() && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        for (size_t i = 0; i < line.size(); ++i) {
            const char ch = line[i];
            if (ch == '"') {
                if (quoted && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
                else quoted = !quoted;
            } else if (ch == ',' && !quoted) { fields.push_back(field); field.clear(); }
            else field += ch;
        }
        if (quoted) return {};
        fields.push_back(field);
        rows.push_back(std::move(fields));
    }
    return rows;
}
inline std::vector<std::wstring> Detect(const std::filesystem::path& game) {
    // A Title ID or the presence of voices does not prove a text translation exists.
    // Require an explicit system-language route AND its installed base text archive.
    const auto routes = ReadCsv(game / "SystemLanguage.csv");
    const auto archives = ReadCsv(game / "RegionBuildOption.csv");
    if (routes.empty() || routes.front().size() < 2 ||
        routes.front()[0] != "Category" || routes.front()[1] != "Text") return {};
    std::vector<std::wstring> result;
    for (const auto& language : Languages) {
        const bool routed = std::any_of(routes.begin() + 1, routes.end(), [&](const auto& row) {
            return row.size() >= 2 && row[0] == language.category && row[1] == language.category;
        });
        if (!routed) continue;
        const bool installed = std::any_of(archives.begin(), archives.end(), [&](const auto& row) {
            if (row.size() < 13 || row[7] != "Text" || row[8] != language.category || row[12] != "0") return false;
            const std::filesystem::path name(row[0]);
            if (name.has_parent_path() || name.extension() != ".cpk") return false;
            std::error_code error;
            if (!std::filesystem::is_regular_file(game / name, error) || error) return false;
            const auto size = std::filesystem::file_size(game / name, error);
            return !error && size > 0;
        });
        if (installed) result.emplace_back(language.label);
    }
    return result;
}
}
