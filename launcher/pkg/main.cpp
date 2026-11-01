// Copyright (c) 2026 HoverMods Vault
// SPDX-License-Identifier: GPL-2.0-or-later
#include "archive.h"
#include "ps4rt_version.h"
#include <windows.h>
#include <cstdio>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc==2 && (std::wstring(argv[1])==L"--version" || std::wstring(argv[1])==L"--self-test")) {
        std::printf("HoverMods Vault PKG Extractor %s\n",PS4RT_VERSION_STRING); return 0;
    }
    try {
        HmvPkg::Options options; std::vector<std::filesystem::path> arguments;
        for (int i=1;i<argc;++i) {
            const std::wstring value=argv[i];
            if (value==L"--list") options.list=true;
            else if (value==L"--verify-tree") options.verify=true;
            else if (value==L"--title-folder") options.title_folder=true;
            else if (value==L"--only" && i+1<argc) {
                auto bytes=std::filesystem::path(argv[++i]).generic_u8string();
                options.only.emplace_back(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            } else if (value==L"--passcode-file" && i+1<argc) {
                std::ifstream file(std::filesystem::path(argv[++i]),std::ios::binary);
                if (!file) throw std::runtime_error("Cannot read the passcode file.");
                std::array<char,35> bytes{}; file.read(bytes.data(),bytes.size());
                if (file.gcount()>34) throw std::runtime_error("The passcode file is too long.");
                std::string secret(bytes.data(),size_t(file.gcount()));
                while (!secret.empty() && (secret.back()=='\n' || secret.back()=='\r')) secret.pop_back();
                if (secret.size()!=32) throw std::runtime_error("The passcode file must contain exactly 32 characters.");
                options.passcode=std::move(secret);
            } else if (value.starts_with(L"--")) throw std::runtime_error("Unknown extractor option.");
            else arguments.emplace_back(argv[i]);
        }
        if (arguments.size()!=(options.list?1u:2u)) {
            std::fputs("Usage: PkgExtractor.exe PACKAGE.pkg EMPTY_OUTPUT_DIRECTORY\n"
                       "       PkgExtractor.exe --list PACKAGE.pkg\n"
                       "       PkgExtractor.exe --verify-tree PACKAGE.pkg EXISTING_DIRECTORY [--only relative/file]\n"
                       "Optional: --title-folder (extract into OUTPUT_DIRECTORY/TITLE_ID).\n"
                       "          --passcode-file FILE (the 32-character creation passcode; never printed).\n",stderr);
            return 2;
        }
        ULONGLONG last=0;
        options.planned=[](const HmvPkg::Summary& plan) {
            if(!plan.staging_name.empty()) std::printf("PKG staging: %s\n",plan.staging_name.c_str());
            if(!plan.title_id.empty()) std::printf("PKG title: %s\n",plan.title_id.c_str());
            std::fflush(stdout);
        };
        options.progress=[&](std::uint64_t done,std::uint64_t total) {
            auto now=GetTickCount64(); if (done && now-last<250 && done!=total) return; last=now;
            std::printf("PKG progress: %llu / %llu bytes (%llu%%)\n",
                static_cast<unsigned long long>(done),static_cast<unsigned long long>(total),
                static_cast<unsigned long long>(total?done*100/total:100)); std::fflush(stdout);
        };
        auto result=HmvPkg::Process(arguments[0],options.list?std::filesystem::path{}:arguments[1],options);
        std::printf("PKG %s complete: %llu files, %llu bytes\n",options.verify?"verification":options.list?"listing":"extraction",
                    static_cast<unsigned long long>(result.files),static_cast<unsigned long long>(result.bytes));
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"ERROR: %s\n",error.what()); return 3; }
}
