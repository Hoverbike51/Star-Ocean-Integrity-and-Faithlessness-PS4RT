// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic package fixtures contain only test strings, never game data.
#include "archive.h"
#include "../launcher/pkg/progress.h"
#include <windows.h>
#include <miniz.h>
#include <cassert>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <vector>
namespace fs=std::filesystem;
using Bytes=std::vector<unsigned char>;
void Put(Bytes& b,size_t at,std::uint64_t value,size_t length,bool big=false) {
    assert(at+length<=b.size());
    for(size_t n=0;n<length;++n) b[at+n]=static_cast<unsigned char>(value>>(8*(big?length-1-n:n)));
}
void Text(Bytes& b,size_t at,const std::string& text) { assert(at+text.size()<=b.size()); std::copy(text.begin(),text.end(),b.begin()+at); }
void Write(const fs::path& path,const Bytes& b) { std::ofstream f(path,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),b.size()); assert(f.good()); }
Bytes Read(const fs::path& path) { std::ifstream f(path,std::ios::binary); assert(f); return Bytes(std::istreambuf_iterator<char>(f),{}); }
template<class F> void Reject(F call) { bool rejected=false; try { call(); } catch(const std::exception&) { rejected=true; } assert(rejected); }
Bytes Hex(std::string_view value) {
    Bytes result;
    for(size_t n=0;n<value.size();n+=2) result.push_back(static_cast<unsigned char>(std::stoul(std::string(value.substr(n,2)),nullptr,16)));
    return result;
}
void Inode(Bytes& b,size_t index,bool signed_blocks,unsigned mode,size_t bytes,std::uint32_t first,std::uint32_t second=UINT32_MAX) {
    size_t at=4096+index*(signed_blocks?0x2c8:0xa8), stride=signed_blocks?36:4;
    Put(b,at,mode,2); Put(b,at+2,1,2); Put(b,at+8,bytes,8); Put(b,at+16,bytes,8);
    Put(b,at+0x60,(bytes+4095)/4096,4);
    for(size_t i=0;i<17;++i) Put(b,at+0x64+i*stride+(signed_blocks?32:0),i==0?first:i==1?second:UINT32_MAX,4);
}
size_t Dirent(Bytes& b,size_t at,unsigned ino,unsigned type,const std::string& name) {
    size_t length=(16+name.size()+1+7)&~size_t(7);
    Put(b,at,ino,4); Put(b,at+4,type,4); Put(b,at+8,name.size(),4); Put(b,at+12,length,4); Text(b,at+16,name); return length;
}
Bytes Pfs(const std::string& name,const Bytes& contents,bool signed_blocks=false,bool fragmented=false) {
    Bytes b(fragmented?7*4096:4*4096+((contents.size()+4095)/4096)*4096);
    Put(b,0,1,8); Put(b,8,20130315,8); Put(b,0x1c,signed_blocks?1:0,2);
    Put(b,0x20,4096,4); Put(b,0x28,1,8); Put(b,0x30,3,8); Put(b,0x40,1,8);
    Inode(b,0,signed_blocks,0x4000,4096,2); Inode(b,1,signed_blocks,0x4000,4096,3);
    Inode(b,2,signed_blocks,0x8000,contents.size(),4,fragmented?6:UINT32_MAX);
    Dirent(b,8192,1,3,"uroot"); Dirent(b,12288,2,2,name);
    if(fragmented) {
        assert(contents.size()>4096 && contents.size()<=8192);
        std::copy_n(contents.begin(),4096,b.begin()+4*4096);
        std::copy(contents.begin()+4096,contents.end(),b.begin()+6*4096);
    } else std::copy(contents.begin(),contents.end(),b.begin()+16384);
    return b;
}
Bytes Pfsc(const Bytes& input) {
    assert(input.size()%4096==0); size_t count=input.size()/4096;
    Bytes result(48+(count+1)*8); Put(result,0,0x43534650,4); Put(result,8,6,4);
    Put(result,12,4096,4); Put(result,16,4096,8); Put(result,24,48,8);
    Put(result,32,result.size(),8); Put(result,40,input.size(),8);
    for(size_t n=0;n<count;++n) {
        Put(result,48+n*8,result.size(),8);
        Bytes compressed(mz_compressBound(4096)); mz_ulong length=compressed.size();
        assert(mz_compress2(compressed.data(),&length,input.data()+n*4096,4096,6)==MZ_OK);
        assert(length<4096); result.insert(result.end(),compressed.begin(),compressed.begin()+length);
    }
    Put(result,48+count*8,result.size(),8); return result;
}
Bytes Package(const Bytes& pfs) {
    Bytes b(65536+pfs.size()); Put(b,0,0x7f434e54,4,true); Put(b,0x10,1,4,true);
    Put(b,0x18,4096,4,true); Put(b,4096,0x1000,4,true);
    Put(b,4096+16,8192,4,true); Put(b,4096+20,4,4,true); Text(b,8192,"meta");
    Put(b,0x410,65536,8,true); Put(b,0x418,pfs.size(),8,true); std::copy(pfs.begin(),pfs.end(),b.begin()+65536);
    return b;
}
int main() {
    const auto root=fs::temp_directory_path()/(L"hmv-pkg-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    assert(fs::create_directory(root)); const auto pkg=root/"fixture.pkg", output=root/L"extracted 空間";
    Bytes content{'o','r','i','g','i','n','a','l'};
    Write(pkg,Package(Pfs("eboot.bin",content)));
    auto result=HmvPkg::Process(pkg,output); assert(result.files==2 && result.bytes==12);
    assert(Read(output/"eboot.bin")==content && Read(output/"sce_sys/param.sfo")==Bytes({'m','e','t','a'}));
    assert(!fs::exists(output/".hmv-pkg-incomplete"));
    // Extraction identity comes from SFO, regardless of the archive filename.
    auto titled=Package(Pfs("eboot.bin",content));
    Bytes sfo(64); Put(sfo,0,0x46535000,4); Put(sfo,4,0x101,4);
    Put(sfo,8,36,4); Put(sfo,12,48,4); Put(sfo,16,1,4);
    Put(sfo,22,0x204,2); Put(sfo,24,10,4); Put(sfo,28,16,4);
    Text(sfo,36,"TITLE_ID"); Text(sfo,48,"CUSA03219");
    Put(titled,4096+20,sfo.size(),4,true); std::copy(sfo.begin(),sfo.end(),titled.begin()+8192);
    Write(pkg,titled);
    HmvPkg::Options named; named.title_folder=true;
    std::string staging; std::uint64_t last_done=0,total_bytes=0; bool first_progress=true;
    named.planned=[&](const HmvPkg::Summary& plan) {
        assert(plan.title_id=="CUSA03219" && plan.staging_name.size()==44);
        staging=plan.staging_name;
    };
    named.progress=[&](auto done,auto total) {
        if(first_progress) assert(done==0);
        first_progress=false; assert(done>=last_done && done<=total);
        last_done=done; total_bytes=total;
    };
    const auto games=root/"Title ID folders";
    assert(HmvPkg::Process(pkg,games,named).title_id=="CUSA03219");
    assert(last_done==total_bytes && total_bytes==content.size()+64);
    assert(Read(games/"CUSA03219/eboot.bin")==content);
    assert(!fs::exists(games/staging) && !fs::exists(games/"CUSA03219/.hmv-pkg-incomplete"));
    Reject([&]{ HmvPkg::Process(pkg,games,named); });
    assert(Read(games/"CUSA03219/eboot.bin")==content && std::distance(fs::directory_iterator(games),fs::directory_iterator{})==1);
    named.progress=[](auto done,auto){if(done) throw std::runtime_error("test interruption");};
    Reject([&]{ HmvPkg::Process(pkg,root/"named interruption",named); });
    assert(!fs::exists(root/"named interruption/CUSA03219"));
    assert(fs::exists(root/"named interruption"/staging/".hmv-pkg-incomplete"));
    auto invalid_title=titled; invalid_title[8192+48+4]='X'; Write(pkg,invalid_title);
    Reject([&]{ HmvPkg::Process(pkg,root/"invalid title",named); });
    assert(!fs::exists(root/"invalid title"));
    // Real progress and UTF-8 lines can be split across any pipe read boundary.
    HmvPkg::Progress progress; HmvPkg::Lines lines;
    std::string protocol="PKG staging: .so5-import-0123456789abcdef0123456789abcdef\r\nPKG title: CUSA03219\n"
        "PKG progress: 0 / 100 bytes (0%)\nPKG progress: 45 / 100 bytes (45%)\n";
    for(char c:protocol) lines.Feed(std::string_view(&c,1),[&](const auto& line){assert(progress.Parse(line));});
    assert(progress.done==45 && progress.total==100 && progress.title=="CUSA03219");
    assert(!progress.Parse("PKG progress: 44 / 100 bytes (44%)"));
    assert(!progress.Parse("PKG progress: 101 / 100 bytes (101%)"));
    assert(!progress.Parse("PKG progress: 18446744073709551616 / 100 bytes (0%)"));
    assert(!progress.Parse("PKG staging: ../../existing-game"));
    assert(!progress.Parse("PKG title: CUSA/3219"));
    assert(progress.done==45);
    // Independent .NET AES-CBC vector: the logical 20-byte file needs a full
    // 32-byte ciphertext read. Its declared key index is 2, not the common 3.
    auto encrypted=Package(Pfs("eboot.bin",content)); Put(encrypted,0x10,2,4,true);
    Put(encrypted,4096+8,0x80000000,4,true); Put(encrypted,4096+12,0x2000,4,true);
    Put(encrypted,4096+20,20,4,true);
    Put(encrypted,4128,0x10,4,true); Put(encrypted,4128+16,12288,4,true); Put(encrypted,4128+20,256,4,true);
    const auto digest1=Hex("b47d91eb5660edb8d8dc7b27236b2e2f9606853dbaa00d88d6bde6b6e95b1cb2");
    const auto digest2=Hex("3c8209a396bf04c6592ddbb6a325e76c1ef280011de6e8f05ae12b58463e02f9");
    const auto ciphertext=Hex("240239943eab5a81985f4f4689b1c2ab070ebf57cd8214dc4d26ee107a9c1191");
    std::copy(digest1.begin(),digest1.end(),encrypted.begin()+12288+64);
    std::copy(digest2.begin(),digest2.end(),encrypted.begin()+12288+96);
    std::copy(ciphertext.begin(),ciphertext.end(),encrypted.begin()+8192); Write(pkg,encrypted);
    assert(HmvPkg::Process(pkg,root/"encrypted-meta").bytes==content.size()+20);
    Bytes expected_meta(20); for(size_t n=0;n<20;++n) expected_meta[n]=static_cast<unsigned char>(n);
    assert(Read(root/"encrypted-meta/sce_sys/param.sfo")==expected_meta);
    HmvPkg::Options wrong_pass; wrong_pass.passcode=std::string(32,'1');
    Reject([&]{ HmvPkg::Process(pkg,root/"wrong-passcode",wrong_pass); });
    assert(!fs::exists(root/"wrong-passcode"));
    Write(pkg,Package(Pfs("eboot.bin",content)));
    HmvPkg::Options verify; verify.verify=true; assert(HmvPkg::Process(pkg,output,verify).files==2);
    Reject([&]{ HmvPkg::Process(pkg,output); }); assert(Read(output/"eboot.bin")==content);
    Write(output/"eboot.bin",Bytes{'c','h','a','n','g','e','d','!'}); Reject([&]{ HmvPkg::Process(pkg,output,verify); });
    auto nested=Package(Pfs("pfs_image.dat",Pfsc(Pfs("eboot.bin",content)))); Write(pkg,nested);
    assert(HmvPkg::Process(pkg,root/"nested").bytes==12); assert(Read(root/"nested/eboot.bin")==content);
    const auto long_output=root/std::string(130,'a')/std::string(130,'b');
    assert(HmvPkg::Process(pkg,long_output).bytes==12);
    auto with_dirs=Pfs("file.bin",content); with_dirs.resize(7*4096); Put(with_dirs,0x30,5,8);
    std::fill(with_dirs.begin()+12288,with_dirs.begin()+16384,0);
    Inode(with_dirs,3,false,0x4000,4096,5); Inode(with_dirs,4,false,0x4000,4096,6);
    Dirent(with_dirs,12288,3,3,"folder"); auto next=Dirent(with_dirs,5*4096,2,2,"file.bin");
    Dirent(with_dirs,5*4096+next,4,3,"empty"); Write(pkg,Package(with_dirs));
    assert(HmvPkg::Process(pkg,root/"directories").bytes==12);
    assert(Read(root/"directories/folder/file.bin")==content && fs::is_directory(root/"directories/folder/empty"));
    content.assign(6000,'A'); std::fill(content.begin()+4096,content.end(),'B');
    Write(pkg,Package(Pfs("eboot.bin",content,true,true)));
    assert(HmvPkg::Process(pkg,root/"fragmented").bytes==6004); assert(Read(root/"fragmented/eboot.bin")==content);
    for(const std::string name:{"../outside","/absolute","C:/drive","folder\\escape","a:stream","NUL.txt","com1","COM¹.txt","bad.","bad ","x//y","x/../y"})
        Reject([&]{ HmvPkg::EntryPath(name); });
    for(const std::string name:{"safe/file.bin","sce_sys/param.sfo","textures/日本語.png"}) assert(!HmvPkg::EntryPath(name).empty());
    Write(pkg,Package(Pfs("../outside",Bytes{1,2,3})));
    Reject([&]{ HmvPkg::Process(pkg,root/"unsafe"); }); assert(!fs::exists(root/"unsafe"));
    auto damaged=nested; damaged.resize(600); Write(pkg,damaged); Reject([&]{ HmvPkg::Process(pkg,root/"truncated"); });
    damaged=nested; Put(damaged,0x410,UINT64_MAX,8,true); Write(pkg,damaged); Reject([&]{ HmvPkg::Process(pkg,root/"overflow"); });
    damaged=Package(Pfs("eboot.bin",Bytes{1})); Put(damaged,65536+0x1c,4,2); Write(pkg,damaged);
    Reject([&]{ HmvPkg::Process(pkg,root/"unknown-key"); });
    auto loop=Pfs("loop",Bytes{1}); Put(loop,12288,1,4); Put(loop,12288+4,3,4); Write(pkg,Package(loop));
    Reject([&]{ HmvPkg::Process(pkg,root/"cyclic"); });
    auto bad_map=Pfs("eboot.bin",Bytes{1}); Put(bad_map,4096+2*0xa8+0x64,UINT32_MAX,4); Write(pkg,Package(bad_map));
    Reject([&]{ HmvPkg::Process(pkg,root/"invalid-map"); });
    // Simulate interruption after the first write; completion marker must survive.
    Write(pkg,nested); HmvPkg::Options stop; stop.progress=[](auto,auto){ throw std::runtime_error("test interruption"); };
    Reject([&]{ HmvPkg::Process(pkg,root/"interrupted",stop); }); assert(fs::exists(root/"interrupted/.hmv-pkg-incomplete"));
    // The test owns this freshly created directory and never creates links.
    assert(fs::is_directory(root) && !(GetFileAttributesW(root.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)); fs::remove_all(root);
    std::cout<<"PASS: original native PKG/PFS/PFSC reader, Unicode, fragmented files, bounds, paths, overwrite refusal and interruption\n";
}
