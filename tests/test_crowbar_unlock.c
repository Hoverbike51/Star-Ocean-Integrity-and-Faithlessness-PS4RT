#include "../src/runtime_services.c"
#include <assert.h>

/* Unrelated guest services must never run in this adapter-only regression. */
uintptr_t runtime_lookup(const RuntimeExport *t,size_t n,const char *s){abort();}
int64_t runtime_file_open(const char *p,int f,int m){abort();}
int64_t runtime_file_read(int f,void *p,uint64_t n){abort();}
int64_t runtime_file_close(int f){abort();}
long compat_utc_offset(time_t t){abort();}
void compat_sleep_ns(uint64_t n){abort();}
int runtime_savedata_icon(int32_t u,const char *d,char *p,size_t n){abort();}
int runtime_savedata_param(int32_t u,const char *d,void *p){abort();}
int ps4rtgpu_choice_begin(const char *t,const char *const *i,const char *const *im,int n,int f){abort();}
int ps4rtgpu_choice_poll(void){abort();}
int ps4rtgpu_text_input_begin(const char *t,const char *p){abort();}
int ps4rtgpu_text_input_poll(char *p,uint64_t n){abort();}

int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    char temp[MAX_PATH],path[MAX_PATH];assert(GetTempPathA(sizeof(temp),temp));
    assert(GetTempFileNameA(temp,"cbr",0,path));assert(_putenv_s("PS4RT_TROPHY_FILE",path)==0);
    FILE *out=fopen(path,"wb");assert(out);fputs("malformed-json\n",out);fclose(out);
    int actual=runtime_crowbar_unlock(1);
    printf("Crowbar unlock with malformed journal: %d (expected refusal)\n",actual);
    assert(actual!=0);
    // The game's guest-facing success contract must remain unchanged.
    int32_t platinum=17;assert(trophy_unlock(0,0,1,&platinum)==0&&platinum==-1);
    assert(DeleteFileA(path));
    assert(runtime_crowbar_unlock(1)==0);assert(runtime_crowbar_unlock(1)==0);
    FILE *in=fopen(path,"rb");assert(in);char body[512]={0};fread(body,1,sizeof(body)-1,in);fclose(in);
    assert(strstr(body,"\"001\""));assert(DeleteFileA(path));
    assert(_putenv_s("PS4RT_TROPHY_FILE","")==0);assert(runtime_crowbar_unlock(1)!=0);
    assert(runtime_crowbar_unlock(-1)!=0&&runtime_crowbar_unlock(128)!=0);
    puts("Crowbar trophy acknowledgement: persistence, duplicate and unchanged guest API PASS");
    return 0;
}
