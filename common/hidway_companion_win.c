#define WIN32_LEAN_AND_MEAN
#include "hidway_companion_win.h"
#include <shellapi.h>
#include <wchar.h>
int hw_companion_open(HWND owner,int target)
{
    const wchar_t *files[]={L"hidway-client.exe",L"hidway-input-monitor.exe",L"HIDway-Setup.exe",L"HIDway-Setup.exe"};
    const wchar_t *classes[]={L"HIDwayClient",L"HIDwayInputMonitor",NULL,NULL};
    if(target<0||target>HW_COMPANION_SETUP)return HW_COMPANION_FAILED;
    wchar_t folder[2048],path[2048];DWORD n=GetModuleFileNameW(NULL,folder,2048);
    if(!n||n>=2048)return HW_COMPANION_FAILED;
    wchar_t *slash=wcsrchr(folder,L'\\');if(!slash)return HW_COMPANION_FAILED;*slash=0;
    if(wcslen(folder)+wcslen(files[target])+2>2048)return HW_COMPANION_FAILED;
    swprintf(path,2048,L"%ls\\%ls",folder,files[target]);
    HWND existing=classes[target]?FindWindowW(classes[target],NULL):NULL;
    if(existing){
        DWORD pid=0;GetWindowThreadProcessId(existing,&pid);
        HANDLE process=pid?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid):NULL;
        wchar_t actual[2048];DWORD length=2048;
        int matches=process&&QueryFullProcessImageNameW(process,0,actual,&length)&&!_wcsicmp(actual,path);
        if(process)CloseHandle(process);
        if(!matches)return HW_COMPANION_OTHER_COPY;
        ShowWindowAsync(existing,SW_RESTORE);SetForegroundWindow(existing);return HW_COMPANION_OK;
    }
    DWORD attributes=GetFileAttributesW(path);
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return HW_COMPANION_MISSING;
    return (INT_PTR)ShellExecuteW(owner,L"open",path,target==HW_COMPANION_UPDATES?L"--updates":NULL,folder,SW_SHOWNORMAL)>32?HW_COMPANION_OK:HW_COMPANION_FAILED;
}
const wchar_t *hw_companion_error(int result)
{
    if(result==HW_COMPANION_OTHER_COPY)return L"A copy from another folder is running. Use the same installed HIDway and Monitor pair; the active session was left untouched.";
    if(result==HW_COMPANION_MISSING)return L"This companion is missing. Install or extract the complete HIDway Windows package.";
    return L"Windows could not open the companion. Check the installation folder.";
}
