#include <windows.h>
#include "../src/runtime_crowbar_pause.c"
#include <assert.h>
int runtime_crowbar_unlock(int id) { (void)id;return 0; }
int ps4rtgpu_request_exit(void) {return 0;}
static int finished;static unsigned missing;static char state_path[1100];
static void *observer(void *unused) {
    (void)unused;
    while(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE))
        if(GetFileAttributesA(state_path)==INVALID_FILE_ATTRIBUTES)++missing;
    return NULL;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    char temp[MAX_PATH];assert(GetTempPathA(sizeof(temp),temp));
    snprintf(control_dir,sizeof(control_dir),"%scodex-crowbar-state-%lu",temp,(unsigned long)GetCurrentProcessId());
    assert(CreateDirectoryA(control_dir,NULL));snprintf(state_path,sizeof(state_path),"%s/state",control_dir);
    write_state(0,"ready");assert(GetFileAttributesA(state_path)!=INVALID_FILE_ATTRIBUTES);
    pthread_t t;assert(!pthread_create(&t,NULL,observer,NULL));
    for(long i=1;i<=500;++i)write_state(i,i%2?"paused":"running");
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);pthread_join(t,NULL);
    printf("Crowbar atomic state publication: %u missing-file observations\n",missing);
    FILE *in=fopen(state_path,"rb");assert(in);long seq;char word[16];assert(fscanf(in,"%ld %15s",&seq,word)==2);fclose(in);
    assert(seq==500&&!strcmp(word,"running"));assert(DeleteFileA(state_path));assert(RemoveDirectoryA(control_dir));
    assert(missing==0);return 0;
}
