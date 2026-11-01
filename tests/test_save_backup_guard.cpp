// Real Windows sharing checks on disposable files, including a newly completed snapshot.
#include "../launcher/save_backup_guard.h"
#include "../launcher/save_preview.h"
#include <cassert>
#include <fstream>

int main() {
    namespace fs=std::filesystem;
    wchar_t temporary[MAX_PATH]{},name[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH,temporary) && GetTempFileNameW(temporary,L"s5g",0,name));
    assert(DeleteFileW(name));
    const fs::path root=name,backups=root/L"savedata-backups";
    const fs::path snapshot=backups/L"1/CUSA03219/SAVE01/backup-existing";
    fs::create_directories(snapshot/L"data");
    const fs::path file=snapshot/L"data/save.bin",live=root/L"active-save.bin";
    std::ofstream(file)<<"snapshot";std::ofstream(live)<<"active";
    {
        SaveBackupGuard guard(backups);
        for(int i=0;i<200 && !guard.Ready();++i) Sleep(10);
        assert(guard.Ready() && guard.ProtectedFiles()>=3);
        HANDLE read=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        assert(read!=INVALID_HANDLE_VALUE);CloseHandle(read);
        assert(CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)==INVALID_HANDLE_VALUE);
        assert(!DeleteFileW(file.c_str()));
        assert(!MoveFileW(snapshot.c_str(),(snapshot.parent_path()/L"renamed").c_str()));
        assert(DeleteFileW(live.c_str()));
        const auto staging=snapshot.parent_path()/L"incomplete-new";
        fs::create_directory(staging);std::ofstream(staging/L"new.bin")<<"new snapshot";
        const auto completed=snapshot.parent_path()/L"backup-new";
        assert(MoveFileW(staging.c_str(),completed.c_str()));
        const size_t before=guard.ProtectedFiles();
        for(int i=0;i<200 && guard.ProtectedFiles()<before+2;++i) Sleep(10);
        const auto fresh=completed/L"new.bin";
        assert(CreateFileW(fresh.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)==INVALID_HANDLE_VALUE);
    }
    HANDLE write=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(write!=INVALID_HANDLE_VALUE);CloseHandle(write);
    // A displayed, decoded save thumbnail must not keep its source PNG locked.
    ULONG_PTR token{};Gdiplus::GdiplusStartupInput input;
    assert(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok);
    {
        UINT count{},bytes{};Gdiplus::GetImageEncodersSize(&count,&bytes);
        std::vector<unsigned char> storage(bytes);
        auto* encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
        assert(Gdiplus::GetImageEncoders(count,bytes,encoders)==Gdiplus::Ok);
        CLSID png{};bool found=false;
        for(UINT i=0;i<count;++i) if(!wcscmp(encoders[i].MimeType,L"image/png")) {png=encoders[i].Clsid;found=true;break;}
        assert(found);
        const fs::path thumbnail=root/L"active-icon.png";
        Gdiplus::Bitmap original(4,4,PixelFormat32bppARGB);
        assert(original.SetPixel(0,0,Gdiplus::Color(255,10,20,30))==Gdiplus::Ok);
        assert(original.Save(thumbnail.c_str(),&png,nullptr)==Gdiplus::Ok);
        auto preview=So5SavePreview::Load(thumbnail);assert(preview && preview->GetWidth()==4);
        assert(DeleteFileW(thumbnail.c_str()));
        assert(original.Save(thumbnail.c_str(),&png,nullptr)==Gdiplus::Ok);
        assert(MoveFileW(thumbnail.c_str(),(root/L"replacement.png").c_str()));
        Gdiplus::Bitmap destination(4,4,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&destination);
        assert(graphics.DrawImage(preview.get(),0,0,4,4)==Gdiplus::Ok);
    }
    Gdiplus::GdiplusShutdown(token);
    fs::remove_all(root); // The exact fixture returned by GetTempFileNameW, never player data.
    std::puts("PASS: snapshots stay readable, reject replacement while guarded, unlock on exit; active saves stay replaceable");
}
