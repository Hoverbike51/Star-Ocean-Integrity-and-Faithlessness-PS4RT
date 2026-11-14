// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud_runtime_model.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <windows.h>

namespace So5Hud {
// Development visibility is a leased live-session setting, never preset data.
struct LiveSnapshot {Preset layout;std::uint64_t revision=0;std::vector<CompiledElement> elements;std::uint64_t force_groups=0;};
class LiveState {
    std::shared_ptr<const LiveSnapshot> current,pending;
    std::atomic<std::uint64_t> generation=0,applied=0;
public:
    void Reset(const Preset&);
    void Queue(const Preset&,std::uint64_t revision,std::uint64_t force_groups=0);
    bool Boundary(); // native game thread: no parsing, disk I/O or pipe operations
    auto Current() const {return std::atomic_load_explicit(&current,std::memory_order_acquire);}
    auto Generation() const {return generation.load(std::memory_order_acquire);}
    auto Applied() const {return applied.load(std::memory_order_acquire);}
};
struct LiveRequest {std::string session,owner,action,layout;std::uint64_t force_groups=0;};
struct LiveReply {std::string session,aspect,message;bool accepted=false;std::uint64_t revision=0,applied=0;};
std::string LiveToken();
std::string Encode(const LiveRequest&);
LiveRequest DecodeRequest(std::string_view);
std::string Encode(const LiveReply&);
LiveReply DecodeReply(std::string_view);
std::wstring LivePipe(const std::filesystem::path& prepared);
LiveReply Exchange(const std::filesystem::path&,const LiveRequest&,HANDLE stop);
// Preserve a separate backup, then remove only the active layout file.
void RemoveInstalledLayout(const std::filesystem::path& prepared);
class LiveServer {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    LiveServer(const std::filesystem::path& prepared,LiveState&,const Preset&,std::function<std::string()> health={});
    ~LiveServer();
    LiveServer(const LiveServer&)=delete;
};
class LiveClient {
    std::filesystem::path prepared;
    std::string owner,session,action;
    Preset latest;
    std::mutex mutex;std::condition_variable wake;
    std::string status="Live preview is off.";
    bool enabled=false,updated=false,closing=false,connected=false,has_preview=false;
    std::uint64_t force_groups=0;
    HANDLE stop{};std::thread worker;
    void Run();
public:
    explicit LiveClient(std::filesystem::path);
    ~LiveClient();
    void Connect(); // handshake/health only; never changes a game layout
    void Preview(const Preset&,std::uint64_t force_groups=0);
    void Apply(const Preset&);
    void Cancel();
    void RestoreOriginal();
    std::string Status();
    bool Connected();
};
}
