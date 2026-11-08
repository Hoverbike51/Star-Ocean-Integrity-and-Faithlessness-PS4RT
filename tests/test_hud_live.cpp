// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud_live.h"
#include "hud_preset_library.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <functional>
using namespace So5Hud;
template<class F> void Reject(F fn){bool rejected=false;try{fn();}catch(...){rejected=true;}assert(rejected);}
int main() {
    const auto dir=std::filesystem::temp_directory_path()/LiveToken();assert(std::filesystem::create_directory(dir));
    assert(LivePipe(dir)==LivePipe(dir/L"."));
    assert(LivePipe(dir)==LivePipe(std::filesystem::path(dir.wstring()+L"\\")));
    const auto baseline=Base(Aspect::Superwide);LiveState state;state.Reset(baseline);
    const auto original=state.Current();auto changed=baseline;changed.groups[0].x=48;changed.groups[0].scale=1.25;
    changed.reference_width=3840;changed.reference_height=1080; // schema 3 must survive preview/apply/cancel/lease expiry
    state.Queue(changed,1);assert(state.Current()==original);assert(state.Boundary());
    assert(state.Current()->layout==changed&&state.Applied()==1&&original->layout==baseline);
    state.Reset(baseline);
    const auto first_owner=LiveToken(),second_owner=LiveToken();
    {
        LiveServer server(dir,state,baseline,[]{return "9 GUI resources matched; 123 mapped draws.";});
        auto hello=Exchange(dir,{"",first_owner,"hello",""},nullptr);assert(hello.accepted&&hello.aspect=="32:9"&&hello.message.find("9 GUI resources")!=std::string::npos);
        auto send=[&](std::string action,std::string layout="",std::string owner="") {
            return Exchange(dir,{hello.session,owner.empty()?first_owner:owner,action,layout},nullptr);
        };
        const auto forced=Exchange(dir,{hello.session,first_owner,"preview",Serialize(changed),1u<<7},nullptr);
        assert(forced.accepted&&state.Boundary()&&state.Current()->force_groups==(1u<<7));
        Reject([&]{Exchange(dir,{hello.session,first_owner,"apply",Serialize(changed),1u<<7},nullptr);});
        Reject([&]{Exchange(dir,{hello.session,first_owner,"preview",Serialize(changed),1u<<23},nullptr);});
        assert(send("cancel").accepted&&state.Boundary()&&!state.Current()->force_groups);
        auto result=send("preview",Serialize(changed));assert(result.accepted&&state.Current()->layout==baseline);
        assert(state.Boundary()&&state.Current()->layout==changed&&state.Applied()==result.revision);
        assert(!send("preview",Serialize(changed),second_owner).accepted);
        assert(!Exchange(dir,{"stale-session",first_owner,"cancel",""},nullptr).accepted);
        assert(!send("preview",Serialize(Base(Aspect::Wide))).accepted);
        auto unbound=changed;unbound.groups[23].visible=false;assert(!send("preview",Serialize(unbound)).accepted);
        assert(state.Current()->layout==changed&&!state.Boundary());
        assert(send("cancel").accepted);assert(state.Boundary()&&state.Current()->layout==baseline);
        result=send("apply",Serialize(changed));assert(result.accepted&&Parse(Read(dir/"hud-layout.json",65536))==changed);
        assert(state.Boundary()&&state.Current()->layout==changed);
        auto temporary=changed;temporary.groups[0].x=96;
        assert(Exchange(dir,{hello.session,first_owner,"preview",Serialize(temporary),1u<<0},nullptr).accepted);assert(state.Boundary()&&state.Current()->layout==temporary&&state.Current()->force_groups);
        // No heartbeat: a crashed/closed Studio must revert its unsaved preview.
        const auto deadline=GetTickCount64()+3500;
        while(GetTickCount64()<deadline&&state.Current()->layout!=changed){Sleep(25);state.Boundary();}
        assert(state.Current()->layout==changed&&!state.Current()->force_groups);
        auto heartbeat=send("heartbeat");assert(heartbeat.accepted&&heartbeat.applied==state.Applied());
        assert(!send("execute","").accepted);
        // Another Studio can connect after lease expiry, while stale sessions
        // and incompatible aspect changes remain refused.
        assert(send("preview",Serialize(temporary),second_owner).accepted);state.Boundary();
        assert(send("cancel","",second_owner).accepted);state.Boundary();
        {
            LiveClient client(dir);client.Connect();
            const auto connection_deadline=GetTickCount64()+2500;
            while(!client.Connected()&&GetTickCount64()<connection_deadline)Sleep(10);
            assert(client.Connected()&&!state.Boundary()&&state.Current()->layout==changed); // handshake must not overwrite the game
            auto edit=changed;
            for(int n=1;n<=64;++n){edit.groups[0].x=100+n;client.Preview(edit);}
            auto wait=[&](auto ready){const auto end=GetTickCount64()+2500;while(!ready()&&GetTickCount64()<end){Sleep(10);state.Boundary();}assert(ready());};
            wait([&]{return state.Current()->layout==edit;});
            client.Apply(edit);wait([&]{return Parse(Read(dir/"hud-layout.json",65536))==edit;});
            auto other=edit;other.groups[0].x=250;client.Preview(other);wait([&]{return state.Current()->layout==other;});
            client.Cancel();wait([&]{return state.Current()->layout==edit;});
            client.RestoreOriginal();wait([&]{return state.Current()->layout.groups==baseline.groups&&!std::filesystem::exists(dir/"hud-layout.json");});
            assert(state.Current()->layout.elements.empty());
            // Cancel after restoring must not revive the previous saved custom.
            assert(send("cancel").accepted);state.Boundary();assert(state.Current()->layout.groups==baseline.groups);
            size_t backups=0;for(const auto& entry:std::filesystem::directory_iterator(dir)) {
                assert(entry.path().filename().wstring().starts_with(L"hud-layout-before-restore-"));
                assert(Parse(Read(entry.path(),65536))==edit);++backups;assert(std::filesystem::remove(entry.path()));
            }
            assert(backups==1);
        }
        // The in-game library uses the actual current-user pipe and the same
        // atomic publication as the Studio. It never edits its source JSONs.
        {
            const auto folder=dir/"hud-presets";assert(std::filesystem::create_directory(folder));
            Save(folder/"a-valid.json",changed);Save(folder/"b-wrong-aspect.json",Base(Aspect::Standard));
            {std::ofstream bad(folder/"c-invalid.json");bad<<"{broken";}
            const auto source=Read(folder/"a-valid.json",65536);
            PresetLibrary library(dir,Aspect::Superwide);
            auto wait=[&] {const auto end=GetTickCount64()+3000;while(library.Snapshot().busy&&GetTickCount64()<end)Sleep(5);assert(!library.Snapshot().busy);};
            library.Refresh();wait();const auto listed=library.Snapshot();
            assert(listed.entries.size()==3&&listed.entries[0].compatible&&!listed.entries[1].compatible&&!listed.entries[2].compatible);
            assert(!state.Boundary());library.Apply(1);wait();assert(!state.Boundary());
            library.Apply(0);wait();assert(library.Snapshot().status==LibraryStatus::Queued);
            assert(state.Current()->layout.groups==baseline.groups); // waits for native frame boundary
            assert(state.Boundary()&&state.Current()->layout==changed);
            assert(Parse(Read(dir/"hud-layout.json",65536))==changed&&Read(folder/"a-valid.json",65536)==source);
            assert(send("preview",Serialize(temporary)).accepted);assert(state.Boundary());
            library.Restore();wait();assert(library.Snapshot().status==LibraryStatus::Failed); // another editor owns the preview
            assert(state.Current()->layout==temporary&&!state.Boundary());
            assert(send("cancel").accepted&&state.Boundary());
            library.Restore();wait();assert(library.Snapshot().status==LibraryStatus::Queued);
            assert(state.Boundary()&&state.Current()->layout.groups==baseline.groups&&!std::filesystem::exists(dir/"hud-layout.json"));
            assert(Read(folder/"a-valid.json",65536)==source);
            for(const auto& entry:std::filesystem::directory_iterator(folder))assert(std::filesystem::remove(entry.path()));
            assert(std::filesystem::remove(folder));
            for(const auto& entry:std::filesystem::directory_iterator(dir)){
                assert(entry.path().filename().wstring().starts_with(L"hud-layout-before-restore-"));assert(std::filesystem::remove(entry.path()));
            }
        }
    }
    // Connecting before the game starts must retry a handshake, not send an
    // uninitialized/default 16:9 layout into a later 32:9 session.
    state.Reset(baseline);
    {
        LiveClient early(dir);early.Connect();Sleep(60);
        LiveServer late(dir,state,baseline);
        const auto end=GetTickCount64()+2500;
        while(!early.Connected()&&GetTickCount64()<end)Sleep(10);
        assert(early.Connected()&&!state.Boundary()&&state.Current()->layout==baseline);
    }
    // Real 16:9 library -> IPC -> frame-boundary publication, followed by a
    // Studio preview and restoration. Source presets never change.
    {
        const auto standard=Base(Aspect::Standard);LiveState standard_state;standard_state.Reset(standard);
        auto edited=standard;edited.name="Custom 16:9";edited.reference_width=1920;edited.reference_height=1080;
        edited.groups[0]={Anchor::Original,37,-23,.75,true};edited.groups[1].x=-160;
        const auto folder=dir/"hud-presets";assert(std::filesystem::create_directory(folder));
        Save(folder/"a-standard.json",edited);Save(folder/"b-wide.json",Base(Aspect::Wide));
        const auto source=Read(folder/"a-standard.json",65536);
        {
            LiveServer standard_server(dir,standard_state,standard);
            PresetLibrary library(dir,Aspect::Standard);
            const auto wait=[&]{const auto end=GetTickCount64()+2500;while(library.Snapshot().busy&&GetTickCount64()<end)Sleep(5);assert(!library.Snapshot().busy);};
            library.Refresh();wait();const auto entries=library.Snapshot().entries;
            assert(entries.size()==2&&entries[0].compatible&&!entries[1].compatible);
            library.Apply(0);wait();assert(library.Snapshot().status==LibraryStatus::Queued);
            assert(standard_state.Current()->layout==standard);
            assert(standard_state.Boundary()&&standard_state.Current()->layout==edited);
            assert(Parse(Read(dir/"hud-layout.json",65536))==edited);
            auto hello=Exchange(dir,{"",first_owner,"hello",""},nullptr);
            assert(hello.accepted&&hello.aspect=="16:9");
            auto preview=edited;preview.groups[1].y=40;
            assert(Exchange(dir,{hello.session,first_owner,"preview",Serialize(preview)},nullptr).accepted);
            assert(standard_state.Boundary()&&standard_state.Current()->layout==preview);
            assert(!Exchange(dir,{hello.session,first_owner,"preview",Serialize(Base(Aspect::Superwide))},nullptr).accepted);
            assert(Exchange(dir,{hello.session,first_owner,"cancel",""},nullptr).accepted);
            assert(standard_state.Boundary()&&standard_state.Current()->layout==edited);
            library.Restore();wait();assert(library.Snapshot().status==LibraryStatus::Queued);
            assert(standard_state.Boundary()&&standard_state.Current()->layout.groups==standard.groups);
            assert(!std::filesystem::exists(dir/"hud-layout.json"));
            assert(Read(folder/"a-standard.json",65536)==source);
        }
        for(const auto& entry:std::filesystem::directory_iterator(folder))assert(std::filesystem::remove(entry.path()));
        assert(std::filesystem::remove(folder));
        for(const auto& entry:std::filesystem::directory_iterator(dir)) {
            assert(entry.path().filename().wstring().starts_with(L"hud-layout-before-restore-"));
            assert(std::filesystem::remove(entry.path()));
        }
    }
    // Startup selection shares the same validation and never alters source
    // JSON. A concurrently running game must refuse offline replacement.
    {
        const auto folder=dir/"hud-presets";assert(std::filesystem::create_directory(folder));
        auto edited=Base(Aspect::Standard);edited.name="Launcher test";edited.groups[0].x=12;
        Save(folder/"a-standard.json",edited);Save(folder/"b-wide.json",Base(Aspect::Wide));
        const auto source=Read(folder/"a-standard.json",65536);
        {
            PresetLibrary library(dir,Aspect::Standard,true);
            const auto wait=[&]{const auto end=GetTickCount64()+2500;while(library.Snapshot().busy&&GetTickCount64()<end)Sleep(5);assert(!library.Snapshot().busy);};
            library.Refresh();wait();assert(library.Snapshot().status==LibraryStatus::Idle);
            library.Apply(1);wait();assert(!std::filesystem::exists(dir/"hud-layout.json"));
            library.Apply(0);wait();assert(library.Snapshot().status==LibraryStatus::Saved);
            assert(Parse(Read(dir/"hud-layout.json",65536))==edited);
            assert(library.Snapshot().active_name==edited.name);
            library.Refresh();wait();assert(library.Snapshot().active_name==edited.name);
            {
                LiveState running;running.Reset(edited);LiveServer server(dir,running,edited);
                library.Restore();wait();assert(library.Snapshot().status==LibraryStatus::Failed);
                assert(Parse(Read(dir/"hud-layout.json",65536))==edited&&!running.Boundary());
            }
            library.Restore();wait();assert(library.Snapshot().status==LibraryStatus::Saved);
            assert(!std::filesystem::exists(dir/"hud-layout.json")&&library.Snapshot().active_name.empty());
            assert(Read(folder/"a-standard.json",65536)==source);
        }
        for(const auto& entry:std::filesystem::directory_iterator(folder))assert(std::filesystem::remove(entry.path()));
        assert(std::filesystem::remove(folder));
        for(const auto& entry:std::filesystem::directory_iterator(dir)) {
            assert(entry.path().filename().wstring().starts_with(L"hud-layout-before-restore-"));
            assert(std::filesystem::remove(entry.path()));
        }
    }
    const auto packet=Encode(LiveRequest{"session",first_owner,"hello",""});assert(DecodeRequest(packet).owner==first_owner);
    Reject([&]{DecodeRequest("{\"protocol\":1,\"protocol\":1,\"session\":\"\",\"owner\":\"x\",\"action\":\"hello\",\"layout\":\"\"}");});
    Reject([&]{DecodeRequest(std::string(65537,'x'));});
    Reject([&]{DecodeReply("{}");});
    Reject([&]{DecodeRequest("{\"protocol\":1,\"session\":\"\",\"owner\":\"x\",\"action\":\"preview\",\"layout\":\"\",\"force_groups\":true}");});
    Reject([&]{DecodeRequest("{\"protocol\":1,\"session\":\"\",\"owner\":\"x\",\"action\":\"preview\",\"layout\":\"\",\"force_groups\":2147483648}");});
    assert(!std::filesystem::exists(dir/"hud-layout.json"));assert(std::filesystem::remove(dir));
    std::cout<<"HUD live: real current-user IPC, immutable frame publication, save/cancel, competing owners, stale/aspect rejection and automatic lease rollback passed\n";
}
