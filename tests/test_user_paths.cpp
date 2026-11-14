// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <cstdio>
namespace fs=std::filesystem;
static const fs::path test_root=fs::temp_directory_path()/
    ("so5-user-paths-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
static const bool prepared=[] {
    assert(!fs::exists(test_root));
    assert(_putenv_s("PS4RT_GPU_USER_DIR",test_root.string().c_str())==0);
    return true;
}();
// Include the production implementation after preparing its environment, so an
// eager global initializer would create test_root and make the first check fail.
#include "../gpu/shadps4/common/path_util.cpp"
int main() {
    assert(prepared && !fs::exists(test_root));
    const auto& user=Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    assert(user==test_root && fs::is_directory(user));
    assert(fs::is_regular_file(test_root/"custom_trophy/Notice.txt"));
    assert(fs::is_directory(Common::FS::GetUserPath(Common::FS::PathType::CacheDir)));
    assert(_putenv_s("PS4RT_GPU_USER_DIR",(test_root/"other").string().c_str())==0);
    assert(Common::FS::GetUserPath(Common::FS::PathType::UserDir)==test_root);
    fs::create_directory(test_root/"override");
    Common::FS::SetUserPath(Common::FS::PathType::CacheDir,test_root/"override");
    assert(Common::FS::GetUserPath(Common::FS::PathType::CacheDir)==test_root/"override");
    assert(test_root.parent_path()==fs::temp_directory_path() && test_root.filename().string().starts_with("so5-user-paths-"));
    fs::remove_all(test_root);
    std::puts("PASS: no user-data writes before main; gameplay initialization and path overrides preserved.");
}
