// SPDX-License-Identifier: GPL-2.0-or-later
// Built-in cheat table compiler. External profile support has been removed.
#include "cheat_profiles.h"
#include "cheat_catalog.h"
#include "../src/sha256_win.h"
#include <cstring>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <charconv>
#include <cwctype>

namespace So5Cheats {
namespace {
using Bytes=std::vector<std::uint8_t>;
struct Line { std::uint64_t offset; Bytes on,off; bool hook; std::int64_t target; };
struct Cheat { std::string name,description,source; std::vector<Line> lines; std::uint64_t slot=0; std::vector<float> choices; int id=0; bool builtin=false; };
std::string Name(std::string s,size_t limit=63) {
    for(auto& c:s) if(static_cast<unsigned char>(c)<32) c=' ';
    if(s.size()>limit) {size_t cut=limit;while(cut&&(static_cast<unsigned char>(s[cut])&0xc0)==0x80)--cut;s.resize(cut);}
    return s.empty()?"Cheat":s;
}
std::uint64_t Number(const std::string& s,int base=16) {
    if(s.empty()||s.size()>16) throw std::runtime_error("invalid integer");
    std::uint64_t value{};auto result=std::from_chars(s.data(),s.data()+s.size(),value,base);
    if(result.ec!=std::errc{}||result.ptr!=s.data()+s.size())throw std::runtime_error("invalid integer");
    return value;
}
Bytes Hex(const std::string& s) {
    std::string v;for(unsigned char c:s)if(c!='-'&&c!=' '&&c!='\t'&&c!='\r'&&c!='\n')v+=char(c);
    if(v.empty()||v.size()%2||v.size()>8192)throw std::runtime_error("invalid or oversized patch bytes");
    Bytes b;for(size_t i=0;i<v.size();i+=2)b.push_back(static_cast<std::uint8_t>(Number(v.substr(i,2))));
    return b;
}
std::string Hex(const Bytes& b) {
    std::ostringstream s;s<<std::hex<<std::uppercase<<std::setfill('0');for(auto v:b)s<<std::setw(2)<<unsigned(v);return s.str();
}
bool Inside(std::span<const Range> ranges,std::uint64_t at,size_t size) {
    return std::ranges::any_of(ranges,[&](auto r){return r.executable&&at>=r.start&&at<=r.end&&size<=r.end-at;});
}
bool Overlap(const Line&a,const Line&b){return a.offset<b.offset+b.on.size()&&b.offset<a.offset+a.on.size();}
}

namespace {
#include "cheat_builtins.inc"
void Validate(Cheat& c,std::span<const std::uint8_t> image,std::span<const Range> ranges) {
    if(c.lines.empty()||c.lines.size()>1024)throw std::runtime_error("no supported patch lines or too many lines");
    for(size_t i=0;i<c.lines.size();++i) {
        auto& l=c.lines[i];
        if(l.on.empty()||l.on.size()>4096||l.on.size()!=l.off.size()||!Inside(ranges,l.offset,l.on.size())||
           l.offset>image.size()||l.off.size()>image.size()-l.offset||(l.offset&4095u)+l.on.size()>4096)
            throw std::runtime_error("invalid executable patch range");
        if(!std::equal(l.off.begin(),l.off.end(),image.begin()+l.offset))throw std::runtime_error("original instructions or code cave do not match this executable");
        l.hook=l.on.size()>=5&&l.on.size()<=16&&l.on[0]==0xe9&&std::all_of(l.on.begin()+5,l.on.end(),[](auto b){return b==0x90;});
        if(l.hook){std::uint32_t raw{};std::memcpy(&raw,l.on.data()+1,4);l.target=static_cast<std::int64_t>(l.offset)+5+std::bit_cast<std::int32_t>(raw);if(l.off[0]==0xcc)throw std::runtime_error("breakpoint hook refused");}
        for(size_t j=0;j<i;++j)if(Overlap(l,c.lines[j]))throw std::runtime_error("overlapping lines");
    }
    for(const auto& l:c.lines) {
        const bool connected=std::ranges::any_of(c.lines,[&](const auto& p){return l.hook?!p.hook&&l.target>=static_cast<std::int64_t>(p.offset)&&l.target<static_cast<std::int64_t>(p.offset+p.on.size()):p.hook&&p.target>=static_cast<std::int64_t>(l.offset)&&p.target<static_cast<std::int64_t>(l.offset+l.on.size());});
        if(!connected)throw std::runtime_error("only hooks into their own code caves are supported");
    }
    if(!c.choices.empty()) {
        if(c.choices.size()>8||c.slot%4||!std::ranges::any_of(c.lines,[&](auto& l){return !l.hook&&c.slot>=l.offset&&c.slot<=l.offset+l.on.size()&&l.offset+l.on.size()-c.slot>=4;}))throw std::runtime_error("value slot is not an aligned float inside a cave");
        for(float v:c.choices)if(!std::isfinite(v)||v<=0||v>1e6)throw std::runtime_error("invalid value choice");
    }
    std::ranges::sort(c.lines,{},&Line::offset);
}
bool SameCode(const Cheat& a,const Cheat& b) {
    if(a.slot!=b.slot||a.choices!=b.choices||a.lines.size()!=b.lines.size())return false;
    for(size_t i=0;i<a.lines.size();++i) {
        const auto& x=a.lines[i];const auto& y=b.lines[i];
        if(x.offset!=y.offset||x.on!=y.on||x.off!=y.off)return false;
    }
    return true;
}
void Metadata(std::ostream& out,int id,bool builtin,const std::string& description) {
    out<<"meta "<<std::dec<<id<<' '<<int(builtin)<<'\n';
    if(!description.empty())out<<"detail "<<Hex(Bytes(description.begin(),description.end()))<<'\n';
}
}
Result Compile(const std::filesystem::path& table,
               const std::string& title,const std::string& version,
               std::span<const std::uint8_t> image,std::span<const Range> ranges,
               bool publish) {
    Result result;std::vector<Cheat> accepted;size_t total=0;
    auto accept=[&](Cheat c) {
        Entry entry;entry.id=c.id;entry.builtin=c.builtin;entry.name=c.name;entry.description=c.description;entry.source=c.source;entry.choices=c.choices;
        try {
            Validate(c,image,ranges);
            for(const auto& prev:accepted) {
                if(SameCode(c,prev)){result.problems.push_back(c.name+": duplicate code ignored (already provided by "+prev.name+")");return;}
                for(const auto& a:prev.lines)for(const auto& b:c.lines)if(Overlap(a,b))throw std::runtime_error("code conflicts with "+prev.name);
            }
            if(accepted.size()>=64||total+c.lines.size()>1024)throw std::runtime_error("too many cheats or patch lines");
            total+=c.lines.size();entry.available=true;result.names.push_back(c.name);accepted.push_back(std::move(c));
        }catch(const std::exception& e){entry.problem=e.what();result.problems.push_back(entry.name+": "+entry.problem);}
        result.entries.push_back(std::move(entry));
    };
    // Only compiled native definitions can reach the generated table.
    if(title=="CUSA03219"&&version=="01.00")for(auto c:Builtins())accept(std::move(c));
    if(!publish)return result;
    std::filesystem::create_directories(table.parent_path());auto staging=table;staging+=L".part";
    std::ofstream out(staging,std::ios::binary|std::ios::trunc);out<<"PS4RTCH1\n";
    for(const auto& p:result.problems)out<<"problem "<<Name(p,383)<<'\n';
    for(const auto& c:accepted){out<<"cheat "<<std::dec<<c.lines.size()<<' '<<c.name<<'\n';for(const auto& l:c.lines)out<<"line "<<std::hex<<l.offset<<' '<<Hex(l.on)<<' '<<Hex(l.off)<<'\n';if(!c.choices.empty()){out<<"value "<<std::hex<<c.slot<<" f32 ";for(size_t i=0;i<c.choices.size();++i)out<<(i?",":"")<<std::dec<<c.choices[i];out<<'\n';}Metadata(out,c.id,c.builtin,c.description);}
    out.close();if(!out)throw std::runtime_error("cannot write native cheat table");
    if(!MoveFileExW(staging.c_str(),table.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("cannot publish native cheat table: "+std::to_string(GetLastError()));
    return result;
}
}
