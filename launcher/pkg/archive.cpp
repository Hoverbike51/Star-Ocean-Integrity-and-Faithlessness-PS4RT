// Copyright (c) 2026 HoverMods Vault
// SPDX-License-Identifier: GPL-2.0-or-later
// Original implementation of documented PS4 PKG/PFS/PFSC formats; see docs/NATIVE_PKG.md.
#include "archive.h"
#include <windows.h>
#include <bcrypt.h>
#include <miniz.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>

namespace HmvPkg {
namespace fs = std::filesystem;
namespace {
using Bytes = std::vector<unsigned char>;
using View = std::span<const unsigned char>;
using Mut = std::span<unsigned char>;
constexpr std::uint64_t MaxFile = 1ull << 40;
void Require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void Range(std::uint64_t at, std::uint64_t count, std::uint64_t size) {
    Require(at <= size && count <= size - at, "Truncated or out-of-range package data.");
}
std::uint64_t Number(View b, size_t at, size_t bytes, bool big = false) {
    Range(at, bytes, b.size()); std::uint64_t result = 0;
    for (size_t i = 0; i < bytes; ++i) result |= std::uint64_t(b[at+i]) << (8*(big ? bytes-1-i : i));
    return result;
}
void Crypto(NTSTATUS status) { Require(status >= 0, "Windows cryptographic operation failed."); }
fs::path NativePath(const fs::path& path) {
    auto existing=path.wstring();
    if (existing.starts_with(L"\\\\?\\")) {
        std::replace(existing.begin(),existing.end(),L'/',L'\\'); return fs::path(existing);
    }
    auto full=fs::absolute(path).lexically_normal(); full.make_preferred(); auto wide=full.wstring();
    if (wide.starts_with(L"\\\\?\\")) return fs::path(wide);
    if (wide.starts_with(L"\\\\")) return fs::path(L"\\\\?\\UNC\\"+wide.substr(2));
    return fs::path(L"\\\\?\\"+wide);
}
struct Algorithm {
    BCRYPT_ALG_HANDLE handle{};
    Algorithm(LPCWSTR name, ULONG flags = 0) { Crypto(BCryptOpenAlgorithmProvider(&handle,name,nullptr,flags)); }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle,0); }
    Algorithm(const Algorithm&) = delete;
};
std::array<unsigned char,32> Hash(View data, View secret = {}) {
    Algorithm provider(BCRYPT_SHA256_ALGORITHM, secret.empty() ? 0 : BCRYPT_ALG_HANDLE_HMAC_FLAG);
    std::array<unsigned char,32> result{};
    Crypto(BCryptHash(provider.handle, const_cast<PUCHAR>(secret.data()), ULONG(secret.size()),
                      const_cast<PUCHAR>(data.data()), ULONG(data.size()), result.data(), ULONG(result.size())));
    return result;
}
struct Aes {
    Algorithm provider{BCRYPT_AES_ALGORITHM}; BCRYPT_KEY_HANDLE handle{};
    Aes(View secret, bool cbc = false) {
        const auto mode = cbc ? BCRYPT_CHAIN_MODE_CBC : BCRYPT_CHAIN_MODE_ECB;
        Crypto(BCryptSetProperty(provider.handle,BCRYPT_CHAINING_MODE,
                                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
                                ULONG((wcslen(mode)+1)*sizeof(wchar_t)),0));
        Crypto(BCryptGenerateSymmetricKey(provider.handle,&handle,nullptr,0,
                                          const_cast<PUCHAR>(secret.data()),ULONG(secret.size()),0));
    }
    ~Aes() { if (handle) BCryptDestroyKey(handle); }
    void Run(Mut data, bool encrypt, Mut iv = {}) {
        ULONG count{};
        auto call = encrypt ? BCryptEncrypt : BCryptDecrypt;
        Crypto(call(handle,data.data(),ULONG(data.size()),nullptr,iv.data(),ULONG(iv.size()),
                    data.data(),ULONG(data.size()),&count,0));
        Require(count == data.size(), "Incomplete AES operation.");
    }
};
struct Reader {
    std::uint64_t size{};
    virtual ~Reader() = default;
    virtual void Read(std::uint64_t offset, Mut output) = 0;
    Bytes Get(std::uint64_t offset, size_t length) {
        Bytes result(length); Read(offset,result); return result;
    }
};
struct File final : Reader {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit File(const fs::path& path) {
        handle = CreateFileW(NativePath(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL|FILE_FLAG_RANDOM_ACCESS,nullptr);
        Require(handle != INVALID_HANDLE_VALUE,"Cannot open package or comparison file.");
        LARGE_INTEGER bytes{};
        if (!GetFileSizeEx(handle,&bytes) || bytes.QuadPart < 0) { CloseHandle(handle); handle=INVALID_HANDLE_VALUE; throw std::runtime_error("Cannot read file length."); }
        size = std::uint64_t(bytes.QuadPart); Require(size <= MaxFile,"File exceeds the supported size limit.");
    }
    ~File() override { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    void Read(std::uint64_t offset, Mut output) override {
        Range(offset,output.size(),size); LARGE_INTEGER pos{}; pos.QuadPart=offset;
        Require(SetFilePointerEx(handle,pos,nullptr,FILE_BEGIN),"Cannot seek package data.");
        while (!output.empty()) {
            DWORD done{}, wanted=DWORD(std::min<size_t>(output.size(),16*1024*1024));
            Require(ReadFile(handle,output.data(),wanted,&done,nullptr) && done==wanted,"Cannot read package data.");
            output=output.subspan(done);
        }
    }
};
struct Slice final : Reader {
    std::shared_ptr<Reader> parent; std::uint64_t start;
    Slice(std::shared_ptr<Reader> p, std::uint64_t at, std::uint64_t count) : parent(std::move(p)),start(at) {
        Range(at,count,parent->size); size=count;
    }
    void Read(std::uint64_t at, Mut output) override { Range(at,output.size(),size); parent->Read(start+at,output); }
};
struct Memory final : Reader {
    Bytes bytes;
    explicit Memory(Bytes b) : bytes(std::move(b)) { size=bytes.size(); }
    void Read(std::uint64_t at, Mut output) override {
        Range(at,output.size(),size); std::copy_n(bytes.data()+at,output.size(),output.data());
    }
};
struct Xts final : Reader {
    std::shared_ptr<Reader> parent; Aes data,tweak; size_t plain;
    std::uint64_t cached = UINT64_MAX; Bytes block, masks;
    Xts(std::shared_ptr<Reader> p, View key, size_t unencrypted)
        : parent(std::move(p)),data(key.subspan(16,16)),tweak(key.first(16)),plain(unencrypted) {
        size=parent->size; block.resize(65536); masks.resize(block.size());
    }
    void Read(std::uint64_t at, Mut output) override {
        Range(at,output.size(),size);
        while (!output.empty()) {
            const auto index=at/65536, start=index*65536;
            const auto count=size_t(std::min<std::uint64_t>(65536,size-start));
            if (cached!=index) {
                parent->Read(start,Mut(block).first(count));
                Require(count%4096==0,"Encrypted PFS size is not sector-aligned.");
                const size_t first=start>=plain ? 0 : size_t(std::min<std::uint64_t>(count,plain-start));
                const size_t sectors=(count-first)/4096;
                Bytes seeds(sectors*16);
                for (size_t s=0;s<sectors;++s) {
                    auto sector=(start+first+s*4096)/4096;
                    for (unsigned n=0;n<8;++n) seeds[s*16+n]=static_cast<unsigned char>(sector>>(n*8));
                }
                if (!seeds.empty()) tweak.Run(seeds,true);
                for (size_t s=0;s<sectors;++s) {
                    std::array<unsigned char,16> value{}; std::copy_n(seeds.data()+s*16,16,value.data());
                    for (size_t p=first+s*4096;p<first+(s+1)*4096;p+=16) {
                        std::copy(value.begin(),value.end(),masks.begin()+p);
                        unsigned carry=0;
                        for (auto& v:value) { unsigned next=v>>7; v=static_cast<unsigned char>((v<<1)|carry); carry=next; }
                        if (carry) value[0]^=0x87;
                    }
                }
                for (size_t n=first;n<count;++n) block[n]^=masks[n];
                if (count>first) data.Run(Mut(block).subspan(first,count-first),false);
                for (size_t n=first;n<count;++n) block[n]^=masks[n];
                cached=index;
            }
            size_t offset=size_t(at-start), n=std::min(output.size(),count-offset);
            std::copy_n(block.data()+offset,n,output.data()); output=output.subspan(n); at+=n;
        }
    }
};
struct Pfsc final : Reader {
    std::shared_ptr<Reader> parent; size_t block_size; std::vector<std::uint64_t> offsets;
    std::uint64_t cached=UINT64_MAX; Bytes block;
    explicit Pfsc(std::shared_ptr<Reader> p) : parent(std::move(p)) {
        auto h=parent->Get(0,48); Require(Number(h,0,4)==0x43534650 && Number(h,4,4)==0,"Invalid PFSC header.");
        block_size=size_t(Number(h,12,4)); size=Number(h,40,8);
        Require(block_size>=4096 && block_size<=1024*1024 && !(block_size&(block_size-1)) &&
                Number(h,16,8)==block_size && size<=MaxFile,"Unsupported PFSC geometry.");
        const auto count=(size+block_size-1)/block_size;
        Require(count<=16*1024*1024,"PFSC block table exceeds the supported limit.");
        auto table=parent->Get(Number(h,24,8),size_t((count+1)*8));
        for (size_t i=0;i<=count;++i) offsets.push_back(Number(table,i*8,8));
        for (size_t i=0;i<count;++i) Require(offsets[i+1]>=offsets[i] && offsets[i+1]<=parent->size,"Invalid PFSC block offset.");
        block.resize(block_size);
    }
    void Read(std::uint64_t at, Mut output) override {
        Range(at,output.size(),size);
        while (!output.empty()) {
            auto index=at/block_size;
            if (cached!=index) {
                auto count=offsets[index+1]-offsets[index];
                Require(count>0 && count<=block_size,"Unsupported sparse or malformed PFSC block.");
                if (count==block_size) parent->Read(offsets[index],block);
                else {
                    auto packed=parent->Get(offsets[index],size_t(count)); mz_ulong expanded=block_size;
                    Require(mz_uncompress(block.data(),&expanded,packed.data(),mz_ulong(packed.size()))==MZ_OK &&
                            expanded==std::min<std::uint64_t>(block_size,size-index*block_size),"Invalid compressed PFSC block.");
                }
                cached=index;
            }
            const size_t offset=size_t(at%block_size), n=std::min(output.size(),block_size-offset);
            std::copy_n(block.data()+offset,n,output.data()); at+=n; output=output.subspan(n);
        }
    }
};
struct Inode { std::uint16_t mode; std::uint32_t flags,blocks; std::uint64_t size,expanded; std::array<std::uint32_t,17> pointers; };
struct NodeReader final : Reader {
    std::shared_ptr<Reader> parent; size_t block_size; std::vector<std::uint32_t> map;
    NodeReader(std::shared_ptr<Reader> p, size_t b, const Inode& ino, std::vector<std::uint32_t> blocks)
        : parent(std::move(p)),block_size(b),map(std::move(blocks)) { size=ino.size; }
    void Read(std::uint64_t at, Mut output) override {
        Range(at,output.size(),size);
        while (!output.empty()) {
            auto index=size_t(at/block_size), offset=size_t(at%block_size);
            Require(index<map.size(),"Incomplete PFS file block map.");
            size_t count=std::min(output.size(),block_size-offset);
            // Coalesce adjacent physical blocks to avoid needless per-block source seeks.
            while (count<output.size() && (offset+count)%block_size==0) {
                size_t next=index+(offset+count)/block_size;
                if (next>=map.size() || map[next]!=map[next-1]+1) break;
                count+=std::min(block_size,output.size()-count);
            }
            parent->Read(std::uint64_t(map[index])*block_size+offset,output.first(count));
            at+=count; output=output.subspan(count);
        }
    }
};
struct Pfs {
    std::shared_ptr<Reader> reader; size_t block_size, stride; bool signed_blocks;
    std::uint64_t mapped_blocks=0;
    std::vector<Inode> inodes;
    Pfs(std::shared_ptr<Reader> source, View ekpfs={}, std::uint64_t flags=0) : reader(std::move(source)) {
        auto h=reader->Get(0,0x400); const auto mode=Number(h,0x1c,2);
        Require(Number(h,0,8)==1 && Number(h,8,8)==20130315,"Unsupported PFS version or invalid key.");
        Require(!(mode&2),"64-bit PFS inodes are not supported by this build.");
        signed_blocks=mode&1; stride=signed_blocks?36:4; block_size=size_t(Number(h,0x20,4));
        Require(block_size>=4096 && block_size<=1024*1024 && !(block_size&(block_size-1)),"Unsupported PFS block size.");
        if (mode&4) {
            Require(ekpfs.size()==32,"Encrypted PFS requires a verified package passcode.");
            Bytes key(ekpfs.begin(),ekpfs.end()); View seed=View(h).subspan(0x370,16);
            if (flags&0x2000000000000000ull) { auto k=Hash(seed,key); key.assign(k.begin(),k.end()); }
            Bytes message(20); message[0]=1; std::copy(seed.begin(),seed.end(),message.begin()+4);
            auto generated=Hash(message,key); reader=std::make_shared<Xts>(reader,generated,block_size);
        }
        const auto count=Number(h,0x30,8), blocks=Number(h,0x40,8); const size_t record=signed_blocks?0x2c8:0xa8;
        const size_t per_block=block_size/record;
        Require(count>0 && count<=500000 && blocks>0 && blocks<=reader->size/block_size &&
                count<=blocks*per_block,"Invalid PFS inode table.");
        for (size_t i=0;i<count;++i) {
            auto b=reader->Get(block_size*(1+i/per_block)+(i%per_block)*record,record);
            Inode ino{std::uint16_t(Number(b,0,2)),std::uint32_t(Number(b,4,4)),std::uint32_t(Number(b,0x60,4)),Number(b,8,8),Number(b,16,8),{}};
            Require(ino.size<=MaxFile && ino.expanded<=MaxFile,"PFS inode exceeds the supported size limit.");
            for (size_t n=0;n<17;++n) ino.pointers[n]=std::uint32_t(Number(b,0x64+n*stride+(signed_blocks?32:0),4));
            inodes.push_back(ino);
        }
    }
    std::shared_ptr<Reader> Open(size_t number, bool decompress=false) {
        Require(number<inodes.size(),"Invalid PFS inode reference."); const auto& ino=inodes[number];
        Require(ino.blocks<=8*1024*1024-mapped_blocks && ino.size<=std::uint64_t(ino.blocks)*block_size,"PFS block map exceeds the supported limit.");
        mapped_blocks+=ino.blocks;
        std::vector<std::uint32_t> map; map.reserve(ino.blocks);
        if (ino.blocks && (ino.blocks==1 || ino.pointers[1]==UINT32_MAX || ino.pointers[1]==0)) {
            Require(ino.pointers[0]>0 && std::uint64_t(ino.pointers[0])+ino.blocks<=reader->size/block_size,"Invalid contiguous PFS extent.");
            for (size_t i=0;i<ino.blocks;++i) map.push_back(ino.pointers[0]+std::uint32_t(i));
        } else if (ino.blocks) {
            for (size_t n=0;n<12 && map.size()<ino.blocks;++n) map.push_back(ino.pointers[n]);
            std::set<std::uint32_t> visited;
            std::function<void(std::uint32_t,unsigned)> indirect=[&](std::uint32_t block,unsigned level) {
                Require(block>0 && block<reader->size/block_size && visited.insert(block).second,"Invalid or cyclic PFS indirect block.");
                auto data=reader->Get(std::uint64_t(block)*block_size,block_size);
                for (size_t at=signed_blocks?32:0;at+4<=data.size() && map.size()<ino.blocks;at+=stride) {
                    auto pointer=std::uint32_t(Number(data,at,4));
                    if (level==1) map.push_back(pointer); else indirect(pointer,level-1);
                }
            };
            for (unsigned n=0;n<5 && map.size()<ino.blocks;++n) indirect(ino.pointers[12+n],n+1);
        }
        Require(map.size()==ino.blocks,"Incomplete PFS block map.");
        for (auto b:map) Require(b>0 && b<reader->size/block_size,"Invalid PFS data block.");
        std::shared_ptr<Reader> result=std::make_shared<NodeReader>(reader,block_size,ino,std::move(map));
        if (decompress && (ino.flags&1) && ino.size!=ino.expanded && ino.size) result=std::make_shared<Pfsc>(result);
        return result;
    }
    struct Child { std::uint32_t inode,type; std::string name; };
    std::vector<Child> Children(size_t inode) {
        Require(inode<inodes.size() && (inodes[inode].mode&0xf000)==0x4000,"PFS directory inode is invalid.");
        auto input=Open(inode); Require(input->size<=64*1024*1024,"PFS directory exceeds the supported size limit.");
        auto data=input->Get(0,size_t(input->size)); std::vector<Child> result;
        for (size_t at=0;at<data.size();) {
            if (data.size()-at<16) break;
            auto length=Number(data,at+12,4); if (!length) { at=((at/block_size)+1)*block_size; continue; }
            auto name_length=Number(data,at+8,4);
            auto type=std::uint32_t(Number(data,at+4,4));
            Require(length%8==0 && length>=24 && length<=block_size-at%block_size && length<=data.size()-at &&
                    name_length>0 && name_length<length-16 && data[at+16+name_length]==0,"Malformed PFS directory entry.");
            std::string name(reinterpret_cast<char*>(data.data()+at+16),size_t(name_length));
            if (type!=4 && type!=5) {
                Require(type==2 || type==3,"Unsupported PFS link or special file.");
                EntryPath(name); Require(name.find('/')==std::string::npos,"Invalid PFS path component.");
                result.push_back({std::uint32_t(Number(data,at,4)),type,std::move(name)});
            } else Require(name=="." || name=="..","Invalid PFS dot entry.");
            at+=size_t(length);
        }
        return result;
    }
    size_t Root() {
        for (const auto& child:Children(0)) if (child.name=="uroot" && child.type==3) return child.inode;
        throw std::runtime_error("PFS root directory is missing.");
    }
};
struct CaseLess {
    bool operator()(const std::wstring& a,const std::wstring& b) const {
        return CompareStringOrdinal(a.data(),int(a.size()),b.data(),int(b.size()),TRUE)==CSTR_LESS_THAN;
    }
};
struct Output { std::string name; fs::path path; std::shared_ptr<Reader> source; };
struct Plan {
    std::vector<Output> files; std::map<std::wstring,bool,CaseLess> names;
    std::vector<fs::path> directories; std::set<std::wstring,CaseLess> declared_directories;
    void Directory(const std::string& name) {
        auto path=EntryPath(name); auto key=path.generic_wstring();
        Require(declared_directories.insert(key).second,"Duplicate or case-colliding package directory.");
        auto [it,inserted]=names.emplace(key,true);
        Require(it->second,"Package directory collides with a file."); directories.push_back(std::move(path));
    }
    void Add(std::string name, std::shared_ptr<Reader> source) {
        auto path=EntryPath(name); auto key=path.generic_wstring();
        Require(CompareStringOrdinal(key.c_str(),int(key.size()),L".hmv-pkg-incomplete",-1,TRUE)!=CSTR_EQUAL,"Reserved extraction marker name.");
        Require(names.emplace(key,false).second,"Duplicate or case-colliding package output name.");
        auto parent=path.parent_path();
        while (!parent.empty()) {
            auto [it,inserted]=names.emplace(parent.generic_wstring(),true);
            Require(it->second,"Package file/directory collision."); parent=parent.parent_path();
        }
        files.push_back({std::move(name),std::move(path),std::move(source)});
        Require(files.size()<=200000,"Package contains too many files.");
    }
    void Tree(Pfs& pfs,size_t inode,const std::string& prefix,std::set<size_t>& active,unsigned depth=0) {
        Require(depth<64 && active.insert(inode).second,"Cyclic or excessively deep PFS directory tree.");
        for (const auto& child:pfs.Children(inode)) {
            const auto name=prefix+child.name;
            if (child.type==3) { Directory(name); Tree(pfs,child.inode,name+"/",active,depth+1); }
            else Add(name,pfs.Open(child.inode,true));
        }
        active.erase(inode);
    }
};
std::array<unsigned char,32> Derived(View content,std::string_view pass,unsigned index) {
    std::array<unsigned char,4> i{0,0,0,static_cast<unsigned char>(index)};
    auto a=Hash(i), b=Hash(content); Bytes bytes(a.begin(),a.end()); bytes.insert(bytes.end(),b.begin(),b.end());
    bytes.insert(bytes.end(),pass.begin(),pass.end()); return Hash(bytes);
}
void PlainPath(const fs::path& path) {
    auto text=path.wstring();
    if (text.starts_with(L"\\\\?\\UNC\\")) text=L"\\\\"+text.substr(8);
    else if (text.starts_with(L"\\\\?\\")) text=text.substr(4);
    for (auto at=fs::absolute(fs::path(text));!at.empty() && at!=at.root_path();) {
        DWORD attr=GetFileAttributesW(NativePath(at).c_str());
        if (attr!=INVALID_FILE_ATTRIBUTES) Require(!(attr&FILE_ATTRIBUTE_REPARSE_POINT),"Reparse points are not accepted in output paths.");
        else Require(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND,"Cannot inspect output path.");
        auto parent=at.parent_path(); if (parent==at) break; at=std::move(parent);
    }
}
}

fs::path EntryPath(const std::string& name) {
    Require(!name.empty() && name.size()<=4096 && name.find('\0')==std::string::npos,"Invalid package filename.");
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),nullptr,0);
    Require(length>0,"Package filename is not valid UTF-8."); std::wstring wide(size_t(length),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),int(name.size()),wide.data(),length);
    size_t start=0;
    while (start<wide.size()) {
        auto end=wide.find(L'/',start); if (end==std::wstring::npos) end=wide.size();
        auto part=wide.substr(start,end-start);
        Require(!part.empty() && part!=L"." && part!=L".." && part.back()!=L'.' && part.back()!=L' ' && part.size()<=255,"Unsafe package filename component.");
        for (wchar_t c:part) Require(c>=32 && !wcschr(L"<>:\"\\|?*",c),"Invalid Windows package filename.");
        auto stem=part.substr(0,part.find(L'.')); for (auto& c:stem) c=towupper(c);
        Require(stem!=L"CON" && stem!=L"PRN" && stem!=L"AUX" && stem!=L"NUL" &&
                stem!=L"CONIN$" && stem!=L"CONOUT$" && stem!=L"CLOCK$" &&
                !(stem.size()==4 && (stem.starts_with(L"COM")||stem.starts_with(L"LPT")) &&
                  ((stem[3]>=L'1' && stem[3]<=L'9') || stem[3]==L'¹' || stem[3]==L'²' || stem[3]==L'³')),"Reserved Windows package filename.");
        start=end+1;
    }
    Require(wide.back()!=L'/',"Package file path ends in a separator."); return fs::path(wide);
}

