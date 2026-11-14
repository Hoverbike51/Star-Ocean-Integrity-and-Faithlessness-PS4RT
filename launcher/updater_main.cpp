// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <shellapi.h>
#include "updater.h"
#include <cstdio>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    int result = 2;
    try {
        if (count == 2 && std::wstring(arguments[1]) == L"--self-test") result=0;
        else if (count == 4 && std::wstring(arguments[1]) == L"--validate-package") {
            const std::wstring text=arguments[3];
            const auto version=So5Update::Version::Parse(std::string(text.begin(),text.end()));
            if (!version) throw So5Update::Error(So5Update::Code::InvalidPackage,"Invalid package version argument.");
            const auto package=So5Update::ValidatePackage(So5Update::fs::absolute(arguments[2]),*version);
            So5Update::RequireLegacy101Compatibility(package);
            So5Update::ProbePackage(package);
            std::fprintf(stdout,"PASS: player payload, v1.0.1 path compatibility and clean-PATH self-tests.\n");
            result=0;
        } else if (count == 5 && std::wstring(arguments[1]) == L"--validate-archive") {
            const std::wstring text=arguments[3];
            const auto version=So5Update::Version::Parse(std::string(text.begin(),text.end()));
            if (!version) throw So5Update::Error(So5Update::Code::InvalidPackage,"Invalid archive version argument.");
            std::atomic_bool cancel{false};
            const auto package=So5Update::Extract(So5Update::fs::absolute(arguments[2]),
                So5Update::fs::absolute(arguments[4]),*version,cancel,{});
            So5Update::RequireLegacy101Compatibility(package);
            So5Update::ProbePackage(package);
            std::fprintf(stdout,"PASS: final ZIP, v1.0.1 path compatibility and clean-PATH self-tests.\n");
            result=0;
        } else result=So5Update::InstallerMain(count,arguments);
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Package validation failed: %s\n",error.what());
        result=2;
    }
    LocalFree(arguments);
    return result;
}
