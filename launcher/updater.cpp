// SPDX-License-Identifier: GPL-2.0-or-later
#include "updater.h"
#include "ps4rt_version.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <objbase.h>
#include <boost/property_tree/json_parser.hpp>
#include <miniz.h>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace So5Update {
namespace {
using Tree = boost::property_tree::ptree;
void Canceled(const std::atomic_bool& cancel) {
    if (cancel.load()) throw Error(Code::Canceled, "Update canceled.");
}
std::string WinError(const char* operation) {
    const DWORD error = GetLastError();
    return std::string(operation) + " (Windows error " + std::to_string(error) + ")";
}
std::wstring Wide(const std::string& value) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!size && !value.empty()) throw Error(Code::InvalidPackage, "Invalid UTF-8 text.");
    std::wstring result(size, L'\0');
    if (size) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), size);
    return result;
}
std::string Lower(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
bool Digest(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
struct Internet {
    HINTERNET handle{};
    explicit Internet(HINTERNET value) : handle(value) {}
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
    Internet(const Internet&) = delete;
    operator HINTERNET() const { return handle; }
};
struct File {
    FILE* handle;
    File(const fs::path& path, const wchar_t* mode) : handle(_wfopen(path.c_str(), mode)) {
        if (!handle) throw Error(Code::Disk, "Could not open update file: " + path.filename().string());
    }
    ~File() { if (handle) std::fclose(handle); }
    void Close() {
        FILE* value = handle; handle = nullptr;
        if (std::fclose(value)) throw Error(Code::Disk, "Could not finish writing update file.");
    }
};
void Plain(const fs::path& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error(Code::InvalidPackage, "Update paths must not contain reparse points.");
}
void PlainUnder(const fs::path& root, const fs::path& relative) {
    Plain(root);
    fs::path current = root;
    for (const auto& component : relative) { current /= component; Plain(current); }
}
struct CaseLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_LESS_THAN;
    }
};
Tree ReadJson(const std::string& text) {
    // GitHub replies are bounded before parsing. Reject excessive nesting before the
    // recursive JSON parser; release notes are strings and do not contribute to depth.
    unsigned depth = 0; bool quoted = false, escaped = false;
    for (char c : text) {
        if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 32) throw Error(Code::InvalidResponse, "JSON nesting limit exceeded."); }
        else if (c == '}' || c == ']') { if (!depth) throw Error(Code::InvalidResponse, "Invalid JSON nesting."); --depth; }
    }
    Tree tree; std::istringstream stream(text); boost::property_tree::read_json(stream, tree); return tree;
}
void HttpGet(const std::wstring& url, std::uint64_t maximum, const std::atomic_bool& cancel,
             const std::function<void(const char*, size_t)>& sink) {
    Canceled(cancel);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, ICU_REJECT_USERPWD, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT)
        throw Error(Code::Network, "Invalid HTTPS update URL.");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
        std::wstring(parts.lpszExtraInfo ? parts.lpszExtraInfo : L"", parts.dwExtraInfoLength);
    const std::wstring agent = L"StarOcean5-Windows/" PS4RT_VERSION_WSTRING;
    Internet session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.handle) throw Error(Code::Network, WinError("WinHttpOpen"));
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 8000);
    Internet connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    if (!connection.handle) throw Error(Code::Network, WinError("WinHttpConnect"));
    Internet request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.handle) throw Error(Code::Network, WinError("WinHttpOpenRequest"));
    // WinHTTP follows HTTPS redirects (GitHub's release CDN) without weakening TLS.
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    const wchar_t* headers = host == L"api.github.com"
        ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
        : L"Accept: application/octet-stream\r\n";
    if (!WinHttpSendRequest(request, headers, DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        Canceled(cancel); throw Error(Code::Network, WinError("GitHub request failed"));
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        throw Error(Code::Network, WinError("HTTP status unavailable"));
    if (status != 200) {
        bool limited = status == 429;
        if (status == 403) {
            wchar_t remaining[64]{}; DWORD length = sizeof(remaining);
            limited = WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"X-RateLimit-Remaining", remaining, &length, nullptr) &&
                      std::wstring(remaining) == L"0";
            length = sizeof(remaining);
            limited = limited || WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Retry-After", remaining, &length, nullptr);
        }
        throw Error(limited ? Code::RateLimit : Code::Http, "GitHub returned HTTP " + std::to_string(status));
    }
    std::uint64_t received = 0;
    std::array<char, 65536> block;
    for (;;) {
        Canceled(cancel);
        DWORD got = 0;
        if (!WinHttpReadData(request, block.data(), DWORD(block.size()), &got))
            throw Error(Code::Network, WinError("Update transfer interrupted"));
        if (!got) break;
        received += got;
        if (received > maximum) throw Error(Code::Integrity, "Update response exceeds its declared size limit.");
        sink(block.data(), got);
    }
    Canceled(cancel);
}
std::string ReadSmallFile(const fs::path& path, size_t max = 8 * 1024 * 1024) {
    if (fs::file_size(path) > max) throw Error(Code::InvalidPackage, "Update metadata is too large.");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw Error(Code::Disk, "Could not read update metadata.");
    return {std::istreambuf_iterator<char>(file), {}};
}
void CheckVersion(const fs::path& path, Version expected) {
    if (!fs::is_regular_file(path)) throw Error(Code::InvalidPackage, "Missing update executable: " + path.filename().string());
    {
        File file(path, L"rb"); IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
        if (std::fread(&dos, 1, sizeof(dos), file.handle) != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < LONG(sizeof(dos)) || std::uint64_t(dos.e_lfanew) + sizeof(nt) > fs::file_size(path) ||
            _fseeki64(file.handle, dos.e_lfanew, SEEK_SET) || std::fread(&nt, 1, sizeof(nt), file.handle) != sizeof(nt) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            throw Error(Code::InvalidPackage, "Update executable is not Windows x64: " + path.filename().string());
    }
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > 1024 * 1024) throw Error(Code::InvalidPackage, "Missing or oversized version resource.");
    std::vector<char> bytes(size);
    VS_FIXEDFILEINFO* info = nullptr; UINT length = 0;
    if (!size || !GetFileVersionInfoW(path.c_str(), 0, size, bytes.data()) ||
        !VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        length < sizeof(*info) || info->dwSignature != 0xfeef04bd ||
        HIWORD(info->dwFileVersionMS) != expected.parts[0] || LOWORD(info->dwFileVersionMS) != expected.parts[1] ||
        HIWORD(info->dwFileVersionLS) != expected.parts[2] || LOWORD(info->dwFileVersionLS) != 0)
        throw Error(Code::InvalidPackage, "Incorrect executable version: " + path.filename().string());
}
} // namespace

