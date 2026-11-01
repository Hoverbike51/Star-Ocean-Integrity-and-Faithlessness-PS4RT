// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>

namespace HmvPkg {
struct Progress {
    std::uint64_t done=0,total=0;
    std::string staging,title;
    bool Parse(std::string_view line) {
        if(line.starts_with("PKG staging: ")) {
            auto name=line.substr(13);
            if(name.size()!=44 || !name.starts_with(".so5-import-") ||
               !std::all_of(name.begin()+12,name.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})) return false;
            if(!staging.empty() && staging!=name) return false;
            staging=name; return true;
        }
        if(line.starts_with("PKG title: ")) {
            auto id=line.substr(11);
            if(id.size()!=9 || !id.starts_with("CUSA") ||
               !std::all_of(id.begin()+4,id.end(),[](char c){return c>='0'&&c<='9';})) return false;
            if(!title.empty() && title!=id) return false;
            title=id; return true;
        }
        constexpr std::string_view prefix="PKG progress: ";
        if(!line.starts_with(prefix)) return false;
        line.remove_prefix(prefix.size());
        std::uint64_t next_done=0,next_total=0;
        auto a=std::from_chars(line.data(),line.data()+line.size(),next_done);
        if(a.ec!=std::errc{} || std::string_view(a.ptr,line.data()+line.size()-a.ptr).substr(0,3)!=" / ") return false;
        auto b=std::from_chars(a.ptr+3,line.data()+line.size(),next_total);
        if(b.ec!=std::errc{} || std::string_view(b.ptr,line.data()+line.size()-b.ptr).substr(0,8)!=" bytes (" ||
           !next_total || next_total>(1ull<<40) || next_done>next_total || next_done<done || (total && total!=next_total)) return false;
        done=next_done; total=next_total; return true;
    }
};
// Decode complete UTF-8 lines, never arbitrary pipe chunks. Bound malformed output.
struct Lines {
    std::string pending;
    template<class F> void Feed(std::string_view bytes,F line) {
        for(char c:bytes) {
            if(c=='\n') { if(!pending.empty() && pending.back()=='\r') pending.pop_back(); line(pending); pending.clear(); }
            else if(pending.size()<32768) pending+=c;
        }
    }
};
}
