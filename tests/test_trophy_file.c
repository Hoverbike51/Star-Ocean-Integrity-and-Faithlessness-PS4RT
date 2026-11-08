/* Disposable Windows files only. No game data or player saves are opened. */
#include "../src/runtime_trophy_file.h"
#include <assert.h>

static void put(const wchar_t *path,const char *body) {
    FILE *f=_wfopen(path,L"wb"); assert(f);
    assert(fwrite(body,1,strlen(body),f)==strlen(body)); assert(!fclose(f));
}
static size_t read_body(const wchar_t *path,char *body,size_t capacity) {
    FILE *f=_wfopen(path,L"rb"); assert(f);
    size_t n=fread(body,1,capacity-1,f); assert(!ferror(f)); assert(!fclose(f)); body[n]=0; return n;
}
int main(void) {
    char saved[128][21];
    const char *valid=" { \"unlocked\" : { \"001\" : \"2026-11-05T20:00:00Z\", \"127\":\"2026-11-05T21:00:00Z\" } } \n";
    assert(trophy_file_parse(valid,strlen(valid),saved));
    assert(!strcmp(saved[1],"2026-11-05T20:00:00Z") && saved[127][0]);
    const char *compact="{\"unlocked\":{\"001\":\"2026-11-05T20:00:00Z\"}}";
    /* Each truncated prefix must fail, including a buffer with no terminator. */
    for (size_t n=0;n<strlen(compact);++n) assert(!trophy_file_parse(compact,n,saved));
    const char *bad[]={"", "{}", "{\"unlocked\":{},\"other\":1}",
        "{\"unlocked\":{\"128\":\"2026-11-05T20:00:00Z\"}}",
        "{\"unlocked\":{\"001\":\"2026-11-05T20:00:00Z\",\"001\":\"2026-11-05T20:00:00Z\"}}",
        "{\"unlocked\":{\"001\":\"2026-99-05T20:00:00Z\"}}",
        "{\"unlocked\":{\"001\":\"2026-11-05T20:00:00Z \"}}",
        "{\"unlocked\":{\"001 \":\"2026-11-05T20:00:00Z\"}}",
        "{\"unlocked\":{}} trailing"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!trophy_file_parse(bad[i],strlen(bad[i]),saved));

    wchar_t temporary[MAX_PATH],directory[MAX_PATH],path[MAX_PATH+64],lock_path[MAX_PATH+96];
    assert(GetTempPathW(MAX_PATH,temporary));
    assert(GetTempFileNameW(temporary,L"s5t",0,directory));
    assert(DeleteFileW(directory) && CreateDirectoryW(directory,NULL));
    swprintf(path,MAX_PATH+64,L"%ls\\trophies-\x65e5\x672c.json",directory);
    assert(SetEnvironmentVariableW(L"PS4RT_TROPHY_FILE",NULL));
    assert(trophy_file_note(1)==TROPHY_FILE_SKIPPED);
    assert(SetEnvironmentVariableW(L"PS4RT_TROPHY_FILE",path));
    assert(trophy_file_note(-1)==TROPHY_FILE_SKIPPED && trophy_file_note(128)==TROPHY_FILE_SKIPPED);
    assert(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    put(path,valid);
    assert(trophy_file_note(2)==TROPHY_FILE_SAVED);
    char before[16385],after[16385]; size_t n=read_body(path,before,sizeof(before));
    assert(trophy_file_parse(before,n,saved) && saved[2][0]);
    assert(!strcmp(saved[1],"2026-11-05T20:00:00Z"));
    assert(trophy_file_note(1)==TROPHY_FILE_UNCHANGED);
    assert(read_body(path,after,sizeof(after))==n && !memcmp(before,after,n));
    for (int id=0;id<128;++id) {
        enum TrophyFileResult r=trophy_file_note(id);
        assert(r==TROPHY_FILE_SAVED || r==TROPHY_FILE_UNCHANGED);
    }
    n=read_body(path,before,sizeof(before)); assert(trophy_file_parse(before,n,saved));
    for (int id=0;id<128;++id) assert(saved[id][0]);
    assert(DeleteFileW(path));
    assert(trophy_file_note(7)==TROPHY_FILE_SAVED); /* New journal. */
    put(path,"{\"unlocked\":{\"001\":\"truncated");
    n=read_body(path,before,sizeof(before));
    assert(trophy_file_note(8)==TROPHY_FILE_INVALID);
    assert(read_body(path,after,sizeof(after))==n && !memcmp(before,after,n));
    put(path,"{\"unlocked\":{}}");
    swprintf(lock_path,MAX_PATH+96,L"%ls.so5-trophy.lock",path);
    HANDLE lock=CreateFileW(lock_path,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,OPEN_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL); assert(lock!=INVALID_HANDLE_VALUE);
    assert(trophy_file_note(9)==TROPHY_FILE_BUSY); assert(CloseHandle(lock));
    HANDLE held=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(held!=INVALID_HANDLE_VALUE);
    assert(trophy_file_note(9)==TROPHY_FILE_IO_ERROR); /* Atomic replace denied; keep original. */
    assert(CloseHandle(held));
    read_body(path,after,sizeof(after)); assert(!strcmp(after,"{\"unlocked\":{}}"));
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY));
    assert(trophy_file_note(9)==TROPHY_FILE_IO_ERROR);
    assert(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL));
    read_body(path,after,sizeof(after)); assert(!strcmp(after,"{\"unlocked\":{}}"));
    assert(DeleteFileW(path));
    /* All temporary and lock files must have been removed. */
    assert(RemoveDirectoryW(directory));
    assert(SetEnvironmentVariableW(L"PS4RT_TROPHY_FILE",NULL));
    puts("PASS: journal schema, truncation, UTC first-unlock preservation, all IDs, Unicode paths, locks and atomic failure");
    return 0;
}