std::string Version::String() const {
    return std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]);
}
std::optional<Version> Version::Parse(const std::string& text) {
    size_t start = !text.empty() && text[0] == 'v' ? 1 : 0;
    Version result;
    for (size_t i = 0; i < 3; ++i) {
        const size_t end = text.find('.', start);
        const size_t stop = end == std::string::npos ? text.size() : end;
        if (start == stop || (i < 2 && end == std::string::npos) || (i == 2 && end != std::string::npos) ||
            (stop - start > 1 && text[start] == '0')) return {};
        const auto parsed = std::from_chars(text.data() + start, text.data() + stop, result.parts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + stop || result.parts[i] > 65535) return {};
        start = stop + 1;
    }
    return result;
}

CheckResult ParseReleases(const std::string& json, Version installed, bool allow_experimental) {
    try {
        if (json.size() > 8 * 1024 * 1024 || json.find_first_not_of(" \t\r\n") == std::string::npos ||
            json[json.find_first_not_of(" \t\r\n")] != '[')
            throw Error(Code::InvalidResponse, "Expected a GitHub releases array.");
        const Tree tree = ReadJson(json);
        CheckResult best{Code::NoPackage, {}, "No published Windows x64 package is available."};
        for (const auto& [key, item] : tree) {
            if (!key.empty()) throw Error(Code::InvalidResponse, "Invalid releases array.");
            const auto version = Version::Parse(item.get<std::string>("tag_name", ""));
            const bool preview = item.get<bool>("prerelease", false);
            if (!version || item.get<bool>("draft", true) || (preview && !allow_experimental)) continue;
            if (*version <= installed) {
                if (!best.release) best = {Code::Current, {}, "No newer Windows release is available."};
                continue;
            }
            if (best.release && best.release->version >= *version) continue;
            Release release;
            release.version = *version; release.tag = item.get<std::string>("tag_name"); release.experimental = preview;
            release.page = "https://github.com/" + std::string(Repository) + "/releases/tag/" + release.tag;
            release.asset_name = std::string(PackagePrefix) + version->String() + ".zip";
            Code code = Code::NoPackage;
            if (const auto assets = item.get_child_optional("assets")) {
                for (const auto& [unused, asset] : *assets) {
                    const std::string asset_name=asset.get<std::string>("name", "");
                    const std::string base=std::string(PackagePrefix)+version->String();
                    if ((asset_name != base+".zip" && asset_name != base+"-latest.zip") ||
                        asset.get<std::string>("state", "") != "uploaded") continue;
                    if (release.asset_name.ends_with("-latest.zip") && release.size) continue;
                    const std::string url = "https://github.com/" + std::string(Repository) +
                        "/releases/download/" + release.tag + "/" + asset_name;
                    if (asset.get<std::string>("browser_download_url", "") != url) continue;
                    const auto size = asset.get<std::uint64_t>("size", 0);
                    if (!size || size > MaxDownload) continue;
                    release.asset_name=asset_name; release.size = size; release.download = url;
                    const std::string digest = Lower(asset.get<std::string>("digest", ""));
                    if (digest.starts_with("sha256:") && Digest(digest.substr(7))) {
                        release.sha256 = digest.substr(7); code = Code::Available;
                    } else code = Code::NoDigest;
                    break;
                }
            }
            best = {code, release, code == Code::Available ? "A newer Windows package is available." :
                code == Code::NoDigest ? "The Windows asset has no valid GitHub SHA-256 digest." :
                                       "The newer release has no supported Windows x64 package."};
        }
        return best;
    } catch (const std::exception& error) { return {Code::InvalidResponse, {}, error.what()}; }
}

CheckResult Check(Version installed, bool allow_experimental, const std::atomic_bool& cancel) {
    try {
        std::string json;
        // Read multiple pages instead of assuming that release publication dates are ordered
        // by version. Cap the scan at 500 releases and each response at 8 MiB.
        CheckResult best{Code::NoPackage, {}, "No published Windows x64 package is available."};
        for (unsigned page = 1; page <= 5; ++page) {
            json.clear();
            HttpGet(Wide("https://api.github.com/repos/" + std::string(Repository) +
                        "/releases?per_page=100&page=" + std::to_string(page)), 8 * 1024 * 1024, cancel,
                    [&](const char* bytes, size_t count) { json.append(bytes, count); });
            auto next = ParseReleases(json, installed, allow_experimental);
            if (next.code == Code::InvalidResponse) return next;
            if (next.release && (!best.release || next.release->version > best.release->version)) best = std::move(next);
            else if (!best.release && next.code == Code::Current) best = std::move(next);
            if (ReadJson(json).size() < 100) break;
        }
        return best;
    } catch (const Error& error) { return {error.code, {}, error.what()}; }
      catch (const std::exception& error) { return {Code::InvalidResponse, {}, error.what()}; }
}

