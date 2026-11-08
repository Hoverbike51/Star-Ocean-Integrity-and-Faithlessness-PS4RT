// SPDX-License-Identifier: GPL-2.0-or-later
#include "../launcher/shortcuts.h"
#include "../launcher/localization.h"
#include <cassert>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
void Verify(const fs::path& path, const fs::path& target, const fs::path& root,
            const wchar_t* arguments, const fs::path& icon) {
    IShellLinkW* link = nullptr;
    assert(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))));
    IPersistFile* persist = nullptr;
    assert(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist))));
    assert(SUCCEEDED(persist->Load(path.c_str(), STGM_READ)));
    wchar_t value[32768]{};
    assert(SUCCEEDED(link->GetPath(value, 32768, nullptr, SLGP_RAWPATH)));
    assert(fs::equivalent(fs::path(value), target));
    assert(SUCCEEDED(link->GetArguments(value, 32768)) && std::wstring(value) == arguments);
    assert(SUCCEEDED(link->GetWorkingDirectory(value, 32768)) && fs::equivalent(fs::path(value), root));
    int index = -1;
    assert(SUCCEEDED(link->GetIconLocation(value, 32768, &index)) && fs::equivalent(fs::path(value), icon) && index == 0);
    persist->Release();link->Release();
}
int main() {
    using namespace So5Shortcuts;
    wchar_t exe[] = L"C:\\A --play folder\\Star Ocean 5 Launcher.exe";
    wchar_t auto_play[] = L"--play", quick[] = L"--quick-play", other[] = L"--playground";
    wchar_t* args[]{exe, other, auto_play, quick};
    assert(Mode(0, nullptr) == LaunchMode::Interactive);
    assert(Mode(1, args) == LaunchMode::Interactive);
    assert(Mode(2, args) == LaunchMode::Interactive);
    assert(Mode(3, args) == LaunchMode::Autoplay);
    assert(Mode(4, args) == LaunchMode::QuickPlay);
    assert(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    GUID id{};assert(SUCCEEDED(CoCreateGuid(&id)));wchar_t token[40]{};assert(StringFromGUID2(id, token, 40));
    const auto fixture = fs::temp_directory_path() / (std::wstring(L"SO5 shortcuts é ") + token);
    assert(fs::create_directory(fixture));
    const auto root = fixture / L"Player folder", desktop = fixture / L"Desktop fixture";
    fs::create_directories(root / L"bin");fs::create_directory(desktop);
    const auto bootstrap = root / L"Star Ocean 5 Launcher.exe";
    const auto launcher = root / L"bin/Star Ocean 5 Launcher.exe";
    const auto runtime = root / L"bin/Star Ocean 5.exe";
    std::ofstream(bootstrap) << "fixture";std::ofstream(launcher) << "fixture";std::ofstream(runtime) << "fixture";
    for (int repeat = 0; repeat < 2; ++repeat) {
        assert(SUCCEEDED(Create(desktop, root, launcher, runtime)));
        Verify(desktop / LauncherName, bootstrap, root, L"", bootstrap);
        Verify(desktop / GameName, bootstrap, root, L"--quick-play", runtime);
    }
    assert(std::distance(fs::directory_iterator(desktop), fs::directory_iterator()) == 2);
    fs::remove(bootstrap);
    assert(SUCCEEDED(Create(desktop, root, launcher, runtime)));
    Verify(desktop / GameName, launcher, root, L"--quick-play", runtime);
    assert(FAILED(Create(fixture / L"missing directory", root, launcher, runtime)));
    const wchar_t* languages[]{L"English",L"Français",L"Español",L"Português",L"Português (Brasil)",L"Italiano",L"Deutsch",L"Русский",L"简体中文",L"繁體中文",L"한국어",L"日本語"};
    for (auto language : languages) {
        const auto text = ps4rt_localize(language, L"Two desktop shortcuts created");
        assert(text && *text);
        if (std::wstring(language) != L"English") assert(std::wstring(text) != L"Two desktop shortcuts created");
    }
    fs::remove_all(fixture);CoUninitialize();
    std::cout << "PASS: both real Shell links, installed and source layouts, Unicode paths, repeat creation, exact arguments and 12 languages\n";
}
