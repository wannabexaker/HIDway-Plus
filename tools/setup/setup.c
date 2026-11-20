#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command,int show)
{
    (void)instance;(void)previous;(void)show;
    wchar_t folder[2048],powershell[2048],args[2304];
    DWORD n=GetModuleFileNameW(NULL,folder,2048);if(!n||n>=2048)return 2;
    wchar_t *last=wcsrchr(folder,L'\\');if(!last)return 2;*last=0;
    n=GetSystemDirectoryW(powershell,1800);if(!n||n>=1800)return 2;
    wcscat(powershell,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    const wchar_t *script=!wcscmp(command,L"--uninstall")?L"hidway-uninstall.ps1":!wcscmp(command,L"--updates")?L"hidway-updates.ps1":L"hidway-setup-gui.ps1";
    int smoke=!wcscmp(command,L"--smoke-test");
    swprintf(args,2304,L"-NoProfile -STA -ExecutionPolicy Bypass -File \"%ls\\%ls\"%ls",folder,script,smoke?L" -SmokeTest":L"");
    wchar_t command_line[4400];swprintf(command_line,4400,L"\"%ls\" %ls",powershell,args);
    STARTUPINFOW startup={0};startup.cb=sizeof startup;PROCESS_INFORMATION process={0};
    /* No console, without SW_HIDE: hiding the first window also hides WPF. */
    if(!CreateProcessW(powershell,command_line,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,folder,&startup,&process)) {
        MessageBoxW(NULL,L"Cannot start the HIDway setup wizard. Extract the whole package first.",L"HIDway Setup",MB_OK|MB_ICONERROR);return 1;
    }
    CloseHandle(process.hThread);DWORD result=0;
    if(smoke){if(WaitForSingleObject(process.hProcess,15000)!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);result=1;}else GetExitCodeProcess(process.hProcess,&result);}
    CloseHandle(process.hProcess);return (int)result;
}
