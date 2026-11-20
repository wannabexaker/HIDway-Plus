/* Production companion routing with Win32 calls replaced. Never opens apps. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>
static int fixture_existing,query_ok=1,shown,launched,failures;
static DWORD fixture_attributes=FILE_ATTRIBUTE_NORMAL,module_length;
static INT_PTR shell_result=33;
static wchar_t fixture_actual[2048]=L"C:\\Fixture\\hidway-client.exe",opened[2048],arguments[64],directory[2048];
static DWORD fake_module(HMODULE m,LPWSTR s,DWORD size){(void)m;(void)size;wcscpy(s,L"C:\\Fixture\\hidway-input-monitor.exe");return module_length?module_length:(DWORD)wcslen(s);}
static HWND fake_find(LPCWSTR c,LPCWSTR t){(void)c;(void)t;return fixture_existing?(HWND)(INT_PTR)1:NULL;}
static DWORD fake_pid(HWND w,LPDWORD p){(void)w;*p=9;return 1;}
static HANDLE fake_process(DWORD access,BOOL inherit,DWORD pid){(void)access;(void)inherit;(void)pid;return (HANDLE)(INT_PTR)2;}
static BOOL fake_query(HANDLE p,DWORD f,LPWSTR s,PDWORD n){(void)p;(void)f;(void)n;wcscpy(s,fixture_actual);return query_ok;}
static BOOL fake_close(HANDLE p){(void)p;return TRUE;}
static BOOL fake_show(HWND w,int show){(void)w;(void)show;shown++;return TRUE;}
static BOOL fake_foreground(HWND w){(void)w;return TRUE;}
static DWORD fake_attributes(LPCWSTR p){(void)p;return fixture_attributes;}
static HINSTANCE fake_shell(HWND w,LPCWSTR verb,LPCWSTR file,LPCWSTR args,LPCWSTR cwd,int show){(void)w;(void)show;if(wcscmp(verb,L"open"))failures++;launched++;wcscpy(opened,file);wcscpy(arguments,args?args:L"");wcscpy(directory,cwd);return (HINSTANCE)shell_result;}
#define GetModuleFileNameW fake_module
#define FindWindowW fake_find
#define GetWindowThreadProcessId fake_pid
#define OpenProcess fake_process
#define QueryFullProcessImageNameW fake_query
#define CloseHandle fake_close
#define ShowWindowAsync fake_show
#define SetForegroundWindow fake_foreground
#define GetFileAttributesW fake_attributes
#define ShellExecuteW fake_shell
#include "../common/hidway_companion_win.c"
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);failures++;}}while(0)
int main(void)
{
    CHECK(hw_companion_open(NULL,-1)==HW_COMPANION_FAILED&&!launched);
    fixture_attributes=INVALID_FILE_ATTRIBUTES;
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_MISSING&&!launched);
    fixture_attributes=FILE_ATTRIBUTE_REPARSE_POINT;
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_MISSING&&!launched);
    fixture_attributes=FILE_ATTRIBUTE_NORMAL;
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_OK&&launched==1);
    CHECK(!wcscmp(opened,L"C:\\Fixture\\hidway-client.exe")&&!arguments[0]&&!wcscmp(directory,L"C:\\Fixture"));
    CHECK(hw_companion_open(NULL,HW_COMPANION_MONITOR)==HW_COMPANION_OK&&launched==2);
    CHECK(!wcscmp(opened,L"C:\\Fixture\\hidway-input-monitor.exe"));
    CHECK(hw_companion_open(NULL,HW_COMPANION_UPDATES)==HW_COMPANION_OK&&launched==3);
    CHECK(!wcscmp(opened,L"C:\\Fixture\\HIDway-Setup.exe")&&!wcscmp(arguments,L"--updates"));
    fixture_existing=1;
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_OK&&launched==3&&shown==1);
    wcscpy(fixture_actual,L"C:\\Other\\hidway-client.exe");
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_OTHER_COPY&&launched==3&&shown==1);
    query_ok=0;
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_OTHER_COPY&&launched==3);
    fixture_existing=0;shell_result=31;
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_FAILED);
    module_length=2048;
    CHECK(hw_companion_open(NULL,HW_COMPANION_CLIENT)==HW_COMPANION_FAILED);
    printf("Companion routing: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
