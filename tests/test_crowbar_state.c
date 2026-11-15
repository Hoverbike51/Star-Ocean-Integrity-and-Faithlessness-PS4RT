#include <windows.h>
#include <assert.h>

/* Exercise a real Windows sharing failure, releasing the handle on the first
 * failed replacement so the retry check is independent of thread timing. */
static HANDLE release_on_failure = INVALID_HANDLE_VALUE;
static unsigned move_attempts;
static BOOL observed_move(LPCSTR from, LPCSTR to, DWORD flags) {
    ++move_attempts;
    const BOOL result = MoveFileExA(from, to, flags);
    if (!result && release_on_failure != INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        assert(CloseHandle(release_on_failure));
        release_on_failure = INVALID_HANDLE_VALUE;
        SetLastError(error);
    }
    return result;
}
#define MoveFileExA observed_move
#include "../src/runtime_crowbar_pause.c"
#undef MoveFileExA

int runtime_crowbar_unlock(int id) { (void)id;return 0; }
int ps4rtgpu_request_exit(void) {return 0;}
static int finished;
static unsigned missing;
static char state_path[1100];
static void *observer(void *unused) {
    (void)unused;
    while(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE))
        if(GetFileAttributesA(state_path)==INVALID_FILE_ATTRIBUTES)++missing;
    return NULL;
}
static void expect_state(long expected, const char *expected_word) {
    FILE *in=fopen(state_path,"rb");assert(in);
    long seq=-1;char word[16]={0};
    assert(fscanf(in,"%ld %15s",&seq,word)==2);assert(fclose(in)==0);
    if(seq!=expected || strcmp(word,expected_word))
        fprintf(stderr,"Crowbar state: expected %ld %s, got %ld %s\n",expected,expected_word,seq,word);
    assert(seq==expected&&!strcmp(word,expected_word));
}
static HANDLE hold_state(void) {
    HANDLE handle=CreateFileA(state_path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(handle!=INVALID_HANDLE_VALUE);return handle;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    char temp[MAX_PATH],directory[MAX_PATH];assert(GetTempPathA(sizeof(temp),temp));
    assert(GetTempFileNameA(temp,"cbr",0,directory));assert(DeleteFileA(directory));
    assert(CreateDirectoryA(directory,NULL));
    snprintf(control_dir,sizeof(control_dir),"%s",directory);
    snprintf(state_path,sizeof(state_path),"%s/state",control_dir);
    assert(write_state(0,"ready"));expect_state(0,"ready");
    pthread_t t;assert(!pthread_create(&t,NULL,observer,NULL));
    for(long i=1;i<=500;++i)assert(write_state(i,i%2?"paused":"running"));
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);pthread_join(t,NULL);
    printf("Crowbar atomic state publication: %u missing-file observations\n",missing);
    expect_state(500,"running");assert(missing==0);

    release_on_failure=hold_state();move_attempts=0;
    assert(write_state(501,"running"));assert(move_attempts>=2);
    assert(release_on_failure==INVALID_HANDLE_VALUE);expect_state(501,"running");
    puts("Crowbar state: transient sharing failure retried PASS");

    HANDLE held=hold_state();
    assert(!write_state(502,"paused"));assert(state_reply.pending);expect_state(501,"running");
    assert(CloseHandle(held));assert(flush_state_reply());assert(!state_reply.pending);
    expect_state(502,"paused");
    puts("Crowbar state: unpublished reply retained and flushed after unlock PASS");

    char tmp[1110];snprintf(tmp,sizeof(tmp),"%s.tmp",state_path);
    held=CreateFileA(tmp,GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(held!=INVALID_HANDLE_VALUE);
    assert(!write_state(503,"running"));assert(state_reply.pending);expect_state(502,"paused");
    assert(CloseHandle(held));assert(flush_state_reply());expect_state(503,"running");
    puts("Crowbar state: temporary-file I/O failure retains reply PASS");

    held=hold_state();
    assert(!write_state(504,"paused"));assert(!write_state(505,"running"));
    expect_state(503,"running");assert(CloseHandle(held));assert(flush_state_reply());
    expect_state(505,"running");
    puts("Crowbar state: newest pending response supersedes older response PASS");

    assert(DeleteFileA(state_path));assert(RemoveDirectoryA(control_dir));
    return 0;
}