std::string Sha256(const fs::path& file, const std::atomic_bool& cancel) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    struct Cleanup {
        BCRYPT_ALG_HANDLE& algorithm; BCRYPT_HASH_HANDLE& hash;
        ~Cleanup() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } cleanup{algorithm, hash};
    DWORD object_size = 0, written = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &written, 0) < 0)
        throw Error(Code::Integrity, "SHA-256 provider is unavailable.");
    std::vector<unsigned char> object(object_size);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) < 0)
        throw Error(Code::Integrity, "Could not initialize SHA-256.");
    File input(file, L"rb"); std::array<unsigned char, 65536> block;
    for (;;) {
        Canceled(cancel);
        const size_t count = std::fread(block.data(), 1, block.size(), input.handle);
        if (count && BCryptHashData(hash, block.data(), ULONG(count), 0) < 0)
            throw Error(Code::Integrity, "SHA-256 calculation failed.");
        if (count < block.size()) { if (std::ferror(input.handle)) throw Error(Code::Disk, "Could not read downloaded archive."); break; }
    }
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0) < 0)
        throw Error(Code::Integrity, "Could not finish SHA-256.");
    std::ostringstream result; result << std::hex << std::setfill('0');
    for (unsigned char byte : digest) result << std::setw(2) << unsigned(byte);
    return result.str();
}

