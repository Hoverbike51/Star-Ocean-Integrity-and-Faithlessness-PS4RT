// SPDX-License-Identifier: GPL-2.0-or-later
#include "hud_live.h"
#include <boost/json.hpp>
#include <sddl.h>
#include <objbase.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace So5Hud {
namespace {
constexpr DWORD MaxPacket=2097152,Timeout=750;
constexpr ULONGLONG Lease=2000;
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
[[noreturn]] void Fail(const char* m){throw std::runtime_error(m);}
std::wstring Sid() {
    Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.h))Fail("Cannot obtain the current Windows identity.");
    DWORD size=0;GetTokenInformation(token.h,TokenUser,nullptr,0,&size);std::vector<unsigned char> data(size);
    if(!size||!GetTokenInformation(token.h,TokenUser,data.data(),size,&size))Fail("Cannot read the current Windows identity.");
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))Fail("Cannot identify the current Windows user.");
    std::wstring out=sid;LocalFree(sid);return out;
}
bool Complete(HANDLE pipe,OVERLAPPED& ov,HANDLE stop,DWORD& bytes,DWORD timeout=Timeout) {
    HANDLE events[]={ov.hEvent,stop};const DWORD result=WaitForMultipleObjects(stop?2:1,events,FALSE,timeout);
    if(result!=WAIT_OBJECT_0){CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&bytes,TRUE);return false;}
    return GetOverlappedResult(pipe,&ov,&bytes,FALSE)!=FALSE;
}
bool Transfer(HANDLE pipe,void* data,DWORD& bytes,bool write,HANDLE stop) {
    Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};if(!event.h)return false;
    OVERLAPPED ov{};ov.hEvent=event.h;DWORD done=0;
    const bool now=(write?WriteFile(pipe,data,bytes,&done,&ov):ReadFile(pipe,data,bytes,&done,&ov))!=FALSE;
    if(!now&&(GetLastError()!=ERROR_IO_PENDING||!Complete(pipe,ov,stop,done)))return false;
    bytes=done;return true;
}
std::string ReadPacket(HANDLE pipe,HANDLE stop) {
    std::string data(MaxPacket+1,'\0');DWORD size=DWORD(data.size());
    if(!Transfer(pipe,data.data(),size,false,stop)||!size||size>MaxPacket)Fail("Live channel read failed or packet limit exceeded.");
    data.resize(size);return data;
}
void WritePacket(HANDLE pipe,std::string_view data,HANDLE stop) {
    if(data.empty()||data.size()>MaxPacket)Fail("Live packet limit exceeded.");
    DWORD size=DWORD(data.size());if(!Transfer(pipe,const_cast<char*>(data.data()),size,true,stop)||size!=data.size())Fail("Live channel write failed.");
}
namespace j=boost::json;
j::object MessageObject(std::string_view text) {
    CheckJson(text,MaxPacket);auto v=j::parse(text);if(!v.is_object())Fail("Invalid live message.");return std::move(v.as_object());
}
void Keys(const j::object& o,std::initializer_list<std::string_view> keys) {
    if(o.size()!=keys.size())Fail("Unexpected live message fields.");
    for(auto k:keys)if(!o.contains(k))Fail("Incomplete live message.");
    if(!o.at("protocol").is_int64()||o.at("protocol").as_int64()!=1)Fail("Unsupported live protocol.");
}
std::string Text(const j::object& o,std::string_view key,size_t limit) {
    const auto& v=o.at(key);if(!v.is_string()||v.as_string().size()>limit)Fail("Invalid live text field.");return std::string(v.as_string());
}
std::uint64_t Number(const j::object& o,std::string_view key) {
    const auto& v=o.at(key);if(v.is_uint64())return v.as_uint64();
    if(v.is_int64()&&v.as_int64()>=0)return std::uint64_t(v.as_int64());Fail("Invalid live sequence.");
}
}
void LiveState::Reset(const Preset& p) {
    Validate(p);std::atomic_store(&pending,std::shared_ptr<const LiveSnapshot>{});
    std::atomic_store(&current,std::make_shared<const LiveSnapshot>(LiveSnapshot{p,0,CompileElements(p)}));applied=0;++generation;
}
void LiveState::Queue(const Preset& p,std::uint64_t rev,std::uint32_t force) {
    std::atomic_store_explicit(&pending,std::make_shared<const LiveSnapshot>(LiveSnapshot{p,rev,CompileElements(p),force}),std::memory_order_release);
}
bool LiveState::Boundary() {
    auto next=std::atomic_exchange_explicit(&pending,std::shared_ptr<const LiveSnapshot>{},std::memory_order_acq_rel);if(!next)return false;
    const auto revision=next->revision;std::atomic_store_explicit(&current,std::move(next),std::memory_order_release);
    applied.store(revision,std::memory_order_release);generation.fetch_add(1,std::memory_order_release);return true;
}
std::string LiveToken() {
    GUID id{};if(FAILED(CoCreateGuid(&id)))Fail("Cannot create a live session token.");
    wchar_t wide[40]{};StringFromGUID2(id,wide,40);std::string result;for(auto* c=wide;*c;++c)result+=char(*c);return result;
}
std::string Encode(const LiveRequest& r) {
    j::object o{{"protocol",1},{"session",r.session},{"owner",r.owner},{"action",r.action},{"layout",r.layout}};
    if(r.force_groups)o["force_groups"]=r.force_groups;return j::serialize(o);
}
LiveRequest DecodeRequest(std::string_view text) {
    auto o=MessageObject(text);std::uint32_t force=0;
    if(o.contains("force_groups")){const auto n=Number(o,"force_groups");
        if(n>=(1u<<GroupCount))Fail("Invalid development HUD mask.");force=std::uint32_t(n);o.erase("force_groups");}
    Keys(o,{"protocol","session","owner","action","layout"});
    LiveRequest r{Text(o,"session",64),Text(o,"owner",64),Text(o,"action",16),Text(o,"layout",1048576)};
    r.force_groups=force;
    if(force&&r.action!="preview")Fail("Development visibility is available only in live preview.");
    for(size_t n=0;n<GroupCount;++n)if((force&(1u<<n))&&!RuntimeSupported(n))Fail("Unbound HUD cannot be forced visible.");
    if(r.owner.empty()||r.action.empty())Fail("Missing live owner/action.");return r;
}
std::string Encode(const LiveReply& r) {
    return j::serialize(j::object{{"protocol",1},{"session",r.session},{"aspect",r.aspect},{"message",r.message},{"accepted",r.accepted},{"revision",r.revision},{"applied",r.applied}});
}
LiveReply DecodeReply(std::string_view text) {
    auto o=MessageObject(text);Keys(o,{"protocol","session","aspect","message","accepted","revision","applied"});
    if(!o.at("accepted").is_bool())Fail("Invalid live result.");
    return {Text(o,"session",64),Text(o,"aspect",8),Text(o,"message",512),o.at("accepted").as_bool(),Number(o,"revision"),Number(o,"applied")};
}
std::wstring LivePipe(const std::filesystem::path& prepared) {
    const auto canonical=std::filesystem::weakly_canonical(prepared.empty()?std::filesystem::current_path():prepared);
    auto path=canonical.wstring();
    while(path.size()>canonical.root_path().wstring().size()&&(path.back()==L'\\'||path.back()==L'/'))path.pop_back();
    std::transform(path.begin(),path.end(),path.begin(),towlower);path+=Sid();
    std::uint64_t hash=14695981039346656037ull;for(wchar_t c:path){hash=(hash^(c&255))*1099511628211ull;hash=(hash^(c>>8))*1099511628211ull;}
    wchar_t suffix[32];swprintf(suffix,32,L"%016llx",static_cast<unsigned long long>(hash));
    return std::wstring(L"\\\\.\\pipe\\HoverModsVault.SO5.HUD.")+suffix;
}
LiveReply Exchange(const std::filesystem::path& prepared,const LiveRequest& req,HANDLE stop) {
    const auto name=LivePipe(prepared);
    Handle pipe{CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr)};
    if(pipe.h==INVALID_HANDLE_VALUE) {
        if(GetLastError()==ERROR_PIPE_BUSY&&WaitNamedPipeW(name.c_str(),200))
            pipe.h=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(pipe.h==INVALID_HANDLE_VALUE)Fail("No live game connection. Use in game, then start the updated runtime.");
    }
    DWORD mode=PIPE_READMODE_MESSAGE;if(!SetNamedPipeHandleState(pipe.h,&mode,nullptr,nullptr))Fail("Cannot configure the live channel.");
    WritePacket(pipe.h,Encode(req),stop);auto response=DecodeReply(ReadPacket(pipe.h,stop));
    unsigned char ack=1;DWORD bytes=1;Transfer(pipe.h,&ack,bytes,true,stop);return response;
}
void RemoveInstalledLayout(const std::filesystem::path& prepared) {
    const auto file=prepared/L"hud-layout.json";
    if(!std::filesystem::exists(file))return;
    const auto backup=prepared/(L"hud-layout-before-restore-"+std::filesystem::path(LiveToken()).wstring()+L".json");
    if(!std::filesystem::copy_file(file,backup))Fail("Cannot back up the active HUD preset; restoration was cancelled.");
    if(!std::filesystem::remove(file))Fail("Cannot disable the active HUD preset.");
}
struct LiveServer::Impl {
    std::filesystem::path prepared;LiveState& state;Preset baseline;std::string session=LiveToken(),owner;
    std::uint64_t revision=0;ULONGLONG deadline=0;Handle stop{CreateEventW(nullptr,TRUE,FALSE,nullptr)},pipe;
    PSECURITY_DESCRIPTOR security=nullptr;std::thread worker;std::function<std::string()> health;
    Impl(const std::filesystem::path& p,LiveState& s,const Preset& base,std::function<std::string()> report):prepared(p),state(s),baseline(base),health(std::move(report)) {
        const auto acl=L"D:P(A;;GA;;;"+Sid()+L")";
        if(!stop.h||!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&security,nullptr))Fail("Cannot secure the live channel.");
        SECURITY_ATTRIBUTES attributes{sizeof attributes,security,FALSE};
        pipe.h=CreateNamedPipeW(LivePipe(prepared).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,MaxPacket+1,MaxPacket+1,0,&attributes);
        if(pipe.h==INVALID_HANDLE_VALUE){LocalFree(security);security=nullptr;Fail("A live HUD channel is already running or unavailable.");}
        worker=std::thread([this]{Run();});
    }
    ~Impl(){SetEvent(stop.h);CancelIoEx(pipe.h,nullptr);if(worker.joinable())worker.join();if(security)LocalFree(security);}
    void Expire() {if(!owner.empty()&&GetTickCount64()>deadline){state.Queue(baseline,++revision);owner.clear();}}
    LiveReply Process(const LiveRequest& r) {
        LiveReply reply{session,std::string(AspectName(baseline.aspect)),"Connected.",true,revision,state.Applied()};
        try {
            if(r.action=="hello") {if(!r.session.empty()||!r.layout.empty())Fail("Invalid live handshake.");if(health)reply.message=health();return reply;}
            if(r.session!=session)Fail("The game session changed. Reconnect live preview.");
            if(!owner.empty()&&r.owner!=owner)Fail("Another Studio owns the current preview.");
            if(r.action=="heartbeat") {if(!r.layout.empty())Fail("Invalid heartbeat.");if(!owner.empty())deadline=GetTickCount64()+Lease;reply.message=state.Applied()==revision?"Live snapshot applied.":"Waiting for the next game frame.";if(health)reply.message+=" "+health();}
            else if(r.action=="cancel") {
                if(!r.layout.empty())Fail("Invalid cancellation.");state.Queue(baseline,++revision);owner.clear();reply.message="Restoring the applied layout.";
            }else if(r.action=="restore") {
                if(!r.layout.empty())Fail("Invalid original-layout restoration.");
                auto original=Base(baseline.aspect);original.reference_width=baseline.reference_width;original.reference_height=baseline.reference_height;
                RemoveInstalledLayout(prepared);baseline=std::move(original);
                state.Queue(baseline,++revision);owner.clear();
                reply.message="Original 16:9 HUD restored; custom preset disabled. The previous preset was backed up.";
            }else if(r.action=="preview"||r.action=="apply") {
                auto p=Parse(r.layout);if(!RuntimeCompatible(p,baseline.aspect))Fail("Aspect mismatch or unbound HUD changes. Changing aspect requires a game restart.");
                if(r.action=="apply") {Save(prepared/L"hud-layout.json",p);baseline=p;owner.clear();reply.message="Layout saved; applying on the next game frame.";}
                else {owner=r.owner;deadline=GetTickCount64()+Lease;reply.message="Preview queued for the next game frame.";}
                state.Queue(p,++revision,r.force_groups);
                if(r.force_groups)reply.message+=" Holding instantiated HUD visible (development only).";
            }else Fail("Unsupported live action.");
            reply.revision=revision;reply.applied=state.Applied();
        }catch(const std::exception& e){reply.accepted=false;reply.message=e.what();}
        return reply;
    }
    void Run() {
        while(WaitForSingleObject(stop.h,0)!=WAIT_OBJECT_0) {
            Expire();Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};OVERLAPPED ov{};ov.hEvent=event.h;
            bool connected=ConnectNamedPipe(pipe.h,&ov)!=FALSE;
            if(!connected) {const auto error=GetLastError();connected=error==ERROR_PIPE_CONNECTED;
                if(error==ERROR_IO_PENDING){DWORD transferred=0;connected=Complete(pipe.h,ov,stop.h,transferred,250);}}
            if(!connected){DisconnectNamedPipe(pipe.h);continue;}
            try {
                const auto request=DecodeRequest(ReadPacket(pipe.h,stop.h));const auto response=Process(request);
                WritePacket(pipe.h,Encode(response),stop.h);
                unsigned char ack=0;DWORD bytes=1;Transfer(pipe.h,&ack,bytes,false,stop.h);
            }catch(const std::exception&){} // malformed/expired clients cannot change the current snapshot
            DisconnectNamedPipe(pipe.h);
        }
        if(!owner.empty())state.Queue(baseline,++revision);
    }
};
LiveServer::LiveServer(const std::filesystem::path& p,LiveState& s,const Preset& base,std::function<std::string()> report):impl(std::make_unique<Impl>(p,s,base,std::move(report))){}
LiveServer::~LiveServer()=default;
LiveClient::LiveClient(std::filesystem::path p):prepared(std::move(p)),owner(LiveToken()),stop(CreateEventW(nullptr,TRUE,FALSE,nullptr)) {
    if(!stop)Fail("Cannot create the live worker.");worker=std::thread([this]{Run();});
}
LiveClient::~LiveClient(){ {std::scoped_lock lock(mutex);closing=true;}SetEvent(stop);wake.notify_all();if(worker.joinable())worker.join();CloseHandle(stop);}
void LiveClient::Connect() {{std::scoped_lock lock(mutex);enabled=true;updated=true;action="heartbeat";}wake.notify_one();}
void LiveClient::Preview(const Preset& p,std::uint32_t force) {Validate(p);{std::scoped_lock lock(mutex);latest=p;force_groups=force;has_preview=true;enabled=true;updated=true;action="preview";}wake.notify_one();}
void LiveClient::Apply(const Preset& p) {Validate(p);{std::scoped_lock lock(mutex);latest=p;force_groups=0;has_preview=true;enabled=true;updated=true;action="apply";}wake.notify_one();}
void LiveClient::Cancel() {{std::scoped_lock lock(mutex);enabled=false;has_preview=false;force_groups=0;updated=true;action="cancel";}wake.notify_one();}
void LiveClient::RestoreOriginal() {{std::scoped_lock lock(mutex);enabled=false;has_preview=false;force_groups=0;updated=true;action="restore";}wake.notify_one();}
std::string LiveClient::Status(){std::scoped_lock lock(mutex);return status;}
bool LiveClient::Connected(){std::scoped_lock lock(mutex);return connected;}
void LiveClient::Run() {
    for(;;) {
        Preset p;std::string operation;std::uint32_t force=0;bool retry=false;
        {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(500),[&]{return updated||closing;});
            if(closing)return;if(!updated&&!enabled)continue;p=latest;operation=updated?action:"heartbeat";force=force_groups;updated=false;}
        try {
            if(session.empty()) {
                auto hello=Exchange(prepared,{"",owner,"hello",""},stop);session=hello.session;
            }
            auto reply=Exchange(prepared,{session,owner,operation,(operation=="preview"||operation=="apply")?Serialize(p):"",operation=="preview"?force:0},stop);
            {std::scoped_lock lock(mutex);status=reply.message;connected=reply.accepted;if(!reply.accepted){enabled=false;session.clear();}}
        }catch(const std::exception& e){
            std::scoped_lock lock(mutex);status=e.what();session.clear();connected=false;retry=true;
            // Reconnect with a preview, never replay an unacknowledged disk save.
            if(enabled&&!updated){updated=true;action=has_preview?"preview":"heartbeat";}
        }
        // Coalesce rapid drag/edit updates; never block the editor or game UI thread.
        if(WaitForSingleObject(stop,retry?500:25)==WAIT_OBJECT_0)return;
    }
}
}
