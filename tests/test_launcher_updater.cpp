// SPDX-License-Identifier: GPL-2.0-or-later
// All installation targets are uniquely named disposable temporary fixtures.
#include "updater.h"
#include "ps4rt_version.h"
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <miniz.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace U = So5Update;
namespace fs = std::filesystem;
const U::Version Current = *U::Version::Parse(PS4RT_VERSION_STRING);
std::atomic_bool cancel{false};

std::string Read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    assert(input); return {std::istreambuf_iterator<char>(input), {}};
}
void Write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path()); std::ofstream output(file, std::ios::binary); output << text; assert(output.good());
}
std::string Utf8(const fs::path& path) { auto text = path.generic_u8string(); return {text.begin(), text.end()}; }
std::wstring Quote(const fs::path& path) { return L"\"" + path.wstring() + L"\""; }
template<class F> void Reject(F run) {
    bool failed = false; try { run(); } catch (const U::Error&) { failed = true; } assert(failed);
}
void Zip(const fs::path& file, const std::vector<std::pair<std::string, std::string>>& entries) {
    FILE* output = _wfopen(file.c_str(), L"wb"); assert(output);
    mz_zip_archive zip{}; assert(mz_zip_writer_init_cfile(&zip, output, 0));
    for (const auto& [name, value] : entries) assert(mz_zip_writer_add_mem(&zip, name.c_str(), value.data(), value.size(), MZ_BEST_SPEED));
    assert(mz_zip_writer_finalize_archive(&zip)); assert(mz_zip_writer_end(&zip)); assert(std::fclose(output) == 0);
}
std::string Release(const std::string& version, bool asset = true, bool digest = true, bool preview = false) {
    const std::string name = std::string(U::PackagePrefix) + version + ".zip";
    return "{\"tag_name\":\"v" + version + "\",\"draft\":false,\"prerelease\":" + (preview ? "true" : "false") +
        ",\"assets\":[" + (asset ? "{\"name\":\"" + name + "\",\"state\":\"uploaded\",\"size\":123,\"browser_download_url\":\"https://github.com/" +
        U::Repository + "/releases/download/v" + version + "/" + name + "\",\"digest\":\"" +
        (digest ? "sha256:" + std::string(64, 'a') : "") + "\"}" : "") + "]}";
}
void LatestMetadata() {
    const auto old=U::Version::Parse("0.3.5"); assert(old);
    std::string json="["+Release(Current.String())+"]";
    const std::string old_name=std::string(U::PackagePrefix)+Current.String()+".zip";
    const std::string name=std::string(U::PackagePrefix)+Current.String()+"-latest.zip";
    for (size_t at=0;(at=json.find(old_name,at))!=std::string::npos;at+=name.size()) json.replace(at,old_name.size(),name);
    const auto result=U::ParseReleases(json,*old,true);
    assert(result.code==U::Code::Available && result.release && result.release->asset_name==name);
}
void Metadata() {
    LatestMetadata();
    const auto baseline = *U::Version::Parse("0.4.0");
    assert(!U::Version::Parse("1.2") && !U::Version::Parse("1.2.3.4") && !U::Version::Parse("0.4.1-beta"));
    assert(!U::Version::Parse("0.04.1") && !U::Version::Parse("0.4.70000") && !U::Version::Parse("0.4.-1"));
    assert(U::Version::Parse("0.4.10") > U::Version::Parse("0.4.9"));
    assert(U::ParseReleases("[]", Current, true).code == U::Code::NoPackage);
    assert(U::ParseReleases("[" + Release("0.3.5") + "]", Current, true).code == U::Code::Current);
    assert(U::ParseReleases("[" + Release(Current.String()) + "]", Current, true).code == U::Code::Current);
    auto next = U::ParseReleases("[" + Release("0.4.1") + "," + Release("0.4.10") + "," + Release("0.4.2") + "]", baseline, true);
    assert(next.code == U::Code::Available && next.release->version.String() == "0.4.10");
    assert(U::ParseReleases("[" + Release("0.4.1", false) + "]", baseline, true).code == U::Code::NoPackage);
    assert(U::ParseReleases("[" + Release("0.4.1", true, false) + "]", baseline, true).code == U::Code::NoDigest);
    assert(U::ParseReleases("[" + Release("0.4.1", true, true, true) + "]", baseline, false).code != U::Code::Available);
    assert(U::ParseReleases("[" + Release("0.4.1", true, true, true) + "]", baseline, true).code == U::Code::Available);
    std::string hostile = Release("0.4.1"); hostile.replace(hostile.find("https://github.com/"), 19, "https://example.org/");
    assert(U::ParseReleases("[" + hostile + "]", baseline, true).code == U::Code::NoPackage);
    assert(U::ParseReleases("{\"message\":\"rate limit\"}", Current, true).code == U::Code::InvalidResponse);
    assert(U::ParseReleases("[", Current, true).code == U::Code::InvalidResponse);
    assert(U::ParseReleases(std::string(40, '[') + std::string(40, ']'), Current, true).code == U::Code::InvalidResponse);
    cancel = true; assert(U::Check(Current, true, cancel).code == U::Code::Canceled); cancel = false;
    std::cout << "PASS: numerical versions, prerelease policy, Windows assets, digests, invalid replies, cancellation\n";
}
void Paths() {
    assert(U::ProgramFile("docs/AMD_FSR_SDK.md"));
    for(const char* file:{"amd_fidelityfx_loader_vk.dll","amd_fidelityfx_upscaler_vk.dll",
            "msvcp140.dll","vcruntime140.dll","vcruntime140_1.dll"})
        assert(U::ProgramFile(fs::path("bin/amd-fsr-sdk24")/file));
    assert(!U::ProgramFile("bin/amd-fsr-sdk24/unknown.dll"));
    assert(!U::ProgramFile("bin/amd-fsr-sdk24/tools/injected.dll"));
    assert(!U::ProgramFile("bin/amd-fsr-sdk24/installer.exe"));
    for (const char* name : {"../secret", "root/../secret", "root\\file", "/absolute", "C:/file", "root/file:stream",
        "root/NUL.txt", "root/COM1", "root/con.", "root/file. ", "root//file", "root/file?", "root/./file"})
        Reject([&] { U::EntryPath(name); });
    assert(!U::ProgramFile("out/launcher.ini") && !U::ProgramFile("user/save.dat") && !U::ProgramFile("mods/custom.txt"));
    assert(U::ProgramFile("launcher/assets/background.png") && U::ProgramFile("Star Ocean 5.exe"));
    assert(U::ProgramFile("bin/Star Ocean 5.exe") && U::ProgramFile("bin/Star Ocean 5 Launcher.exe"));
    assert(U::ProgramFile("bin/SO5-Updater.exe") && U::ProgramFile("bin/SDL3.dll"));
    assert(!U::ProgramFile("bin/unknown.exe") && !U::ProgramFile("bin/nested/payload.dll"));
    assert(U::ProgramFile("CHANGELOG.md") && U::ProgramFile("docs/LAUNCHER_GUIDE.md"));
    assert(U::ProgramFile("docs/IN_GAME_MENU_GUIDE.md"));
    assert(U::ProgramFile("docs/images/readme/launcher/1_Cockpit_launcher_home.png"));
    assert(U::ProgramFile("docs/images/readme/custom_menu/1_ingame_custom_display.png"));
    assert(!U::ProgramFile("docs/images/readme/launcher/payload.exe"));
    assert(!U::ProgramFile("docs/images/private/capture.png"));
    std::cout << "PASS: Windows path traversal, streams, device aliases and protected user directories\n";
}
struct Fixture {
    fs::path parent = fs::absolute(fs::temp_directory_path()).lexically_normal(), root;
    Fixture() {
        GUID guid{}; assert(SUCCEEDED(CoCreateGuid(&guid))); wchar_t token[40]{}; StringFromGUID2(guid, token, 40);
        root = parent / (std::wstring(L"updater-tests-") + token + L"-é 日本語"); assert(fs::create_directory(root));
    }
    ~Fixture() {
        assert(root.is_absolute() && root.parent_path() == parent && root.filename().wstring().starts_with(L"updater-tests-{"));
        std::error_code ignored; fs::remove_all(root, ignored);
    }
};
std::map<fs::path, std::string> UserFiles() {
    return {{"out/launcher.ini", "user settings"}, {"out/CUSA03219/user/savedata/save.dat", "private save"},
        {"out/CUSA03219/user/gpu/cache.bin", "warm shader cache"}, {"mods/custom.txt", "custom mod"}, {"game/eboot.bin", "private game"}};
}
void Populate(const fs::path& target) {
    Write(target / "VERSION", "0.3.5\n"); Write(target / "Star Ocean 5 Launcher.exe", "old launcher"); Write(target / "README.md", "old readme");
    for (const auto& [relative, value] : UserFiles()) Write(target / relative, value);
}
void Preserved(const fs::path& target) { for (const auto& [relative, value] : UserFiles()) assert(Read(target / relative) == value); }
void Transactions(const fs::path& root) {
    const auto stage = root / "transaction-stage";
    Write(stage / "VERSION", Current.String() + "\n"); Write(stage / "Star Ocean 5 Launcher.exe", "new launcher");
    Write(stage / "README.md", "new readme"); Write(stage / "new.dll", "new dependency");
    U::Package package{stage, "Star Ocean 5 Launcher.exe", Current, {"Star Ocean 5 Launcher.exe", "VERSION", "README.md", "new.dll"}};
    const auto target = root / "rollback"; Populate(target);
    auto failed = U::Install(package, target, target / "out/updates/test/previous", [](size_t count) {
        if (count == 4) throw std::runtime_error("Injected interruption after four replacements");
    });
    assert(!failed.success && failed.recovered && Read(target / "VERSION") == "0.3.5\n");
    assert(Read(target / "Star Ocean 5 Launcher.exe") == "old launcher" && !fs::exists(target / "new.dll")); Preserved(target);
    assert(Read(failed.backup / "Star Ocean 5 Launcher.exe") == "old launcher" && fs::exists(failed.backup / "transaction.json"));
    HANDLE locked = CreateFileW((target / "Star Ocean 5 Launcher.exe").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    auto in_use = U::Install(package, target, target / "out/updates/locked/previous");
    assert(!in_use.success && in_use.recovered && in_use.retryable && Read(target / "VERSION") == "0.3.5\n");
    assert(in_use.detail.find("Star Ocean 5 Launcher.exe")!=std::string::npos);CloseHandle(locked);
    auto success = U::Install(package, target, target / "out/updates/success/previous");
    assert(success.success && Read(target / "VERSION") == Current.String() + "\n" && Read(target / "new.dll") == "new dependency"); Preserved(target);
    assert(!U::Install(package, target, target / "out/updates/equal/previous").success);
    Write(target / "build-windows.ps1", "source checkout"); assert(!U::PlayerInstall(target));
    assert(!U::Install(package, target, target / "out/updates/source/previous").success); Preserved(target);
    const auto legacy=root/"legacy names"; Populate(legacy);
    fs::remove(legacy/"Star Ocean 5 Launcher.exe");
    Write(legacy/"PS4RT-Launcher.exe","old launcher"); Write(legacy/"ps4rt.exe","old runtime");
    Write(legacy/"PS4RT-PkgExtractor.exe","old extractor");
    Write(stage/"Star Ocean 5.exe","new runtime"); Write(stage/"PkgExtractor.exe","new extractor");
    auto renamed=package; renamed.files.push_back("Star Ocean 5.exe"); renamed.files.push_back("PkgExtractor.exe");
    auto restore_old=U::Install(renamed,legacy,legacy/"out/updates/rollback/previous",[](size_t count){
        if(count==7) throw std::runtime_error("Interrupted after retiring an old executable");
    });
    assert(!restore_old.success && restore_old.recovered && Read(legacy/"ps4rt.exe")=="old runtime");
    assert(Read(legacy/"PS4RT-Launcher.exe")=="old launcher" && !fs::exists(legacy/"Star Ocean 5.exe")); Preserved(legacy);
    auto migrated=U::Install(renamed,legacy,legacy/"out/updates/success/previous");
    assert(migrated.success && Read(legacy/"Star Ocean 5.exe")=="new runtime");
    assert(!fs::exists(legacy/"ps4rt.exe") && !fs::exists(legacy/"PS4RT-Launcher.exe") && !fs::exists(legacy/"PS4RT-PkgExtractor.exe"));
    assert(Read(migrated.backup/"ps4rt.exe")=="old runtime"); Preserved(legacy);
    const auto flat=root/"flat to bin"; Populate(flat);
    Write(flat/"Star Ocean 5.exe","flat runtime"); Write(flat/"SDL3.dll","flat dependency");
    Write(stage/"bin/Star Ocean 5.exe","bin runtime"); Write(stage/"bin/SDL3.dll","bin dependency");
    auto bin_package=package; bin_package.files.push_back("bin/Star Ocean 5.exe"); bin_package.files.push_back("bin/SDL3.dll");
    const auto relocated=U::Install(bin_package,flat,flat/"out/updates/bin/previous");
    assert(relocated.success && !fs::exists(flat/"Star Ocean 5.exe") && !fs::exists(flat/"SDL3.dll"));
    assert(Read(flat/"bin/Star Ocean 5.exe")=="bin runtime" && Read(relocated.backup/"SDL3.dll")=="flat dependency");
    assert(fs::exists(flat/"Star Ocean 5 Launcher.exe")); Preserved(flat);
    std::cout << "PASS: atomic installation, rollback, locked files, legacy filename migration, no downgrade and user-data preservation\n";
}
void Archives(const fs::path& root) {
    const auto zip = root / "fixture.zip";
    Zip(zip, {{"root/../escape.txt", "bad"}});
    Reject([&] { U::Extract(zip, root / "traversal", Current, cancel, {}); }); assert(!fs::exists(root / "escape.txt"));
    Zip(zip, {{"root/out/launcher.ini", "bad"}});
    Reject([&] { U::Extract(zip, root / "protected", Current, cancel, {}); });
    Zip(zip, {{"root/README.md", "one"}, {"root/readme.md", "two"}});
    Reject([&] { U::Extract(zip, root / "duplicate", Current, cancel, {}); });
    Zip(zip, {{"root/README.md", "source archive"}});
    bool incomplete = false; try { U::Extract(zip, root / "source", Current, cancel, {}); } catch (const std::exception&) { incomplete = true; } assert(incomplete);
    Write(root / "hash", "abc"); assert(U::Sha256(root / "hash", cancel) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    cancel = true; Reject([&] { U::Sha256(root / "hash", cancel); }); cancel = false;
    std::cout << "PASS: native SHA-256, traversal/protected ZIP rejection, Windows case collisions and incomplete packages\n";
}
fs::path Module() { wchar_t module[32768]{}; GetModuleFileNameW(nullptr, module, 32768); return module; }
U::Package PlayerPackage(const fs::path& root, const fs::path& binaries) {
    fs::create_directories(root / "bin");
    fs::copy_file(binaries / "bootstrap/Star Ocean 5 Launcher.exe", root / "Star Ocean 5 Launcher.exe");
    for (const char* name : {"Star Ocean 5.exe", "Star Ocean 5 Launcher.exe", "PkgExtractor.exe"}) fs::copy_file(Module(), root / "bin" / name);
    fs::copy_file(binaries / "SO5-Updater.exe", root / "bin/SO5-Updater.exe");
    for (const char* name : {"SDL3.dll", "libc++.dll", "libunwind.dll", "libwinpthread-1.dll"}) fs::copy_file(binaries / name, root / "bin" / name);
    Write(root / "VERSION", Current.String() + "\n");
    for (const char* name : {"launcher/assets/launcher-background.png", "launcher/assets/launcher-icon.png", "launcher/assets/star-ocean-logo.png",
        "gpu/assets/star_ocean_overlay.png", "LICENSE"}) Write(root / name, "fixture data");
    Write(root / L"launcher/assets/背景.png", "non-ASCII asset");
    auto package = U::ValidatePackage(root, Current); U::ProbePackage(package); return package;
}
void Handoff(const fs::path& root, const fs::path& binaries) {
    const auto workspace = root / "so5-update-{handoff}";
    Write(workspace / "workspace.marker", "SO5 update workspace v1\n");
    auto package = PlayerPackage(workspace / "package", binaries);
    fs::rename(package.root / "bin/libc++.dll", package.root / "bin/libc++.disabled");
    const auto missing_dll_started = GetTickCount64();
    Reject([&] { U::ProbePackage(package); });
    assert(GetTickCount64() - missing_dll_started < 5000);
    fs::rename(package.root / "bin/libc++.disabled", package.root / "bin/libc++.dll");
    // Full native extraction and resource validation, with non-ASCII staging paths.
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& file : package.files) entries.push_back({"player/" + Utf8(file), Read(package.root / file)});
    Zip(root / "player-fixture.zip", entries);
    auto extracted = U::Extract(root / "player-fixture.zip", root / "extracted", Current, cancel, {});
    U::ProbePackage(extracted);
    assert(Read(extracted.root / L"launcher/assets/背景.png") == "non-ASCII asset");
    auto wrong_version = Current; ++wrong_version.parts[2];
    Reject([&] { U::ValidatePackage(extracted.root, wrong_version); });
    U::Release release;
    release.version = Current; release.tag = "v" + Current.String();
    release.asset_name = std::string(U::PackagePrefix) + Current.String() + ".zip";
    release.download = "https://github.com/" + std::string(U::Repository) + "/releases/download/" + release.tag + "/" + release.asset_name;
    const auto archive = root / "player-fixture.zip";
    release.sha256 = U::Sha256(archive, cancel); release.size = fs::file_size(archive);
    const auto bytes = Read(archive);
    const U::Transfer transfer = [&](const auto&, auto, const auto&, const auto& receive) {
        for (size_t at = 0; at < bytes.size(); at += 65536) receive(bytes.data() + at, std::min<size_t>(65536, bytes.size() - at));
    };
    const auto downloads = root / "downloads";
    bool ready = false;
    auto prepared = U::Prepare(release, downloads, cancel, [&](U::Phase phase, auto done, auto total) {
        assert(done <= total || total == 0); if (phase == U::Phase::Ready) ready = true;
    }, transfer);
    assert(ready); U::RemoveWorkspace(prepared.workspace); assert(fs::is_empty(downloads));
    auto mismatch = release; mismatch.sha256 = std::string(64, '0');
    Reject([&] { U::Prepare(mismatch, downloads, cancel, {}, transfer); }); assert(fs::is_empty(downloads));
    Reject([&] { U::Prepare(release, downloads, cancel, {}, [&](const auto&, auto, const auto&, const auto& receive) {
        receive(bytes.data(), bytes.size() - 1);
    }); }); assert(fs::is_empty(downloads));
    Reject([&] { U::Prepare(release, downloads, cancel, {}, [&](const auto&, auto, const auto&, const auto& receive) {
        cancel = true; receive(bytes.data(), 1);
    }); }); cancel = false; assert(fs::is_empty(downloads));
    std::cout << "PASS: complete transfer pipeline, progress, SHA mismatch, truncated download and cancellation cleanup\n";
    const auto target = root / "player";
    fs::copy(package.root, target, fs::copy_options::recursive); Write(target / "VERSION", "0.3.5\n");
    // The installed helper is intentionally obsolete. Only the verified staged
    // helper may perform the new transaction, from outside the installation.
    Write(target / "bin/SO5-Updater.exe", "obsolete helper");
    for (const auto& [path, value] : UserFiles()) Write(target / path, value);
    HANDLE unloading=CreateFileW((target/"bin/Star Ocean 5 Launcher.exe").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(unloading!=INVALID_HANDLE_VALUE);
    std::thread release_lock([unloading]{Sleep(2000);CloseHandle(unloading);});
    std::wstring command = Quote(target / "Star Ocean 5 Launcher.exe") + L" --handoff-parent " + Quote(workspace) + L" " + Quote(target);
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    assert(CreateProcessW((target / "Star Ocean 5 Launcher.exe").c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, target.c_str(), &startup, &process));
    assert(WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0); DWORD code = 9;
    GetExitCodeProcess(process.hProcess, &code); assert(code == 0); CloseHandle(process.hThread); CloseHandle(process.hProcess);
    const ULONGLONG deadline = GetTickCount64() + 15000;
    while (!fs::exists(target / "relaunched.marker") && GetTickCount64() < deadline) Sleep(20);
    assert(fs::exists(target / "relaunched.marker") && Read(target / "VERSION") == Current.String() + "\n"); Preserved(target);
    release_lock.join();
    const auto result = U::ReadInstallResult(target);
    assert(result && result->success && fs::exists(result->backup / "transaction.json"));
    assert(!U::ReadInstallResult(target));
    std::cout << "PASS: native installer handshake, launcher shutdown, installation, restart and result acknowledgement\n";
}
int main() {
    std::cout << std::unitbuf;
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    struct FreeArgs { wchar_t** args; ~FreeArgs() { LocalFree(args); } } free_args{argv};
    if (argc == 2 && (std::wstring(argv[1]) == L"--self-test" || std::wstring(argv[1]) == L"--cpu-features")) return 0;
    if (argc == 2 && std::wstring(argv[1]) == L"--update-result") { Write(fs::current_path() / "relaunched.marker", "restarted"); return 0; }
    if (argc == 4 && std::wstring(argv[1]) == L"--handoff-parent") {
        const fs::path workspace = argv[2], target = argv[3];
        U::Prepared prepared{workspace, U::ValidatePackage(workspace / "package", Current)};
        U::StartInstaller(prepared, target, Module(), GetCurrentProcessId()); return 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--live-check") {
        const auto result = U::Check(Current, true, cancel);
        std::cout << "Live GitHub check: " << int(result.code) << " | " << result.detail << '\n';
        return result.code == U::Code::NoPackage || result.code == U::Code::Current || result.code == U::Code::Available ? 0 : 1;
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--validate-package") {
        const auto package = U::ValidatePackage(fs::absolute(argv[2]), Current);
        U::ProbePackage(package);
        std::cout << "PASS: real player-folder resources, " << package.files.size() << " allowed program files, and clean-PATH launcher probe\n";
        return 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--paths-only") { Paths(); return 0; }
    assert(argc == 2); const fs::path binaries = fs::absolute(argv[1]);
    Fixture fixture; Metadata(); Paths(); Transactions(fixture.root); Archives(fixture.root); Handoff(fixture.root, binaries);
    return 0;
}
