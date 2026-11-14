// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace So5Update {
namespace fs = std::filesystem;
inline constexpr char Repository[] = "Hoverbike51/Star-Ocean-Integrity-and-Faithlessness-PS4RT";
inline constexpr char PackagePrefix[] = "ps4rt-star-ocean-5-windows-x64-v";
inline constexpr std::uint64_t MaxDownload = 1024ull * 1024 * 1024;
inline constexpr std::uint64_t MaxExpanded = 4ull * 1024 * 1024 * 1024;

struct Version {
    std::array<unsigned, 3> parts{};
    std::string String() const;
    auto operator<=>(const Version&) const = default;
    static std::optional<Version> Parse(const std::string& text);
};
enum class Code {
    Current, Available, NoPackage, NoDigest, Network, RateLimit, Http, InvalidResponse,
    Canceled, Integrity, InvalidPackage, Disk, InUse, Install, Recovery, Development
};
struct Error : std::runtime_error {
    Code code;
    Error(Code value, const std::string& detail) : std::runtime_error(detail), code(value) {}
};
struct Release {
    Version version;
    std::string tag, page, asset_name, download, sha256;
    std::uint64_t size = 0;
    bool experimental = false;
};
struct CheckResult {
    Code code = Code::Current;
    std::optional<Release> release;
    std::string detail;
};
enum class Phase { Download, Verify, Extract, Validate, Ready, Install };
using Progress = std::function<void(Phase, std::uint64_t, std::uint64_t)>;
// An injectable transfer is used only by developer tests. The shipped client uses HTTPS.
using Transfer = std::function<void(const std::string&, std::uint64_t, const std::atomic_bool&,
                                   const std::function<void(const char*, size_t)>&)>;
struct Package {
    fs::path root;
    fs::path launcher;
    Version version;
    std::vector<fs::path> files;
};
struct Prepared { fs::path workspace; Package package; };
struct InstallResult {
    bool success = false, recovered = true;
    fs::path backup;
    std::string detail;
    fs::path workspace;
    bool retryable = false; // Sharing violation in preflight; no files touched.
};

// Windows experimental builds accept prereleases with numeric vMAJOR.MINOR.PATCH tags.
// Stable clients may set allow_experimental=false. Drafts/source archives are never updates.
CheckResult ParseReleases(const std::string& json, Version installed, bool allow_experimental);
CheckResult Check(Version installed, bool allow_experimental, const std::atomic_bool& cancel);
std::string Sha256(const fs::path& file, const std::atomic_bool& cancel);
// Reject unsafe Windows paths, including alternate streams, aliases, device names and links.
fs::path EntryPath(const std::string& name);
bool ProgramFile(const fs::path& relative);
bool PlayerInstall(const fs::path& root);
Package ValidatePackage(const fs::path& root, Version expected);
// Release gate: old installed clients must accept all payload paths before handoff.
void RequireLegacy101Compatibility(const Package& package);
Package Extract(const fs::path& archive, const fs::path& destination, Version expected,
                const std::atomic_bool& cancel, const Progress& progress);
Prepared Prepare(const Release& release, const fs::path& work_parent,
                 const std::atomic_bool& cancel, const Progress& progress, const Transfer& transfer = {});
void RemoveWorkspace(const fs::path& workspace);
// Staged validation executes only the verified launcher's --self-test in the staged folder.
void ProbePackage(const Package& package);
// Installer uses a static native helper copied outside the running application directory.
void StartInstaller(const Prepared& prepared, const fs::path& target, const fs::path& current_launcher,
                    unsigned long parent_pid);
InstallResult Install(const Package& package, const fs::path& target, const fs::path& backup,
                      const std::function<void(size_t)>& after_file = {});
std::optional<InstallResult> ReadInstallResult(const fs::path& target);
std::optional<fs::path> PendingCleanup(const fs::path& target);
int InstallerMain(int argc, wchar_t** argv);
} // namespace So5Update
