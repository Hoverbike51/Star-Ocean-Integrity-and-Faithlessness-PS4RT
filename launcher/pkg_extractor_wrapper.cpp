#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\'); result.push_back(L'\"');
        } else {
            result.append(slashes, L'\\'); result.push_back(c);
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\'); result.push_back(L'\"');
    return result;
}

static fs::path executable_directory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return fs::path(path).parent_path();
}

int wmain(int argc, wchar_t** argv) {
    const fs::path tool = executable_directory() / "tools" / "pkg-extractor" / "pkg_extractor.exe";
    if (!fs::is_regular_file(tool)) {
        fwprintf(stderr, L"ERROR: missing bundled extractor: %ls\n", tool.c_str());
        return 2;
    }
    std::wstring command = quote(tool.wstring());
    for (int i = 1; i < argc; ++i) command += L" " + quote(argv[i]);
    std::vector<wchar_t> mutable_command(command.begin(), command.end()); mutable_command.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                        tool.parent_path().c_str(), &startup, &process);
    if (!created) {
        fwprintf(stderr, L"ERROR: could not start bundled extractor (Windows code %lu)\n", GetLastError());
        return 3;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(process.hProcess, &code); CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
