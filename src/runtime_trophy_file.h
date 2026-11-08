#ifndef PS4RT_RUNTIME_TROPHY_FILE_H
#define PS4RT_RUNTIME_TROPHY_FILE_H

/* Optional local trophy journal. Based on shinseiryuu's PR #1:
 * https://github.com/Hoverbike51/Star-Ocean-Integrity-and-Faithlessness-PS4RT/pull/1
 * Preserve the interoperable {"unlocked":{"001":"UTC timestamp"}} format.
 * This host-only helper does not implement PSN or change guest trophy answers. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum TrophyFileResult { TROPHY_FILE_SKIPPED, TROPHY_FILE_UNCHANGED,
    TROPHY_FILE_SAVED, TROPHY_FILE_BUSY, TROPHY_FILE_INVALID, TROPHY_FILE_IO_ERROR };
typedef struct { const char *at, *end; } TrophyJson;

static void trophy_json_space(TrophyJson *j) {
    while (j->at<j->end && (*j->at==' ' || *j->at=='\t' || *j->at=='\r' || *j->at=='\n')) ++j->at;
}
static int trophy_json_take(TrophyJson *j,char c) {
    trophy_json_space(j);
    if (j->at==j->end || *j->at!=c) return 0;
    ++j->at; return 1;
}
static int trophy_json_literal(TrophyJson *j,const char *s) {
    trophy_json_space(j); size_t n=strlen(s);
    if ((size_t)(j->end-j->at)<n || memcmp(j->at,s,n)) return 0;
    j->at+=n; return 1;
}
static int trophy_timestamp_valid(const char *s) {
    for (unsigned i=0;i<20;++i) {
        char separator=i==4 || i==7?'-':i==10?'T':i==13 || i==16?':':i==19?'Z':0;
        if (separator ? s[i]!=separator : s[i]<'0' || s[i]>'9') return 0;
    }
    unsigned month=(s[5]-'0')*10+s[6]-'0',day=(s[8]-'0')*10+s[9]-'0';
    unsigned hour=(s[11]-'0')*10+s[12]-'0',minute=(s[14]-'0')*10+s[15]-'0';
    unsigned second=(s[17]-'0')*10+s[18]-'0';
    return month>=1 && month<=12 && day>=1 && day<=31 && hour<24 && minute<60 && second<=60;
}
/* Deliberately accept only the documented journal schema, including whitespace.
 * Reject truncated/oversized/foreign documents instead of silently overwriting them. */
static int trophy_file_parse(const char *body,size_t length,char saved[128][21]) {
    TrophyJson j={body,body+length}; memset(saved,0,128*21);
    if (!trophy_json_take(&j,'{') || !trophy_json_literal(&j,"\"unlocked\"") ||
        !trophy_json_take(&j,':') || !trophy_json_take(&j,'{')) return 0;
    trophy_json_space(&j);
    if (j.at<j.end && *j.at!='}') for (;;) {
        if (!trophy_json_take(&j,'"') || j.end-j.at<4) return 0;
        unsigned id=0;
        for (unsigned i=0;i<3;++i) {
            if (*j.at<'0' || *j.at>'9') return 0;
            id=id*10+(unsigned)(*j.at++-'0');
        }
        if (id>=128 || saved[id][0] || *j.at++!='"' ||
            !trophy_json_take(&j,':') || !trophy_json_take(&j,'"') || j.end-j.at<21 ||
            !trophy_timestamp_valid(j.at)) return 0;
        memcpy(saved[id],j.at,20); j.at+=20;
        if (*j.at++!='"') return 0;
        trophy_json_space(&j);
        if (j.at==j.end) return 0;
        if (*j.at=='}') break;
        if (!trophy_json_take(&j,',')) return 0;
    }
    if (!trophy_json_take(&j,'}') || !trophy_json_take(&j,'}')) return 0;
    trophy_json_space(&j); return j.at==j.end;
}