fs::path EntryPath(const std::string& name) {
    if (name.empty() || name.size() > 400 || name.front() == '/' || name.find('\\') != std::string::npos ||
        name.find('\0') != std::string::npos) throw Error(Code::InvalidPackage, "Unsafe archive path.");
    std::string path = name;
    if (path.back() == '/') path.pop_back();
    fs::path result; size_t at = 0;
    while (at < path.size()) {
        const size_t slash = path.find('/', at), end = slash == std::string::npos ? path.size() : slash;
        const std::string part = path.substr(at, end - at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' ||
            part.find_first_of(":<>\"|?*") != std::string::npos ||
            std::any_of(part.begin(), part.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw Error(Code::InvalidPackage, "Unsafe Windows archive name.");
        const std::string base = Lower(part.substr(0, part.find('.')));
        if (base == "con" || base == "prn" || base == "aux" || base == "nul" || base == "conin$" || base == "conout$" ||
            (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '0' && base[3] <= '9') ||
            part.find("\xc2\xb9") != std::string::npos || part.find("\xc2\xb2") != std::string::npos || part.find("\xc2\xb3") != std::string::npos)
            throw Error(Code::InvalidPackage, "Reserved Windows archive name.");
        result /= Wide(part);
        at = end + 1;
    }
    if (result.empty()) throw Error(Code::InvalidPackage, "Empty archive path.");
    return result;
}

bool ProgramFile(const fs::path& relative) {
    const std::string name = Lower(relative.generic_string());
    if (name.starts_with("docs/third-party/") && (name.ends_with(".txt") || name.ends_with(".md"))) return true;
    const std::string parent=Lower(relative.parent_path().generic_string());
    if ((parent == "docs/images/readme/launcher" || parent == "docs/images/readme/custom_menu") &&
        name.ends_with(".png")) return true;
    if (name == "docs/launcher_guide.md" || name == "docs/in_game_menu_guide.md") return true;
    if (relative.parent_path() == "bin") {
        return name == "bin/star ocean 5.exe" || name == "bin/star ocean 5 launcher.exe" ||
               name == "bin/pkgextractor.exe" || name == "bin/so5-updater.exe" || name.ends_with(".dll");
    }
    if (name.find('/') == std::string::npos) {
        return name == "ps4rt.exe" || name == "ps4rt-launcher.exe" || name == "pkgextractor.exe" ||
            name == "star ocean 5.exe" || name == "star ocean 5 launcher.exe" ||
            name == "license" || name == "changelog.md" || name == "readme.md" || name == "readme-windows.txt" || name == "version" ||
            name.ends_with(".dll");
    }
    return name.starts_with("gpu/assets/") || name.starts_with("launcher/assets/") ||
           name.starts_with("fsr4_shaders/") ||
           name == "tools/updater/so5-updater.exe" || name == "docs/upstream_credits.md" || name == "docs/windows_updates.md" ||
           name == "docs/native_pkg.md" || name == "docs/miniz_license.txt" || name == "docs/libcxx_license.txt" ||
           name == "docs/window_presentation.md";
}

bool PlayerInstall(const fs::path& root) {
    std::error_code error;
    if (fs::exists(root / ".git", error) || fs::exists(root / "build-windows.ps1", error) ||
        fs::exists(root / "build.sh", error)) return false;
    return fs::is_regular_file(root / "VERSION", error) &&
        (fs::is_regular_file(root / "PS4RT-Launcher.exe", error) || fs::is_regular_file(root / "Star Ocean 5 Launcher.exe", error));
}

Package ValidatePackage(const fs::path& root, Version expected) {
    Package package{root, {}, expected, {}};
    Plain(root);
    if (!fs::is_directory(root)) throw Error(Code::InvalidPackage, "Update root directory is missing.");
    if (!fs::is_regular_file(root / "VERSION")) throw Error(Code::InvalidPackage, "Package VERSION is missing.");
    const auto stored = ReadSmallFile(root / "VERSION", 32);
    std::string version = stored; while (!version.empty() && (version.back() == '\r' || version.back() == '\n')) version.pop_back();
    if (Version::Parse(version) != expected) throw Error(Code::InvalidPackage, "Incorrect package version.");
    const bool bin_layout = fs::is_regular_file(root / "bin/Star Ocean 5 Launcher.exe");
    const fs::path program_directory = bin_layout ? "bin" : "";
    const bool branded = bin_layout || fs::is_regular_file(root / "Star Ocean 5 Launcher.exe");
    const fs::path runtime = program_directory / (branded ? "Star Ocean 5.exe" : "ps4rt.exe");
    const fs::path launcher = branded ? "Star Ocean 5 Launcher.exe" : "PS4RT-Launcher.exe";
    const fs::path extractor = program_directory / "PkgExtractor.exe";
    const fs::path updater = bin_layout ? "bin/SO5-Updater.exe" : "tools/updater/SO5-Updater.exe";
    package.launcher = launcher;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const fs::path relative = entry.path().lexically_relative(root);
        EntryPath(relative.generic_string());
        PlainUnder(root, relative);
        if (entry.is_directory()) continue;
        if (!entry.is_regular_file() || !ProgramFile(relative)) throw Error(Code::InvalidPackage, "Unexpected file in update package.");
        package.files.push_back(relative);
        if (package.files.size() > 20000) throw Error(Code::InvalidPackage, "Too many update files.");
    }
    if (bin_layout) CheckVersion(root / "bin/Star Ocean 5 Launcher.exe",expected);
    for (const fs::path& name : {runtime, launcher, extractor, updater}) CheckVersion(root / name, expected);
    for (const char* name : {"launcher/assets/launcher-background.png", "launcher/assets/launcher-icon.png", "launcher/assets/star-ocean-logo.png",
         "gpu/assets/star_ocean_overlay.png", "LICENSE"})
        if (!fs::is_regular_file(root / name)) throw Error(Code::InvalidPackage, std::string("Missing update file: ") + name);
    for (const char* name : {"SDL3.dll", "libc++.dll"})
        if (!fs::is_regular_file(root / program_directory / name))
            throw Error(Code::InvalidPackage, std::string("Missing update dependency: ") + name);
    return package;
}

Package Extract(const fs::path& archive, const fs::path& destination, Version expected,
                const std::atomic_bool& cancel, const Progress& progress) {
    if (fs::exists(destination)) throw Error(Code::InvalidPackage, "Extraction needs a new staging directory.");
    fs::create_directories(destination); Plain(destination);
    File input(archive, L"rb"); mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, input.handle, 0, 0)) throw Error(Code::InvalidPackage, "Invalid ZIP archive.");
    struct EndZip { mz_zip_archive& zip; ~EndZip() { mz_zip_reader_end(&zip); } } end{zip};
    const auto count = mz_zip_reader_get_num_files(&zip);
    if (!count || count > 20000) throw Error(Code::InvalidPackage, "Invalid archive entry count.");
    struct Entry { mz_uint index; fs::path path; bool directory; std::uint64_t bytes; };
    std::vector<Entry> entries; std::set<std::wstring, CaseLess> seen;
    std::optional<fs::path> prefix;
    std::uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        Canceled(cancel); mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_get_filename(&zip, i, nullptr, 0) > 401 || !mz_zip_reader_file_stat(&zip, i, &stat))
            throw Error(Code::InvalidPackage, "Invalid archive file entry.");
        const fs::path path = EntryPath(stat.m_filename);
        // Player ZIPs have exactly one enclosing folder. Source ZIPs cannot pass this
        // layout, file allowlist, version-resource and staged executable validation.
        const auto root = *path.begin();
        if (!prefix) prefix = root;
        if (*prefix != root) throw Error(Code::InvalidPackage, "Expected one package root directory.");
        const fs::path relative = path.lexically_relative(*prefix);
        if (relative == ".") { if (!stat.m_is_directory) throw Error(Code::InvalidPackage, "Invalid package root entry."); continue; }
        const unsigned kind = (stat.m_external_attr >> 16) & 0170000;
        if (stat.m_is_encrypted || !stat.m_is_supported || (kind && kind != 0100000 && kind != 0040000) ||
            (stat.m_external_attr & FILE_ATTRIBUTE_REPARSE_POINT) || stat.m_uncomp_size > MaxDownload ||
            stat.m_uncomp_size > MaxExpanded - total || !seen.insert(relative.wstring()).second)
            throw Error(Code::InvalidPackage, "Unsupported, duplicate, linked or oversized ZIP entry.");
        if (!stat.m_is_directory && !ProgramFile(relative)) throw Error(Code::InvalidPackage, "Package contains a protected or unexpected path.");
        total += stat.m_uncomp_size;
        entries.push_back({i, relative, bool(stat.m_is_directory), stat.m_uncomp_size});
    }
    if (fs::space(destination).available < total + 32 * 1024 * 1024)
        throw Error(Code::Disk, "Insufficient disk space for extraction.");
    std::uint64_t done = 0;
    for (const auto& entry : entries) {
        Canceled(cancel); PlainUnder(destination, entry.path);
        const fs::path target = destination / entry.path;
        if (entry.directory) { fs::create_directories(target); continue; }
        fs::create_directories(target.parent_path());
        File output(target, L"wb");
        if (!mz_zip_reader_extract_to_cfile(&zip, entry.index, output.handle, 0))
            throw Error(Code::Integrity, "ZIP extraction or CRC validation failed.");
        output.Close(); done += entry.bytes;
        if (progress) progress(Phase::Extract, done, total);
    }
    Canceled(cancel);
    if (progress) progress(Phase::Validate, 0, 0);
    return ValidatePackage(destination, expected);
}

