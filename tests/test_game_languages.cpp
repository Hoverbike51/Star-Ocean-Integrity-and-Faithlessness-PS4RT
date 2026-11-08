// SPDX-License-Identifier: GPL-2.0-or-later
#include "../launcher/game_languages.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("so5-language-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    const auto put = [&](const char* name, const std::string& text) { std::ofstream(root / name, std::ios::binary) << text; };
    assert(So5Languages::Detect(root).empty());
    const std::string base_routes = "\xef\xbb\xbf" "Category,Text,Voice,Region\r\n"
        "English,English,English,B\r\nFrench,French,English,B\r\nOthers,English,English,B\r\n";
    put("SystemLanguage.csv", base_routes);
    std::string archives;
    for (const auto& language : So5Languages::Languages)
        archives += std::string("text_") + language.category + ".cpk,0,0,0,1,0,0,Text," + language.category + ",14,ps4,0,0\n";
    put("RegionBuildOption.csv", archives);
    assert(So5Languages::Detect(root).empty()); // Metadata without text archives is insufficient.
    put("text_English.cpk", "synthetic text fixture");
    put("text_French.cpk", "synthetic text fixture");
    assert((So5Languages::Detect(root) == std::vector<std::wstring>{L"English", L"Français"}));
    put("text_German.cpk", "synthetic text fixture");
    assert(So5Languages::Detect(root).size() == 2); // A file alone cannot override the game's routing table.
    put("SystemLanguage.csv", base_routes + "German,German,English,B\nItalian,Italian,English,B\nSpanish,Spanish,English,B\n");
    assert(So5Languages::Detect(root).size() == 3);
    put("text_Italian.cpk", "synthetic text fixture");
    put("text_Spanish.cpk", "synthetic text fixture");
    assert((So5Languages::Detect(root) == std::vector<std::wstring>{L"English", L"Français", L"Deutsch", L"Italiano", L"Español"}));
    put("RegionBuildOption.csv", "\"text_English.cpk\",0,0,0,1,0,0,Text,English,14,ps4,0,0\n"
        "text_French.cpk,0,0,0,1,0,0,Text,French,0,ps4,0,1\n"
        "../text_German.cpk,0,0,0,1,0,0,Text,German,14,ps4,0,0\n");
    assert((So5Languages::Detect(root) == std::vector<std::wstring>{L"English"}));
    put("SystemLanguage.csv", "Category,Text\nFrench,English\nOthers,English\n");
    assert(So5Languages::Detect(root).empty()); // Do not advertise a language which falls back to English.
    put("SystemLanguage.csv", "Category,Text\n\"unterminated,French\n");
    assert(So5Languages::Detect(root).empty());
    assert(So5Languages::SystemCode(L"English") == 1);
    assert(So5Languages::SystemCode(L"Français") == 2);
    assert(So5Languages::SystemCode(L"Español") == 3);
    assert(So5Languages::SystemCode(L"Deutsch") == 4);
    assert(So5Languages::SystemCode(L"Italiano") == 5);
    assert(So5Languages::SystemCode(L"日本語") == 0);
    fs::remove_all(root);
    std::cout << "Installed game text-language detection passed\n";
}
