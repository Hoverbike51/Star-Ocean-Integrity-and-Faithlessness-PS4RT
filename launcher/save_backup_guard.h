// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <filesystem>
#include <atomic>
#include <thread>
#include <unordered_set>
#include <vector>

// Temporary, process-lifetime protection of completed snapshots. No ACL or file
// attributes are changed. Reads and new snapshots remain possible; active saves
// and incomplete staging directories are never held open by this guard.
class SaveBackupGuard {
    std::filesystem::path root;
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread worker;
    std::atomic<size_t> protected_files{0};
    std::atomic<bool> ready{false};
    void Run() {
        std::vector<HANDLE> handles;
        std::unordered_set<std::wstring> locked;
        auto protect=[&](const std::filesystem::path& path) {
            const auto key=path.wstring();
            if(locked.contains(key)) return;
            const DWORD attributes=GetFileAttributesW(path.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || attributes&FILE_ATTRIBUTE_REPARSE_POINT) return;
            const bool directory=attributes&FILE_ATTRIBUTE_DIRECTORY;
            HANDLE handle=CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,nullptr,
                OPEN_EXISTING,directory?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL,nullptr);
            if(handle!=INVALID_HANDLE_VALUE) {handles.push_back(handle);locked.insert(key);protected_files=handles.size();}
        };
        auto scan=[&] {
            std::error_code ec;
            for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end && !ec;it.increment(ec)) {
                if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0) break;
                const auto path=it->path();
                const DWORD attributes=GetFileAttributesW(path.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES) continue;
                if(attributes&FILE_ATTRIBUTE_REPARSE_POINT) {it.disable_recursion_pending();continue;}
                if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
                const auto name=path.filename().wstring();
                if(name.starts_with(L"incomplete-")) {it.disable_recursion_pending();continue;}
                if(!name.starts_with(L"backup-")) continue;
                protect(path);
                std::error_code sub_ec;
                for(std::filesystem::recursive_directory_iterator sub(path,std::filesystem::directory_options::skip_permission_denied,sub_ec),last;sub!=last && !sub_ec;sub.increment(sub_ec)) {
                    if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0) break;
                    const auto child=sub->path();const auto flags=GetFileAttributesW(child.c_str());
                    if(flags!=INVALID_FILE_ATTRIBUTES && flags&FILE_ATTRIBUTE_REPARSE_POINT) {sub.disable_recursion_pending();continue;}
                    protect(child);
                }
                it.disable_recursion_pending();
            }
        };
        HANDLE change=FindFirstChangeNotificationW(root.c_str(),TRUE,FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_FILE_NAME);
        scan();ready=true;
        const HANDLE events[]={stop,change};
        while(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0) {
            const DWORD result=change==INVALID_HANDLE_VALUE?WaitForSingleObject(stop,1000):WaitForMultipleObjects(2,events,FALSE,INFINITE);
            if(result==WAIT_OBJECT_0) break;
            if(change!=INVALID_HANDLE_VALUE && !FindNextChangeNotification(change)) {FindCloseChangeNotification(change);change=INVALID_HANDLE_VALUE;}
            scan();
        }
        if(change!=INVALID_HANDLE_VALUE) FindCloseChangeNotification(change);
        for(HANDLE handle:handles) CloseHandle(handle);
        protected_files=0;
    }
public:
    explicit SaveBackupGuard(std::filesystem::path directory):root(std::move(directory)) {
        const auto attributes=GetFileAttributesW(root.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && attributes&FILE_ATTRIBUTE_REPARSE_POINT) return;
        std::error_code ec;std::filesystem::create_directories(root,ec);
        if(stop && !ec) worker=std::thread([this]{Run();});
    }
    SaveBackupGuard(const SaveBackupGuard&)=delete;
    ~SaveBackupGuard() {if(stop) SetEvent(stop);if(worker.joinable()) worker.join();if(stop) CloseHandle(stop);}
    bool Ready() const {return ready;}
    size_t ProtectedFiles() const {return protected_files;}
};