Summary Process(const fs::path& package,const fs::path& requested_directory,const Options& options) {
    Require(!options.title_folder || (!options.list && !options.verify && options.only.empty()),"Title folders require a complete extraction.");
    auto directory=NativePath(requested_directory.empty()?fs::current_path():requested_directory);
    auto input=std::make_shared<File>(package); auto h=input->Get(0,0x1000);
    Require(Number(h,0,4,true)==0x7f434e54,"Not a PS4 PKG file.");
    Require(!(Number(h,0x78,4,true)&0x01000000),"Delta patch packages require an installed base and are not supported.");
    const auto count=Number(h,0x10,4,true), table_at=Number(h,0x18,4,true);
    Require(count>0 && count<=10000,"Invalid PKG entry count."); auto table=input->Get(table_at,size_t(count*32));
    std::array<unsigned char,32> ekpfs{}; bool have_key=false; std::array<bool,7> entry_key_valid{}; Bytes names;
    for (size_t i=0;i<count;++i) {
        View entry=View(table).subspan(i*32,32); auto id=Number(entry,0,4,true);
        auto at=Number(entry,16,4,true), size=Number(entry,20,4,true); Range(at,size,input->size);
        if (id==0x10) {
            Require(size>=0x100 && options.passcode.size()==32,"A package creation passcode must contain exactly 32 characters.");
            auto hashes=input->Get(at,0x100);
            for (unsigned index=0;index<7;++index) {
                auto derived=Derived(View(h).subspan(0x40,48),options.passcode,index);
                auto digest=Hash(derived); for (size_t n=0;n<32;++n) digest[n]^=derived[n];
                entry_key_valid[index]=std::equal(digest.begin(),digest.end(),hashes.begin()+0x20+index*32);
                if (index==1) ekpfs=derived;
            }
            have_key=entry_key_valid[1];
        }
        if (id==0x200) { Require(size<=16*1024*1024,"PKG filename table is too large."); names=input->Get(at,size_t(size)); }
    }
    const auto pfs_at=Number(h,0x410,8,true), pfs_size=Number(h,0x418,8,true), flags=Number(h,0x408,8,true);
    auto image=std::make_shared<Slice>(input,pfs_at,pfs_size);
    const auto pfs_header=image->Get(0,0x400);
    if (Number(pfs_header,0x1c,2)&4)
        Require(have_key,"PKG passcode is unknown or incorrect. This native extractor needs the creation passcode; retail-license recovery is not supported.");
    Pfs outer(image,have_key?View(ekpfs):View{},flags); Plan plan; auto root=outer.Root();
    std::shared_ptr<Reader> nested;
    for (const auto& child:outer.Children(root)) if (child.name=="pfs_image.dat" && child.type==2) nested=outer.Open(child.inode);
    if (nested) {
        auto magic=nested->Get(0,4); if (Number(magic,0,4)==0x43534650) nested=std::make_shared<Pfsc>(nested);
        Pfs inner(nested); std::set<size_t> active; plan.Tree(inner,inner.Root(),"",active);
    } else { std::set<size_t> active; plan.Tree(outer,root,"",active); }

    // Shell metadata is separate from the nested filesystem in PS4 packages.
    const std::map<unsigned,std::string> known{{0x400,"license.dat"},{0x401,"license.info"},{0x402,"nptitle.dat"},
        {0x403,"npbind.dat"},{0x404,"selfinfo.dat"},{0x406,"imageinfo.dat"},{0x409,"psreserved.dat"},
        {0x1000,"param.sfo"},{0x1001,"playgo-chunk.dat"},{0x1002,"playgo-chunk.sha"},{0x1003,"playgo-manifest.xml"},
        {0x1004,"pronunciation.xml"},{0x1005,"pronunciation.sig"},{0x1200,"icon0.png"},{0x1220,"pic0.png"},
        {0x1240,"snd0.at9"},{0x1260,"changeinfo/changeinfo.xml"},{0x1280,"icon0.dds"},{0x12a0,"pic0.dds"},{0x1400,"trophy/trophy00.trp"}};
    for (size_t i=0;i<count;++i) {
        View e=View(table).subspan(i*32,32); unsigned id=unsigned(Number(e,0,4,true)); if (id<0x400) continue;
        std::string name; auto offset=Number(e,4,4,true);
        if (offset && offset<names.size()) {
            auto end=std::find(names.begin()+offset,names.end(),0); Require(end!=names.end(),"Unterminated PKG metadata filename.");
            name.assign(reinterpret_cast<char*>(names.data()+offset),size_t(end-(names.begin()+offset)));
        } else if (auto found=known.find(id);found!=known.end()) name=found->second;
        else continue;
        const auto key=EntryPath("sce_sys/"+name).generic_wstring();
        if (auto existing=plan.names.find(key);existing!=plan.names.end()) {
            Require(!existing->second,"PKG metadata collides with a directory."); continue;
        }
        auto size=Number(e,20,4,true); Require(size<=64*1024*1024,"PKG metadata entry is too large.");
        std::shared_ptr<Reader> file=std::make_shared<Slice>(input,Number(e,16,4,true),size);
        if (Number(e,8,4,true)&0x80000000u) {
            Require(have_key,"Encrypted PKG metadata needs a valid passcode.");
            unsigned index=unsigned((Number(e,12,4,true)>>12)&15); Require(index<7,"Unsupported PKG metadata key index.");
            Require(entry_key_valid[index],"PKG metadata entry key does not match the supplied passcode.");
            auto derived=Derived(View(h).subspan(0x40,48),options.passcode,index); Bytes material(e.begin(),e.end());
            material.insert(material.end(),derived.begin(),derived.end()); auto iv_key=Hash(material);
            // Encrypted metadata occupies complete CBC blocks, even when its logical
            // file length is shorter. Read the final ciphertext block before truncating.
            auto bytes=input->Get(Number(e,16,4,true),size_t((size+15)&~std::uint64_t(15)));
            Aes cipher(View(iv_key).subspan(16,16),true);
            std::array<unsigned char,16> iv{}; std::copy_n(iv_key.begin(),16,iv.begin());
            if (!bytes.empty()) cipher.Run(bytes,false,iv);
            bytes.resize(size_t(size));
            file=std::make_shared<Memory>(std::move(bytes));
        }
        plan.Add("sce_sys/"+name,file);
    }
    Summary result;
    for (const auto& file:plan.files) {
        if (!options.only.empty() && std::find(options.only.begin(),options.only.end(),file.name)==options.only.end()) continue;
        ++result.files; Require(file.source->size<=MaxFile-result.bytes,"Package expanded size exceeds the supported limit."); result.bytes+=file.source->size;
    }
    Require(result.files>0,"Package has no selected extractable files.");
    if (options.list) { for (const auto& file:plan.files) std::cout<<file.source->size<<"\t"<<file.name<<"\n"; return result; }
    fs::path final_directory;
    if(options.title_folder) {
        Require(!options.verify && options.only.empty(),"Title folders require a complete extraction.");
        // Read the identity from param.sfo itself, not from the user-controlled PKG name.
        for(const auto& file:plan.files) if(CompareStringOrdinal(file.path.generic_wstring().c_str(),-1,L"sce_sys/param.sfo",-1,TRUE)==CSTR_EQUAL) {
            Require(file.source->size>=20 && file.source->size<=1024*1024,"Invalid param.sfo size.");
            auto data=file.source->Get(0,size_t(file.source->size));
            Require(Number(data,0,4)==0x46535000,"Invalid param.sfo header.");
            const auto keys=Number(data,8,4),values=Number(data,12,4),count=Number(data,16,4);
            Require(count<=1024 && keys>=20+count*16 && values>=keys && values<=data.size(),"Invalid param.sfo tables.");
            for(size_t i=0;i<count;++i) {
                const size_t entry=20+i*16;
                auto key=keys+Number(data,entry,2); Require(key<values,"Invalid param.sfo key.");
                auto end=std::find(data.begin()+key,data.begin()+values,0);
                Require(end!=data.begin()+values,"Unterminated param.sfo key.");
                std::string name(reinterpret_cast<const char*>(data.data()+key),size_t(end-(data.begin()+key)));
                if(name!="TITLE_ID") continue;
                Require(result.title_id.empty(),"Duplicate TITLE_ID field.");
                const auto length=Number(data,entry+4,4),capacity=Number(data,entry+8,4),at=values+Number(data,entry+12,4);
                Require(Number(data,entry+2,2)==0x204 && length==10 && capacity>=length,"Invalid TITLE_ID field.");
                Range(at,capacity,data.size()); Require(data[at+9]==0,"Unterminated TITLE_ID.");
                result.title_id.assign(reinterpret_cast<const char*>(data.data()+at),9);
            }
        }
        Require(result.title_id.size()==9 && result.title_id.starts_with("CUSA") &&
                std::all_of(result.title_id.begin()+4,result.title_id.end(),[](char c){return c>='0' && c<='9';}),
                "The PKG does not contain a valid PS4 TITLE_ID.");
        PlainPath(directory);
        final_directory=directory/fs::path(result.title_id);
        Require(!fs::exists(final_directory),"A folder for this TITLE_ID already exists. Existing games are never overwritten.");
        std::array<unsigned char,16> random{};
        Crypto(BCryptGenRandom(nullptr,random.data(),ULONG(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        result.staging_name=".so5-import-";
        for(unsigned char c:random) { result.staging_name+="0123456789abcdef"[c>>4]; result.staging_name+="0123456789abcdef"[c&15]; }
        directory/=fs::path(result.staging_name);
    }
    if(options.planned) options.planned(result);
    PlainPath(directory);
    const auto marker=directory/L".hmv-pkg-incomplete";
    if (!options.verify) {
        Require(!fs::exists(directory) || (fs::is_directory(directory) && fs::is_empty(directory)),"Extraction requires a new or empty output directory.");
        if(options.title_folder) {
            fs::create_directories(directory.parent_path()); PlainPath(directory.parent_path());
            Require(fs::create_directory(directory),"Cannot reserve a new extraction staging folder.");
        } else fs::create_directories(directory);
        PlainPath(directory);
        Require(fs::space(directory).available>=result.bytes,"Not enough free space for extracted files.");
        HANDLE mark=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,nullptr);
        Require(mark!=INVALID_HANDLE_VALUE,"Cannot create the extraction completion marker."); CloseHandle(mark);
    }
    if (options.only.empty()) for (const auto& relative:plan.directories) {
        const auto target=NativePath(directory/relative); PlainPath(target);
        if (options.verify) Require(fs::is_directory(target),"Comparison directory is missing.");
        else { fs::create_directories(target); PlainPath(target); }
    }
    Bytes buffer(1024*1024), compare(buffer.size()); std::uint64_t done=0;
    if(options.progress) options.progress(0,result.bytes);
    for (const auto& file:plan.files) {
        if (!options.only.empty() && std::find(options.only.begin(),options.only.end(),file.name)==options.only.end()) continue;
        const auto target=NativePath(directory/file.path); PlainPath(target);
        std::unique_ptr<File> reference;
        HANDLE output=INVALID_HANDLE_VALUE;
        if (options.verify) { reference=std::make_unique<File>(target); Require(reference->size==file.source->size,"Comparison file has a different size."); }
        else {
            fs::create_directories(target.parent_path()); PlainPath(target.parent_path());
            output=CreateFileW(NativePath(target).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (output==INVALID_HANDLE_VALUE) {
                const auto code=GetLastError(); auto utf8=target.u8string();
                throw std::runtime_error("Cannot create output file without overwriting existing data (Windows "+
                    std::to_string(code)+"): "+std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size()));
            }
        }
        try {
            for (std::uint64_t at=0;at<file.source->size;) {
                const auto n=size_t(std::min<std::uint64_t>(buffer.size(),file.source->size-at)); auto bytes=Mut(buffer).first(n);
                file.source->Read(at,bytes);
                if (reference) { reference->Read(at,Mut(compare).first(n)); Require(std::equal(bytes.begin(),bytes.end(),compare.begin()),"Extracted content differs from the comparison file."); }
                else { DWORD written{}; Require(WriteFile(output,buffer.data(),DWORD(n),&written,nullptr) && written==n,"Cannot write extracted data."); }
                at+=n; done+=n; if (options.progress) options.progress(done,result.bytes);
            }
            if (output!=INVALID_HANDLE_VALUE) { Require(FlushFileBuffers(output),"Cannot flush extracted data."); CloseHandle(output); output=INVALID_HANDLE_VALUE; }
        } catch (const std::exception& error) {
            if (output!=INVALID_HANDLE_VALUE) CloseHandle(output);
            throw std::runtime_error(file.name+": "+error.what());
        }
    }
    if (!options.verify) {
        if(!final_directory.empty()) {
            PlainPath(final_directory);
            Require(MoveFileW(directory.c_str(),final_directory.c_str()),"Cannot publish the TITLE_ID folder; partial files remain in the staging folder.");
            Require(DeleteFileW((final_directory/L".hmv-pkg-incomplete").c_str()),"Cannot finalize the extraction completion marker.");
        } else Require(DeleteFileW(marker.c_str()),"Cannot finalize the extraction completion marker.");
    }
    return result;
}
}
