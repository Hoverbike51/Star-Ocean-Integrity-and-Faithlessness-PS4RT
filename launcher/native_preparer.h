#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace ps4rt {

using PrepareLog = std::function<void(const std::wstring&)>;

// Converts the title's already decrypted SELF files into PS4RTB05 and creates
// the offline AppContent profile.  This is the native equivalent of the old
// Python prepare/link scripts; it does not decrypt PKG/SELF content.
void prepare_title(const std::filesystem::path& game,
                   const std::filesystem::path& output,
                   const PrepareLog& log);

std::string title_id_from_sfo(const std::filesystem::path& param_sfo);

} // namespace ps4rt
