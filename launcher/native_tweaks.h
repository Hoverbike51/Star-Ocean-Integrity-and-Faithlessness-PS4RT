// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace So5Tweaks {
struct Settings {bool leader=true;int camera=0;};
inline Settings Read(const std::filesystem::path& path) {
    Settings result;std::ifstream file(path);std::string line;
    while(std::getline(file,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto at=line.find('=');if(at==std::string::npos)continue;
        int value{};const auto parsed=std::from_chars(line.data()+at+1,line.data()+line.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=line.data()+line.size())continue;
        const auto key=line.substr(0,at);
        if(key=="camera_follow"&&value>=0&&value<=2)result.camera=value;
        if(key=="exploration_leader_enabled"&&(value==0||value==1))result.leader=value!=0;
    }
    return result;
}
// Merge only the edited key. Never rewrite unrelated runtime settings or the
// leader preference selected in game. Publish atomically, preserving the old
// file on failure; the caller never uses this while a game is running.
inline void Write(const std::filesystem::path& path,const char* key,int value) {
    if((std::string(key)!="camera_follow"||value<0||value>2)&&
       (std::string(key)!="exploration_leader_enabled"||value<0||value>1))
        throw std::runtime_error("invalid native tweak");
    std::vector<std::string> lines;std::string line;bool found=false;
    if(std::filesystem::exists(path)) {
        std::ifstream input(path);if(!input)throw std::runtime_error("cannot read runtime settings");
        while(std::getline(input,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.starts_with(std::string(key)+"=")) {
                if(found)continue;line=std::string(key)+"="+std::to_string(value);found=true;
            }
            lines.push_back(line);
        }
        if(input.bad())throw std::runtime_error("cannot read runtime settings");
    }
    if(!found)lines.push_back(std::string(key)+"="+std::to_string(value));
    std::filesystem::create_directories(path.parent_path());
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("cannot create settings transaction");
    wchar_t guid[40]{};StringFromGUID2(id,guid,40);
    auto temp=path;temp+=std::wstring(L".")+guid+L".tmp";
    std::string data;for(const auto& item:lines)data+=item+'\n';
    const auto file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot stage runtime settings");
    DWORD written{};const bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());throw std::runtime_error("cannot save runtime settings");
    }
}
}