Prepared Prepare(const Release& release, const fs::path& work_parent,
                 const std::atomic_bool& cancel, const Progress& progress, const Transfer& transfer) {
    const std::string base=std::string(PackagePrefix)+release.version.String();
    const std::string url_prefix="https://github.com/"+std::string(Repository)+"/releases/download/"+release.tag+"/";
    const bool known_asset=release.download==url_prefix+base+".zip" || release.download==url_prefix+base+"-latest.zip";
    if (!known_asset || Version::Parse(release.tag) != release.version || !Digest(release.sha256) ||
        !release.size || release.size > MaxDownload) throw Error(Code::Integrity, "Invalid release asset metadata.");
    GUID id{}; if (FAILED(CoCreateGuid(&id))) throw Error(Code::Disk, "Could not create update workspace ID.");
    wchar_t token[40]{}; StringFromGUID2(id, token, 40);
    fs::create_directories(work_parent); Plain(work_parent);
    const fs::path workspace = work_parent / (std::wstring(L"so5-update-") + token);
    if (!fs::create_directory(workspace)) throw Error(Code::Disk, "Update workspace already exists.");
    {
        std::ofstream marker(workspace / "workspace.marker", std::ios::binary);
        marker << "SO5 update workspace v1\n"; marker.flush();
        if (!marker) throw Error(Code::Disk, "Could not mark the update workspace.");
    }
    try {
        const fs::path archive = workspace / "package.zip";
        File output(archive, L"wb"); std::uint64_t done = 0;
        if (progress) progress(Phase::Download, 0, release.size);
        const auto receive = [&](const char* bytes, size_t count) {
            Canceled(cancel);
            if (count > release.size - done) throw Error(Code::Integrity, "Package exceeds its declared download size.");
            if (std::fwrite(bytes, 1, count, output.handle) != count) throw Error(Code::Disk, "Could not write downloaded package.");
            done += count; if (progress) progress(Phase::Download, done, release.size);
        };
        if (transfer) transfer(release.download, release.size, cancel, receive);
        else HttpGet(Wide(release.download), release.size, cancel, receive);
        output.Close();
        if (done != release.size) throw Error(Code::Integrity, "Incomplete package download.");
        if (progress) progress(Phase::Verify, 0, 0);
        if (Sha256(archive, cancel) != release.sha256) throw Error(Code::Integrity, "Downloaded package SHA-256 does not match GitHub.");
        auto package = Extract(archive, workspace / "package", release.version, cancel, progress);
        Canceled(cancel); ProbePackage(package); Canceled(cancel);
        if (progress) progress(Phase::Ready, 1, 1);
        return {workspace, std::move(package)};
    } catch (...) { try { RemoveWorkspace(workspace); } catch (...) {} throw; }
}

void RemoveWorkspace(const fs::path& workspace) {
    if (!workspace.filename().wstring().starts_with(L"so5-update-{") ||
        ReadSmallFile(workspace / "workspace.marker", 64) != "SO5 update workspace v1\n")
        throw Error(Code::InvalidPackage, "Refusing to remove an unrecognized update workspace.");
    Plain(workspace);
    for (const auto& entry : fs::recursive_directory_iterator(workspace)) Plain(entry.path());
    fs::remove_all(workspace);
}
namespace {
std::wstring Quote(const fs::path& value) {
    std::wstring result = L"\""; unsigned slashes = 0;
    for (wchar_t c : value.wstring()) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); result += L'"'; return result;
}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
void AtomicCopy(const fs::path& source, const fs::path& destination, const wchar_t* suffix) {
    fs::path temporary = destination; temporary += suffix;
    if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE)) throw Error(Code::Disk, WinError("Could not stage installed file"));
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto detail = WinError("Could not replace installed file");
        DeleteFileW(temporary.c_str()); throw Error(Code::Install, detail);
    }
}
void WriteTree(const fs::path& path, const Tree& tree) {
    fs::path temporary = path;
    temporary += L".so5-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".new";
    if (fs::exists(temporary)) throw Error(Code::Disk, "Update journal temporary file already exists.");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        boost::property_tree::write_json(file, tree);
        file.flush(); if (!file) throw Error(Code::Disk, "Could not write update journal.");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error(Code::Disk, WinError("Could not commit update journal"));
}
struct UpdateLock {
    Handle mutex;
    bool acquired = false;
    explicit UpdateLock(const fs::path& target) {
        Handle directory(CreateFileW(target.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (directory.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(directory.value, &info))
            throw Error(Code::Install, WinError("Could not identify installation directory"));
        const std::wstring name = L"Local\\StarOcean5Update-" + std::to_wstring(info.dwVolumeSerialNumber) + L"-" +
            std::to_wstring(info.nFileIndexHigh) + L"-" + std::to_wstring(info.nFileIndexLow);
        mutex.value = CreateMutexW(nullptr, FALSE, name.c_str());
        const DWORD wait = mutex.value ? WaitForSingleObject(mutex.value, 0) : WAIT_FAILED;
        acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if (!acquired) throw Error(Code::InUse, "Another updater is already using this installation.");
    }
    ~UpdateLock() { if (acquired) ReleaseMutex(mutex.value); }
};
std::string Utf8(const fs::path& path) { const auto value = path.u8string(); return {value.begin(), value.end()}; }
void WriteResult(const fs::path& target, const InstallResult& result) {
    Tree tree; tree.put("success", result.success); tree.put("recovered", result.recovered);
    tree.put("detail", result.detail); tree.put("backup", Utf8(result.backup));
    tree.put("workspace", Utf8(result.workspace));
    PlainUnder(target, "out/update-result.json"); fs::create_directories(target / "out");
    WriteTree(target / "out/update-result.json", tree);
}
void StartProcess(const fs::path& executable, const std::wstring& arguments, const fs::path& cwd,
                  PROCESS_INFORMATION& process, bool clean_path) {
    std::wstring command = Quote(executable) + L" " + arguments;
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = clean_path ? SW_HIDE : SW_SHOWNORMAL;
    std::vector<wchar_t> environment;
    if (clean_path) {
        LPWCH current = GetEnvironmentStringsW();
        if (!current) throw Error(Code::Install, WinError("Could not read environment"));
        for (const wchar_t* entry = current; *entry; entry += std::wcslen(entry) + 1) {
            if (_wcsnicmp(entry, L"PATH=", 5) == 0) continue;
            environment.insert(environment.end(), entry, entry + std::wcslen(entry) + 1);
        }
        FreeEnvironmentStringsW(current);
        wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows, MAX_PATH);
        const std::wstring path = std::wstring(L"PATH=") + windows + L"\\System32;" + windows;
        environment.insert(environment.end(), path.begin(), path.end()); environment.push_back(0); environment.push_back(0);
    }
    const DWORD old_error_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, clean_path ? environment.data() : nullptr,
        cwd.c_str(), &startup, &process);
    const DWORD create_error = GetLastError(); SetErrorMode(old_error_mode);
    if (!created) { SetLastError(create_error); throw Error(Code::Install, WinError("Could not start staged application")); }
}
} // namespace

