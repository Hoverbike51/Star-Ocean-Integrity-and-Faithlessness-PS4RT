// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud_preset_library.h"
#include "hud_live.h"
#include "hud_runtime_model.h"
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>

namespace So5Hud {
struct PresetLibrary::Impl {
    std::filesystem::path prepared;Aspect aspect;bool offline;
    mutable std::mutex mutex;LibrarySnapshot state;
    std::vector<std::optional<Preset>> layouts;
    std::future<void> worker; // destroyed first: waits before referenced state dies
    Impl(std::filesystem::path path,Aspect a,bool o):prepared(std::move(path)),aspect(a),offline(o){}
    void Start(LibraryStatus status,std::function<void()> task) {
        std::scoped_lock lock(mutex);if(state.busy)return;
        state.busy=true;state.status=status;
        worker=std::async(std::launch::async,[this,task=std::move(task)] {
            LibraryStatus result=LibraryStatus::Idle;
            try {task();result=offline?LibraryStatus::Saved:LibraryStatus::Queued;}
            catch(const std::exception& e){std::printf("HUD preset: %s\n",e.what());result=LibraryStatus::Failed;}
            std::scoped_lock done(mutex);
            state.status=state.status==LibraryStatus::Scanning&&result!=LibraryStatus::Failed?LibraryStatus::Idle:result;
            state.busy=false;
        });
    }
    void Send(std::string action,std::string layout={}) {
        if(offline) {
            // Never replace a live session's layout, including games started
            // independently of the launcher. File operations stay off the UI thread.
            if(WaitNamedPipeW(LivePipe(prepared).c_str(),1)||GetLastError()!=ERROR_FILE_NOT_FOUND)
                throw std::runtime_error("close the game before changing its startup HUD layout");
            if(action=="restore")RemoveInstalledLayout(prepared);
            else if(action=="apply")Save(prepared/L"hud-layout.json",Parse(layout));
            else throw std::runtime_error("unsupported offline HUD action");
            return;
        }
        const auto owner=LiveToken();const auto hello=Exchange(prepared,{"",owner,"hello",""},nullptr);
        if(!hello.accepted||hello.aspect!=AspectName(aspect))throw std::runtime_error("game session or aspect mismatch");
        const auto result=Exchange(prepared,{hello.session,owner,std::move(action),std::move(layout)},nullptr);
        if(!result.accepted)throw std::runtime_error(result.message);
        std::printf("HUD preset: selection queued for frame boundary, revision %llu\n",static_cast<unsigned long long>(result.revision));
    }
};
PresetLibrary::PresetLibrary(std::filesystem::path p,Aspect a,bool offline):impl(std::make_unique<Impl>(std::move(p),a,offline)){}
PresetLibrary::~PresetLibrary()=default;
LibrarySnapshot PresetLibrary::Snapshot() const {std::scoped_lock lock(impl->mutex);return impl->state;}
void PresetLibrary::Refresh() {
    impl->Start(LibraryStatus::Scanning,[p=impl.get()] {
        const auto folder=p->prepared/L"hud-presets";std::filesystem::create_directories(folder);
        std::vector<std::filesystem::path> files;
        size_t inspected=0;
        for(const auto& entry:std::filesystem::directory_iterator(folder)) {
            if(++inspected>1024)break;
            const auto status=entry.symlink_status();
            if(!std::filesystem::is_regular_file(status))continue;
            auto extension=entry.path().extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
            if(extension==L".json"&&files.size()<64)files.push_back(entry.path());
        }
        std::ranges::sort(files);
        std::vector<LibraryEntry> entries;std::vector<std::optional<Preset>> layouts;
        for(const auto& file:files) {
            try {
                auto layout=Parse(Read(file,1048576));const bool compatible=RuntimeCompatible(layout,p->aspect);
                entries.push_back({layout.name,compatible});layouts.push_back(compatible?std::optional(std::move(layout)):std::nullopt);
            }catch(const std::exception& e) {
                const auto name=file.stem().u8string();entries.push_back({std::string(name.begin(),name.end()),false});layouts.push_back(std::nullopt);
                std::printf("HUD preset: ignored invalid file (%s)\n",e.what());
            }
        }
        std::string active;
        if(p->offline&&std::filesystem::is_regular_file(p->prepared/L"hud-layout.json")) {
            try {const auto installed=Parse(Read(p->prepared/L"hud-layout.json",1048576));
                if(RuntimeCompatible(installed,p->aspect))active=installed.name;
            }catch(const std::exception&){} // Keep an invalid existing file intact.
        }
        std::scoped_lock lock(p->mutex);p->state.entries=std::move(entries);p->layouts=std::move(layouts);
        if(p->offline)p->state.active_name=std::move(active);
    });
}
void PresetLibrary::Apply(size_t index) {
    std::optional<Preset> selected;
    {std::scoped_lock lock(impl->mutex);if(impl->state.busy||index>=impl->layouts.size()||!impl->layouts[index])return;selected=impl->layouts[index];}
    impl->Start(LibraryStatus::Applying,[p=impl.get(),selected=std::move(*selected)] {
        if(!RuntimeCompatible(selected,p->aspect))throw std::runtime_error("incompatible HUD layout");
        p->Send("apply",Serialize(selected));
        std::scoped_lock lock(p->mutex);p->state.active_name=selected.name;
    });
}
void PresetLibrary::Restore(){impl->Start(LibraryStatus::Applying,[p=impl.get()]{p->Send("restore");std::scoped_lock lock(p->mutex);p->state.active_name.clear();});}
}
