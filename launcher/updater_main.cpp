// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <shellapi.h>
#include "updater.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    const int result = count == 2 && std::wstring(arguments[1]) == L"--self-test" ? 0 : So5Update::InstallerMain(count, arguments);
    LocalFree(arguments);
    return result;
}
