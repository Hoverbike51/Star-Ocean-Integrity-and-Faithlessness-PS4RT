// SPDX-License-Identifier: GPL-2.0-or-later
#include "../launcher/cheat_profiles.h"
#include "../launcher/cheat_catalog.h"
#include "../launcher/native_preparer.h"
#include "../src/runtime_cheats.h"
#include "../src/sha256_win.h"
#include <cassert>
#include <array>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
extern "C" void runtime_cheats_test_fail_after(int);
extern "C" void runtime_cheats_test_allow_synthetic(int);
namespace fs=std::filesystem;
int main(int argc,char** argv) {
    assert(So5Cheats::Catalog(3)==nullptr);
    assert(So5Cheats::Definitions.size()==8);
    if(argc>=3){
        if(argc==3)ps4rt::prepare_title(fs::path(argv[1]),fs::path(argv[2]),{});
        So5Cheats::Result inspected;
        ps4rt::prepare_title(fs::path(argv[1]),fs::path(argv[2]),{},&inspected);
        assert(inspected.names.size()==8);
        for(const auto& e:inspected.entries)assert(e.builtin&&e.available&&e.id!=3);
        const auto movement=std::find_if(inspected.entries.begin(),inspected.entries.end(),[](const auto& e){return e.id==9;});
        assert(movement!=inspected.entries.end()&&movement->choices==std::vector<float>({1.5f,2.f}));
        std::cout<<"PASS: eight native definitions; retired inventory hook absent; no external profiles\n";return 0;
    }
    auto dir=fs::current_path()/("so5-cheats-test-"+std::to_string(GetCurrentProcessId()));
    fs::create_directories(dir);
    if(argc>1&&std::string(argv[1])=="integrity") {
        // A synthetic image contains only one real native hook's expected original bytes.
        std::vector<std::uint8_t> source(0xA00000);
        const std::uint8_t hp_original[]={0xc5,0xfa,0x11,0x8b,0xb4,0,0,0};
        std::memcpy(source.data()+0x23B9E8,hp_original,sizeof(hp_original));
        const std::array<So5Cheats::Range,1> native_ranges{{{0,source.size(),true,false}}};
        const auto compiled=So5Cheats::Compile(dir/"native.txt","CUSA03219","01.00",source,native_ranges);
        assert(compiled.names.size()==1);
        assert(compiled.entries.front().id==1&&compiled.entries.front().available);
        auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,source.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(memory);
        std::memcpy(memory,source.data(),source.size());DWORD old;
        assert(VirtualProtect(memory,source.size(),PAGE_EXECUTE_READ,&old));
        CheatRange exec{0,source.size()};runtime_cheats_bind_image(memory,source.size(),nullptr,0);
        assert(runtime_cheats_load((dir/"native.txt").string().c_str(),memory,&exec,1,nullptr)==0);
        assert(runtime_cheat_count()==1&&runtime_cheat_catalog_id(0)==1&&runtime_cheat_builtin(0));
        char message[512]{};
        assert(runtime_cheat_set(0,1,message,sizeof(message))==0);
        assert(runtime_cheat_set(0,0,message,sizeof(message))==0);
        assert(!std::memcmp(memory+0x23B9E8,hp_original,sizeof(hp_original)));
        std::ifstream in(dir/"native.txt",std::ios::binary);std::string table((std::istreambuf_iterator<char>(in)),{});in.close();
        const auto line=table.find("line ");assert(line!=std::string::npos);
        const auto on=table.find(' ',line+5)+1;table[on+1]=table[on+1]=='9'?'8':'9';
        {std::ofstream out(dir/"tampered.txt",std::ios::binary);out<<table;}
        assert(runtime_cheats_load((dir/"tampered.txt").string().c_str(),memory,&exec,1,nullptr)<0);
        assert(runtime_cheat_count()==0&&!std::memcmp(memory+0x23B9E8,hp_original,sizeof(hp_original)));
        VirtualFree(memory,0,MEM_RELEASE);fs::remove_all(dir);
        std::cout<<"PASS: real native fingerprint accepted; modified native bytes refused before writes\n";return 0;
    }
    std::array<std::uint8_t,8192> image{};
    const std::uint8_t original[]={0xb8,7,0,0,0,0xc3};std::memcpy(image.data(),original,6);
    const std::uint8_t valued_original[]={0xb8,0,0,0,0,0xc3};std::memcpy(image.data()+0x20,valued_original,6);
    std::uint32_t money=10;std::memcpy(image.data()+4096,&money,4);
    char hash[65];assert(so5_image_sha256(image.data(),image.size(),hash));
    const std::array<So5Cheats::Range,2> ranges{{{0,4096,true,false},{4096,8192,false,true}}};
    // Legacy XML is deliberately ignored, including malformed/renamed profiles.
    {std::ofstream old(dir/"old-profile.shn");old<<"<!DOCTYPE Trainer [<!ENTITY x 'bad'>]><Trainer/>";}
    const auto preview=So5Cheats::Compile(dir/"preview.txt","CUSA03219","01.00",image,ranges,false);
    assert(preview.names.empty()&&preview.entries.size()==8&&!fs::exists(dir/"preview.txt"));
    for(const auto& entry:preview.entries)assert(entry.builtin&&entry.id!=3);
    const auto wrong_title=So5Cheats::Compile(dir/"wrong.txt","CUSA99999","01.00",image,ranges,false);
    assert(wrong_title.entries.empty());
    const auto wrong_version=So5Cheats::Compile(dir/"wrong.txt","CUSA03219","01.01",image,ranges,false);
    assert(wrong_version.entries.empty());
    const auto put_table=[&](const std::string& text){std::ofstream out(dir/"table.txt",std::ios::binary);out<<text;};
    const std::string hook="cheat 2 Synthetic function\nline 0 E97B000000 B807000000\nline 80 B809000000C3 000000000000\n";
    const auto table=std::string("PS4RTCH1\naction 1000 5 20 ")+hash+" Add currency\n"+hook+
        "cheat 2 Synthetic value\nline 20 E99B000000 B800000000\n"
        "line C0 8B050A000000C30000000000000000000000803F 0000000000000000000000000000000000000000\nvalue D0 f32 1,2\n";
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(memory);std::memcpy(memory,image.data(),image.size());
    CheatRange data{4096,8192},exec{0,4096};runtime_cheats_bind_image(memory,8192,&data,1);
    DWORD old;assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));
    // Default production policy rejects external tables, forged native IDs and retired ID 3.
    put_table(table);assert(runtime_cheats_load((dir/"table.txt").string().c_str(),memory,&exec,1,nullptr)<0);
    assert(runtime_cheat_count()==0);
    put_table("PS4RTCH1\n"+hook+"meta 1 1\n");
    assert(runtime_cheats_load((dir/"table.txt").string().c_str(),memory,&exec,1,nullptr)<0);
    put_table("PS4RTCH1\n"+hook+"meta 3 1\n");
    assert(runtime_cheats_load((dir/"table.txt").string().c_str(),memory,&exec,1,nullptr)<0);
    // Only this test binary can explicitly opt in to executing synthetic fixture code.
    runtime_cheats_test_allow_synthetic(1);put_table(table);
    assert(runtime_cheats_load((dir/"table.txt").string().c_str(),memory,&exec,1,nullptr)==0);
    assert(runtime_cheat_count()==3&&runtime_cheat_is_action(0)&&runtime_cheat_state(1)==0);
    assert(!runtime_cheat_builtin(1)&&runtime_cheat_catalog_id(1)==0);
    char message[512]{};using Function=int(*)();auto function=reinterpret_cast<Function>(memory);assert(function()==7);
    if(argc>1&&std::string(argv[1])=="rollback") {
        runtime_cheats_test_fail_after(1);assert(runtime_cheat_set(1,1,message,sizeof(message))<0);assert(function()==7);
    } else {
        assert(runtime_cheat_add(0,message,sizeof(message))==0);assert(*reinterpret_cast<unsigned*>(memory+4096)==15);
        assert(runtime_cheat_add(0,message,sizeof(message))==0);assert(*reinterpret_cast<unsigned*>(memory+4096)==20);
        assert(runtime_cheat_add(0,message,sizeof(message))==0);assert(*reinterpret_cast<unsigned*>(memory+4096)==20);
        assert(runtime_cheat_set(1,1,message,sizeof(message))==0);assert(function()==9);
        assert(runtime_cheat_set(1,0,message,sizeof(message))==0);assert(function()==7);
        auto valued=reinterpret_cast<Function>(memory+0x20);assert(valued()==0);
        assert(runtime_cheat_select(2,0,message,sizeof(message))==0);assert(valued()==0x3f800000);
        assert(runtime_cheat_select(2,1,message,sizeof(message))==0);assert(valued()==0x40000000);
        assert(runtime_cheat_select(2,-1,message,sizeof(message))==0);assert(valued()==0);
    }
    MEMORY_BASIC_INFORMATION protection{};assert(VirtualQuery(memory,&protection,sizeof(protection)));assert(protection.Protect==PAGE_EXECUTE_READ);
    VirtualFree(memory,0,MEM_RELEASE);fs::remove_all(dir);std::cout<<"PASS: native-only compilation, external/forged/retired code refusal, synthetic hook execution and rollback\n";
}