void ProbePackage(const Package& package) {
    const auto probe = [&](const fs::path& executable, const wchar_t* arguments) {
        PROCESS_INFORMATION process{};
        StartProcess(package.root / executable, arguments, package.root, process, true);
        Handle thread(process.hThread), child(process.hProcess);
        if (WaitForSingleObject(child.value, 10000) != WAIT_OBJECT_0) {
            TerminateProcess(child.value, 2); WaitForSingleObject(child.value, 3000);
            throw Error(Code::InvalidPackage, "Staged application self-test did not complete.");
        }
        DWORD code = 0;
        if (!GetExitCodeProcess(child.value, &code) || code != 0)
            throw Error(Code::InvalidPackage, "Staged " + executable.string() + " self-test failed (exit code " + std::to_string(code) + ").");
    };
    probe(package.launcher, L"--self-test");
    const bool bin_layout=fs::is_regular_file(package.root / "bin/Star Ocean 5.exe");
    if (bin_layout) probe("bin/Star Ocean 5 Launcher.exe",L"--self-test");
    probe(bin_layout ? "bin/Star Ocean 5.exe" :
        package.launcher == "Star Ocean 5 Launcher.exe" ? "Star Ocean 5.exe" : "ps4rt.exe", L"--cpu-features");
}

InstallResult Install(const Package& package, const fs::path& target, const fs::path& backup,
                      const std::function<void(size_t)>& after_file) {
    struct Record { fs::path relative; bool existed = false; bool retire = false; };
    std::vector<Record> records; size_t attempted = 0;
    InstallResult result{false, true, backup, {}};
    try {
        if (!PlayerInstall(target)) throw Error(Code::Development, "Automatic installation is limited to standalone player folders.");
        UpdateLock update_lock(target);
        std::string installed_text = ReadSmallFile(target / "VERSION", 32);
        while (!installed_text.empty() && (installed_text.back() == '\r' || installed_text.back() == '\n')) installed_text.pop_back();
        const auto installed = Version::Parse(installed_text);
        if (!installed || package.version <= *installed)
            throw Error(Code::Install, "Refusing to install an equal or older version.");
        if (fs::weakly_canonical(package.root) == fs::weakly_canonical(target) || fs::exists(backup))
            throw Error(Code::Install, "Invalid or already used installation workspace.");
        const fs::path backup_relative = backup.lexically_relative(target);
        if (backup_relative.empty() || *backup_relative.begin() != "out" ||
            backup_relative.generic_string().find("..") != std::string::npos)
            throw Error(Code::Install, "Invalid backup directory.");
        PlainUnder(target, backup_relative);
        std::set<std::wstring, CaseLess> seen;
        std::uint64_t needed = 32 * 1024 * 1024;
        for (const auto& relative : package.files) {
            if (EntryPath(relative.generic_string()) != relative || !ProgramFile(relative) || !seen.insert(relative.wstring()).second)
                throw Error(Code::InvalidPackage, "Invalid installation manifest.");
            PlainUnder(package.root, relative); PlainUnder(target, relative);
            if (!fs::is_regular_file(package.root / relative)) throw Error(Code::InvalidPackage, "Staged file disappeared.");
            needed += fs::file_size(package.root / relative);
            if (fs::exists(target / relative)) {
                if (!fs::is_regular_file(target / relative)) throw Error(Code::Install, "Installed file is a directory.");
                Handle probe(CreateFileW((target / relative).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
                if (probe.value == INVALID_HANDLE_VALUE) throw Error(Code::InUse, WinError("An installed file is in use or not writable"));
                needed += fs::file_size(target / relative);
            }
            records.push_back({relative, fs::exists(target / relative)});
        }
        // Retire only exact old program names whose replacement is in this manifest.
        // Include them in the same backup/journal/rollback transaction as replacements.
        for (const auto& names : {std::pair{"ps4rt.exe","Star Ocean 5.exe"},
                                 std::pair{"PS4RT-Launcher.exe","Star Ocean 5 Launcher.exe"},
                                 std::pair{"PS4RT-PkgExtractor.exe","PkgExtractor.exe"},
                                 std::pair{"SO5-PkgExtractor.exe","PkgExtractor.exe"}}) {
            const fs::path old=names.first;
            const fs::path flat_replacement=names.second;
            const fs::path replacement=seen.contains(flat_replacement.wstring()) ? flat_replacement : fs::path("bin")/flat_replacement;
            if (!seen.contains(replacement.wstring()) || seen.contains(old.wstring()) || !fs::exists(target/old)) continue;
            PlainUnder(target,old);
            if (!fs::is_regular_file(target/old)) throw Error(Code::Install,"An obsolete executable is not a regular file.");
            Handle probe(CreateFileW((target/old).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr));
            if (probe.value==INVALID_HANDLE_VALUE) throw Error(Code::InUse,WinError("An obsolete executable is in use"));
            needed+=fs::file_size(target/old); records.push_back({old,true,true});
        }
        for (const auto& relative : package.files) {
            if (relative.parent_path() != "bin") continue;
            const fs::path old = relative.filename() == "SO5-Updater.exe" ?
                fs::path("tools/updater/SO5-Updater.exe") : relative.filename();
            if (seen.contains(old.wstring()) || !fs::exists(target / old)) continue;
            PlainUnder(target,old);
            if (!fs::is_regular_file(target / old)) throw Error(Code::Install,"An old flat program file is not a regular file.");
            Handle probe(CreateFileW((target / old).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr));
            if (probe.value==INVALID_HANDLE_VALUE) throw Error(Code::InUse,WinError("An old flat program file is in use"));
            needed+=fs::file_size(target / old); records.push_back({old,true,true});
        }
        if (fs::space(target).available < needed) throw Error(Code::Disk, "Insufficient disk space for update and backup.");
        fs::create_directories(backup);
        // Complete every backup before replacing the first installed file.
        for (const auto& record : records) if (record.existed) {
            fs::create_directories((backup / record.relative).parent_path());
            fs::copy_file(target / record.relative, backup / record.relative);
        }
        Tree journal; journal.put("target", Utf8(target)); journal.put("version", package.version.String());
        Tree entries;
        for (const auto& record : records) {
            Tree entry; entry.put("path", record.relative.generic_string()); entry.put("existed", record.existed);
            entry.put("retired",record.retire);
            entries.push_back({"", entry});
        }
        journal.add_child("files", entries); journal.put("attempted", 0);
        WriteTree(backup / "transaction.json", journal);
        for (const auto& record : records) {
            PlainUnder(target, record.relative);
            fs::create_directories((target / record.relative).parent_path());
            journal.put("attempted", ++attempted); WriteTree(backup / "transaction.json", journal);
            if (record.retire) {
                if (!DeleteFileW((target/record.relative).c_str())) throw Error(Code::Install,WinError("Could not retire an obsolete executable"));
            } else AtomicCopy(package.root / record.relative, target / record.relative, L".so5-update-new");
            if (after_file) after_file(attempted);
        }
        journal.put("complete", true); WriteTree(backup / "transaction.json", journal);
        result.success = true; result.detail = "Update installed successfully. Previous program files were retained.";
    } catch (const std::exception& error) {
        result.detail = error.what();
        // Restore only program files touched by this transaction. Saves, settings, caches,
        // game data and unknown mod files never enter either manifest or rollback.
        while (attempted) {
            const auto& record = records[--attempted];
            try {
                PlainUnder(target, record.relative);
                if (record.existed) AtomicCopy(backup / record.relative, target / record.relative, L".so5-restore-new");
                else if (fs::exists(target / record.relative) && !DeleteFileW((target / record.relative).c_str()))
                    throw Error(Code::Recovery, WinError("Could not remove a new program file during rollback"));
            } catch (const std::exception& recovery) {
                result.recovered = false; result.detail += std::string(" | Rollback: ") + recovery.what();
            }
        }
    }
    return result;
}

void StartInstaller(const Prepared& prepared, const fs::path& target, const fs::path& current_launcher,
                    unsigned long parent_pid) {
    (void)parent_pid; // A real inherited process handle avoids PID reuse races.
    if (!PlayerInstall(target)) throw Error(Code::Development, "Development checkouts cannot be replaced by the player updater.");
    if (current_launcher != target / "Star Ocean 5 Launcher.exe" &&
        current_launcher != target / "PS4RT-Launcher.exe" &&
        current_launcher != target / "bin/Star Ocean 5 Launcher.exe")
        throw Error(Code::Development, "Launcher must be in the player folder or its bin directory.");
    const auto helper = fs::is_regular_file(target / "bin/SO5-Updater.exe") ?
        target / "bin/SO5-Updater.exe" : target / "tools/updater/SO5-Updater.exe";
    CheckVersion(helper, *Version::Parse(PS4RT_VERSION_STRING));
    const auto copy = prepared.workspace / "SO5-Updater.exe";
    Plain(copy);
    if (!CopyFileW(helper.c_str(), copy.c_str(), FALSE)) throw Error(Code::Disk, WinError("Could not prepare native installer"));
    HANDLE inherited_parent{};
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &inherited_parent,
                         SYNCHRONIZE, TRUE, 0)) throw Error(Code::Install, WinError("Could not pass launcher process handle"));
    Handle parent(inherited_parent);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; Handle ready(CreateEventW(&security, TRUE, FALSE, nullptr));
    if (!ready.value) throw Error(Code::Install, WinError("Could not create installer handshake"));
    SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> memory(bytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) throw Error(Code::Install, WinError("Could not initialize installer handles"));
    struct Attributes { PPROC_THREAD_ATTRIBUTE_LIST value; ~Attributes() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
    HANDLE handles[] = {parent.value, ready.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr))
        throw Error(Code::Install, WinError("Could not restrict installer handle inheritance"));
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    std::wstring command = Quote(copy) + L" --apply " + Quote(prepared.workspace) + L" " + Quote(target) + L" " +
        Quote(current_launcher.filename()) + L" " + Wide(prepared.package.version.String()) + L" " +
        std::to_wstring(reinterpret_cast<UINT_PTR>(parent.value)) + L" " + std::to_wstring(reinterpret_cast<UINT_PTR>(ready.value));
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(copy.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, prepared.workspace.c_str(), &startup.StartupInfo, &process))
        throw Error(Code::Install, WinError("Could not start native installer"));
    Handle thread(process.hThread), child(process.hProcess);
    HANDLE wait[] = {ready.value, child.value};
    if (WaitForMultipleObjects(2, wait, FALSE, 8000) != WAIT_OBJECT_0)
        throw Error(Code::Install, "Native installer did not accept the staged update. The launcher remains open.");
}