static enum TrophyFileResult trophy_file_note(int32_t id) {
    if (id<0 || id>127) return TROPHY_FILE_SKIPPED;
    DWORD length=GetEnvironmentVariableW(L"PS4RT_TROPHY_FILE",NULL,0);
    if (!length) return TROPHY_FILE_SKIPPED;
    if (length>32700) return TROPHY_FILE_IO_ERROR;
    wchar_t *environment=calloc(length,sizeof(wchar_t));
    if (!environment) return TROPHY_FILE_IO_ERROR;
    DWORD got=GetEnvironmentVariableW(L"PS4RT_TROPHY_FILE",environment,length);
    if (!got || got>=length) { free(environment); return TROPHY_FILE_IO_ERROR; }
    DWORD required=GetFullPathNameW(environment,0,NULL,NULL);
    wchar_t *path=required && required<=32700?calloc(required,sizeof(wchar_t)):NULL;
    got=path?GetFullPathNameW(environment,required,path,NULL):0; free(environment);
    if (!got || got>=required) { free(path); return TROPHY_FILE_IO_ERROR; }
    wchar_t *sidecar=calloc(required+64,sizeof(wchar_t));
    if (!sidecar) { free(path); return TROPHY_FILE_IO_ERROR; }
    swprintf(sidecar,required+64,L"%ls.so5-trophy.lock",path);
    /* Serialize cooperating threads/processes without a gameplay-blocking retry.
     * A busy external writer is reported; the original document is left intact. */
    HANDLE lock=CreateFileW(sidecar,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,OPEN_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
    if (lock==INVALID_HANDLE_VALUE) {
        DWORD error=GetLastError(); free(sidecar); free(path);
        return error==ERROR_SHARING_VIOLATION?TROPHY_FILE_BUSY:TROPHY_FILE_IO_ERROR;
    }
    enum TrophyFileResult result=TROPHY_FILE_IO_ERROR;
    char saved[128][21],body[16385]; memset(saved,0,sizeof(saved));
    HANDLE input=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (input!=INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size; DWORD read=0;
        int sized=GetFileSizeEx(input,&size) && size.QuadPart>0 && size.QuadPart<=16384;
        int valid=sized && ReadFile(input,body,(DWORD)size.QuadPart,&read,NULL) && read==size.QuadPart;
        CloseHandle(input);
        if (!valid) { result=sized?TROPHY_FILE_IO_ERROR:TROPHY_FILE_INVALID; goto done; }
        if (!trophy_file_parse(body,read,saved)) { result=TROPHY_FILE_INVALID; goto done; }
    } else if (GetLastError()!=ERROR_FILE_NOT_FOUND) goto done;
    if (saved[id][0]) { result=TROPHY_FILE_UNCHANGED; goto done; }
    SYSTEMTIME utc; GetSystemTime(&utc);
    int formatted=snprintf(saved[id],sizeof(saved[id]),"%04u-%02u-%02uT%02u:%02u:%02uZ",
        (unsigned)utc.wYear,(unsigned)utc.wMonth,(unsigned)utc.wDay,
        (unsigned)utc.wHour,(unsigned)utc.wMinute,(unsigned)utc.wSecond);
    if (formatted!=20) goto done;
    size_t used=0;
    memcpy(body,"{\"unlocked\":{",13); used=13;
    int first=1;
    for (unsigned number=0;number<128;++number) if (saved[number][0]) {
        int n=snprintf(body+used,sizeof(body)-used,"%s\"%03u\":\"%s\"",first?"":",",number,saved[number]);
        if (n<0 || (size_t)n>=sizeof(body)-used) goto done;
        used+=(size_t)n; first=0;
    }
    memcpy(body+used,"}}\n",3); used+=3;
    static volatile LONG sequence;
    swprintf(sidecar,required+64,L"%ls.so5-%lu-%lu.tmp",path,
             (unsigned long)GetCurrentProcessId(),(unsigned long)InterlockedIncrement(&sequence));
    HANDLE output=CreateFileW(sidecar,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if (output==INVALID_HANDLE_VALUE) goto done;
    DWORD written=0;
    int complete=WriteFile(output,body,(DWORD)used,&written,NULL) && written==used && FlushFileBuffers(output);
    if (!CloseHandle(output)) complete=0;
    if (complete && MoveFileExW(sidecar,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) result=TROPHY_FILE_SAVED;
    else DeleteFileW(sidecar); /* Only our successfully created temporary file. */
done:
    CloseHandle(lock); free(sidecar); free(path); return result;
}
#endif
