// SPDX-License-Identifier: GPL-2.0-or-later
#include "../launcher/native_tweaks.h"
#include <cassert>
#include <iostream>
int main() {
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/(L"so5-native-tweaks-"+std::to_wstring(GetCurrentProcessId()));
    assert(fs::create_directory(dir));const auto path=dir/L"ps4rt.ini";
    {std::ofstream file(path);file<<"# Runtime settings\nupscaler=fsr4\nexploration_leader=3\ncamera_follow=1\ncamera_follow=2\ncustom_key=preserve me\n";}
    So5Tweaks::Write(path,"camera_follow",0);
    auto read=So5Tweaks::Read(path);assert(read.camera==0&&read.leader);
    So5Tweaks::Write(path,"exploration_leader_enabled",0);
    read=So5Tweaks::Read(path);assert(read.camera==0&&!read.leader);
    const auto contents=[&]{std::ifstream file(path);return std::string(std::istreambuf_iterator<char>(file),{});};
    const auto before=contents();assert(before.find("upscaler=fsr4\n")!=std::string::npos);
    assert(before.find("exploration_leader=3\n")!=std::string::npos&&before.find("custom_key=preserve me\n")!=std::string::npos);
    assert(before.find("camera_follow=",before.find("camera_follow=")+1)==std::string::npos);
    // A Windows sharing violation must preserve the previous settings entirely.
    HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(lock!=INVALID_HANDLE_VALUE);bool refused=false;
    try{So5Tweaks::Write(path,"camera_follow",2);}catch(const std::exception&){refused=true;}
    CloseHandle(lock);assert(refused&&contents()==before);
    try{So5Tweaks::Write(path,"exploration_leader",5);assert(false);}catch(const std::exception&){}
    assert(contents()==before);
    So5Tweaks::Write(path,"camera_follow",2);assert(So5Tweaks::Read(path).camera==2);
    assert(fs::remove(path));assert(fs::is_empty(dir));assert(fs::remove(dir));
    std::cout<<"Native tweaks: unrelated runtime preferences preserved, duplicate keys normalized, sharing violations atomic.\n";
}