std::optional<InstallResult> ReadInstallResult(const fs::path& target) {
    const auto path = target / "out/update-result.json";
    if (!fs::is_regular_file(path)) return {};
    PlainUnder(target, "out/update-result.json");
    const Tree tree = ReadJson(ReadSmallFile(path, 16384));
    InstallResult result{tree.get<bool>("success"), tree.get<bool>("recovered"),
                         fs::path(Wide(tree.get<std::string>("backup"))), tree.get<std::string>("detail")};
    result.workspace = fs::path(Wide(tree.get<std::string>("workspace", "")));
    const auto seen = target / "out/update-result-last.json";
    PlainUnder(target, "out/update-result-last.json");
    if (!MoveFileExW(path.c_str(), seen.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error(Code::Disk, WinError("Could not acknowledge update result"));
    return result;
}

std::optional<fs::path> PendingCleanup(const fs::path& target) {
    const auto path = target / "out/update-result-last.json";
    if (!fs::is_regular_file(path)) return {};
    PlainUnder(target, "out/update-result-last.json");
    const auto tree = ReadJson(ReadSmallFile(path, 16384));
    const auto value = tree.get<std::string>("workspace", "");
    if (!tree.get<bool>("recovered", true)) return {};
    if (value.empty()) return {};
    const fs::path workspace = Wide(value);
    if (fs::exists(workspace)) return workspace;
    return {};
}

int InstallerMain(int argc, wchar_t** argv) {
    if (argc != 8 || std::wstring(argv[1]) != L"--apply") return 2;
    const fs::path workspace = fs::absolute(argv[2]).lexically_normal();
    const fs::path target = fs::absolute(argv[3]).lexically_normal();
    const fs::path old_launcher = argv[4];
    const auto version = Version::Parse(Utf8(fs::path(argv[5])));
    wchar_t* end = nullptr;
    const auto parent_number = std::wcstoull(argv[6], &end, 10);
    if (!parent_number || !end || *end) return 2;
    const auto ready_number = std::wcstoull(argv[7], &end, 10);
    if (!ready_number || !end || *end) return 2;
    Handle parent(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(parent_number)));
    Handle ready(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(ready_number)));
    InstallResult result; Package package;
    try {
        if (!version || !PlayerInstall(target) || old_launcher != old_launcher.filename() ||
            (old_launcher != "PS4RT-Launcher.exe" && old_launcher != "Star Ocean 5 Launcher.exe") ||
            !workspace.filename().wstring().starts_with(L"so5-update-{") ||
            ReadSmallFile(workspace / "workspace.marker", 64) != "SO5 update workspace v1\n")
            throw Error(Code::InvalidPackage, "Invalid native installer arguments.");
        UpdateLock update_lock(target);
        Plain(workspace); package = ValidatePackage(workspace / "package", *version);
        if (!SetEvent(ready.value)) throw Error(Code::Install, "Installer handshake failed.");
        if (WaitForSingleObject(parent.value, 30000) != WAIT_OBJECT_0) return 3;
        const fs::path backup = target / "out" / "updates" / workspace.filename() / "previous";
        result = Install(package, target, backup);
        result.workspace = workspace;
        WriteResult(target, result);
        // Remove large staging payloads before relaunch. The tiny running helper copy is
        // removed by the launcher after this process exits. Previous program files stay.
        try {
            if (result.recovered && package.root.parent_path() == workspace && package.root.filename() == "package") {
                Plain(package.root);
                for (const auto& entry : fs::recursive_directory_iterator(package.root)) Plain(entry.path());
                fs::remove_all(package.root);
                DeleteFileW((workspace / "package.zip").c_str());
            }
        } catch (...) {}
        const fs::path launcher = target / (result.success ? package.launcher : old_launcher);
        PROCESS_INFORMATION process{};
        StartProcess(launcher, L"--update-result", target, process, false);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        return result.success ? 0 : result.recovered ? 4 : 5;
    } catch (const std::exception& error) {
        // No legacy system popup. A diagnosable result is retained for the next normal
        // launcher start, including a backup location if file recovery was incomplete.
        result.detail = std::string("Native installer: ") + error.what();
        try { if (PlayerInstall(target)) WriteResult(target, result); } catch (...) {}
        return 6;
    }
}
} // namespace So5Update
