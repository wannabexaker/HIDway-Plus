#include "observer.h"
#include <string.h>
static HANDLE worker;
static DWORD worker_id;
static HHOOK keyboard_hook,mouse_hook;
static volatile LONG ready_state,paused_state,stop_requested;
static SRWLOCK lock=SRWLOCK_INIT;
static im_counts keyboard_counts,mouse_counts;
static volatile LONG keyboard_seen,mouse_seen;
static LRESULT CALLBACK keyboard_proc(int code,WPARAM w,LPARAM l)
{
    if(code==HC_ACTION&&!InterlockedCompareExchange(&paused_state,0,0)) {
        InterlockedIncrement(&keyboard_seen);
        const KBDLLHOOKSTRUCT *k=(const KBDLLHOOKSTRUCT *)l;
        if(k->flags&(LLKHF_INJECTED|LLKHF_LOWER_IL_INJECTED)) {
            RAWINPUT r={0};r.header.dwType=RIM_TYPEKEYBOARD;
            r.data.keyboard.MakeCode=(USHORT)k->scanCode;r.data.keyboard.VKey=(USHORT)k->vkCode;
            r.data.keyboard.Flags=(USHORT)(((k->flags&LLKHF_EXTENDED)?RI_KEY_E0:0)|((k->flags&LLKHF_UP)?RI_KEY_BREAK:0));
            AcquireSRWLockExclusive(&lock);im_record(&keyboard_counts,&r);ReleaseSRWLockExclusive(&lock);
        }
    }
    return CallNextHookEx(NULL,code,w,l); /* observation only; never consume input */
}
static LRESULT CALLBACK mouse_proc(int code,WPARAM w,LPARAM l)
{
    if(code==HC_ACTION&&!InterlockedCompareExchange(&paused_state,0,0)) {
        InterlockedIncrement(&mouse_seen);
        const MSLLHOOKSTRUCT *m=(const MSLLHOOKSTRUCT *)l;
        if(m->flags&(LLMHF_INJECTED|LLMHF_LOWER_IL_INJECTED)) {
            RAWINPUT r={0};r.header.dwType=RIM_TYPEMOUSE;
            switch(w) {
            case WM_LBUTTONDOWN:r.data.mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;break;
            case WM_LBUTTONUP:r.data.mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_UP;break;
            case WM_RBUTTONDOWN:r.data.mouse.usButtonFlags=RI_MOUSE_RIGHT_BUTTON_DOWN;break;
            case WM_RBUTTONUP:r.data.mouse.usButtonFlags=RI_MOUSE_RIGHT_BUTTON_UP;break;
            case WM_MBUTTONDOWN:r.data.mouse.usButtonFlags=RI_MOUSE_MIDDLE_BUTTON_DOWN;break;
            case WM_MBUTTONUP:r.data.mouse.usButtonFlags=RI_MOUSE_MIDDLE_BUTTON_UP;break;
            case WM_XBUTTONDOWN:r.data.mouse.usButtonFlags=HIWORD(m->mouseData)==XBUTTON1?RI_MOUSE_BUTTON_4_DOWN:RI_MOUSE_BUTTON_5_DOWN;break;
            case WM_XBUTTONUP:r.data.mouse.usButtonFlags=HIWORD(m->mouseData)==XBUTTON1?RI_MOUSE_BUTTON_4_UP:RI_MOUSE_BUTTON_5_UP;break;
            case WM_MOUSEWHEEL:r.data.mouse.usButtonFlags=RI_MOUSE_WHEEL;break;
            case WM_MOUSEHWHEEL:r.data.mouse.usButtonFlags=RI_MOUSE_HWHEEL;break;
            }
            r.data.mouse.usButtonData=HIWORD(m->mouseData);
            AcquireSRWLockExclusive(&lock);im_record(&mouse_counts,&r);ReleaseSRWLockExclusive(&lock);
        }
    }
    return CallNextHookEx(NULL,code,w,l);
}
static DWORD WINAPI observe_thread(LPVOID event)
{
    MSG msg;PeekMessageW(&msg,NULL,WM_USER,WM_USER,PM_NOREMOVE);
    if(!InterlockedCompareExchange(&stop_requested,0,0)) {
        keyboard_hook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard_proc,GetModuleHandleW(NULL),0);
        mouse_hook=SetWindowsHookExW(WH_MOUSE_LL,mouse_proc,GetModuleHandleW(NULL),0);
    }
    InterlockedExchange(&ready_state,keyboard_hook&&mouse_hook?1:-1);SetEvent((HANDLE)event);CloseHandle((HANDLE)event);
    if(keyboard_hook&&mouse_hook&&!InterlockedCompareExchange(&stop_requested,0,0))
        while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(keyboard_hook)UnhookWindowsHookEx(keyboard_hook);
    if(mouse_hook)UnhookWindowsHookEx(mouse_hook);
    keyboard_hook=mouse_hook=NULL;return 0;
}
int im_observer_start(void)
{
    if(worker&&WaitForSingleObject(worker,0)==WAIT_OBJECT_0){CloseHandle(worker);worker=NULL;}
    if(worker)return !InterlockedCompareExchange(&stop_requested,0,0)&&InterlockedCompareExchange(&ready_state,0,0)==1;
    HANDLE event=CreateEventW(NULL,TRUE,FALSE,NULL);if(!event)return 0;
    HANDLE thread_event=NULL;
    if(!DuplicateHandle(GetCurrentProcess(),event,GetCurrentProcess(),&thread_event,0,FALSE,DUPLICATE_SAME_ACCESS)){CloseHandle(event);return 0;}
    InterlockedExchange(&ready_state,0);InterlockedExchange(&stop_requested,0);
    worker=CreateThread(NULL,0,observe_thread,thread_event,0,&worker_id);
    if(!worker){CloseHandle(thread_event);CloseHandle(event);return 0;}
    DWORD wait=WaitForSingleObject(event,2000);
    CloseHandle(event); /* worker owns a separate reference, including timeout */
    if(wait!=WAIT_OBJECT_0||InterlockedCompareExchange(&ready_state,0,0)!=1){im_observer_stop();return 0;}
    return 1;
}
void im_observer_stop(void)
{
    if(!worker)return;
    InterlockedExchange(&stop_requested,1);
    PostThreadMessageW(worker_id,WM_QUIT,0,0);
    if(WaitForSingleObject(worker,1000)==WAIT_OBJECT_0){CloseHandle(worker);worker=NULL;}
}
void im_observer_pause(int paused){InterlockedExchange(&paused_state,paused);}
void im_observer_reset(void)
{
    AcquireSRWLockExclusive(&lock);
    memset(&keyboard_counts,0,sizeof keyboard_counts);memset(&mouse_counts,0,sizeof mouse_counts);
    InterlockedExchange(&keyboard_seen,0);InterlockedExchange(&mouse_seen,0);
    ReleaseSRWLockExclusive(&lock);
}
void im_observer_snapshot(im_counts *keyboard,im_counts *mouse)
{
    AcquireSRWLockShared(&lock);*keyboard=keyboard_counts;*mouse=mouse_counts;ReleaseSRWLockShared(&lock);
}
void im_observer_seen(LONG *keyboard,LONG *mouse)
{
    *keyboard=InterlockedCompareExchange(&keyboard_seen,0,0);*mouse=InterlockedCompareExchange(&mouse_seen,0,0);
}
