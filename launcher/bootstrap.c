/* Native player entry point. No shell or application DLL is needed at the root. */
#define UNICODE
#define _UNICODE
#include <windows.h>

static WCHAR root[32768], executable[32768], message[65536];
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show) {
    (void)instance; (void)previous; (void)show;
    DWORD length=GetModuleFileNameW(NULL,root,32768);
    if (!length || length>=32768) return ERROR_FILENAME_EXCED_RANGE;
    while (length && root[length-1]!=L'\\' && root[length-1]!=L'/') --length;
    if (!length) return ERROR_PATH_NOT_FOUND;
    root[length]=0;
    const WCHAR suffix[]=L"bin\\Star Ocean 5 Launcher.exe";
    if (length+lstrlenW(suffix)>=32768) return ERROR_FILENAME_EXCED_RANGE;
    lstrcpyW(executable,root); lstrcatW(executable,suffix);
    const DWORD chars=lstrlenW(executable)+lstrlenW(arguments)+4;
    if (chars>32767) return ERROR_BAD_LENGTH;
    WCHAR* command=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,chars*sizeof(WCHAR));
    if (!command) return ERROR_NOT_ENOUGH_MEMORY;
    lstrcpyW(command,L"\""); lstrcatW(command,executable); lstrcatW(command,L"\" "); lstrcatW(command,arguments);
    STARTUPINFOW startup={0}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION process={0};
    const BOOL created=CreateProcessW(executable,command,NULL,NULL,FALSE,0,NULL,root,&startup,&process);
    const DWORD error=created ? 0 : GetLastError();
    HeapFree(GetProcessHeap(),0,command);
    if (!created) {
        /* Windows supplies the localized error text; include the exact missing/failed path. */
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,error,0,message,32000,NULL);
        lstrcatW(message,L"\n"); lstrcatW(message,executable);
        MessageBoxW(NULL,message,L"Star Ocean 5 Launcher",MB_OK|MB_ICONERROR);
        return (int)error;
    }
    CloseHandle(process.hThread);
    DWORD result=0;
    /* Package verification needs the child's real exit code. Normal launches exit immediately. */
    if (lstrcmpW(arguments,L"--self-test")==0) {
        if (WaitForSingleObject(process.hProcess,15000)!=WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess,ERROR_TIMEOUT); result=ERROR_TIMEOUT;
        } else if (!GetExitCodeProcess(process.hProcess,&result)) result=GetLastError();
    }
    CloseHandle(process.hProcess);
    return (int)result;
}
