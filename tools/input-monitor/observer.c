#include "observer.h"
#include "recording.h"
#include <string.h>
static HANDLE worker;
static DWORD worker_id;
static HHOOK keyboard_hook,mouse_hook;
static volatile LONG ready_state,paused_state,stop_requested;
static SRWLOCK lock=SRWLOCK_INIT;
static im_counts keyboard_counts,mouse_counts;
static volatile LONG keyboard_seen,mouse_seen;
static im_observed_event observed[4096];static unsigned observed_head,observed_count;
static int record_on,record_overflow,point_valid;static POINT previous_point;
static void queue_observed(const hw_rec_event *event,void *context)
{
    if(!record_on||(event->type<=HW_REC_KEY_UP&&event->vk==VK_F9))return;
    if(observed_count==4096){record_overflow=1;return;}
    im_observed_event *out=&observed[(observed_head+observed_count)%4096];
    out->event=*event;out->tick=GetTickCount();out->keyboard=context!=NULL;observed_count++;
}
static void observe_keyboard(const KBDLLHOOKSTRUCT *k)
{
    if(!(k->flags&(LLKHF_INJECTED|LLKHF_LOWER_IL_INJECTED)))return;
    RAWINPUT r={0};r.header.dwType=RIM_TYPEKEYBOARD;
    r.data.keyboard.MakeCode=(USHORT)k->scanCode;r.data.keyboard.VKey=(USHORT)k->vkCode;
    r.data.keyboard.Flags=(USHORT)(((k->flags&LLKHF_EXTENDED)?RI_KEY_E0:0)|((k->flags&LLKHF_UP)?RI_KEY_BREAK:0));
    AcquireSRWLockExclusive(&lock);im_record(&keyboard_counts,&r);im_raw_events(&r,0,queue_observed,(void *)1);ReleaseSRWLockExclusive(&lock);
}
static LRESULT CALLBACK keyboard_proc(int code,WPARAM w,LPARAM l)
{
    if(code==HC_ACTION&&!InterlockedCompareExchange(&paused_state,0,0)) {
        InterlockedIncrement(&keyboard_seen);
        observe_keyboard((const KBDLLHOOKSTRUCT *)l);
    }
    return CallNextHookEx(NULL,code,w,l); /* observation only; never consume input */
}
static void observe_mouse(WPARAM w,const MSLLHOOKSTRUCT *m)
{
    AcquireSRWLockExclusive(&lock);
        if(m->flags&(LLMHF_INJECTED|LLMHF_LOWER_IL_INJECTED)) {
            RAWINPUT r={0};r.header.dwType=RIM_TYPEMOUSE;
            switch(w) {
            case WM_MOUSEMOVE:if(point_valid){r.data.mouse.lLastX=m->pt.x-previous_point.x;r.data.mouse.lLastY=m->pt.y-previous_point.y;}break;
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
            im_record(&mouse_counts,&r);im_raw_events(&r,1,queue_observed,NULL);
        }
    previous_point=m->pt;point_valid=1;ReleaseSRWLockExclusive(&lock);
}
static LRESULT CALLBACK mouse_proc(int code,WPARAM w,LPARAM l)
{
    if(code==HC_ACTION&&!InterlockedCompareExchange(&paused_state,0,0)){InterlockedIncrement(&mouse_seen);observe_mouse(w,(const MSLLHOOKSTRUCT *)l);}
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
void im_observer_record(int on)
{
    POINT point={0};int valid=GetCursorPos(&point)!=0;
    AcquireSRWLockExclusive(&lock);record_on=on;
    if(on){observed_head=observed_count=0;record_overflow=0;previous_point=point;point_valid=valid;}
    ReleaseSRWLockExclusive(&lock);
}
int im_observer_take(im_observed_event *event,int *overflow)
{
    AcquireSRWLockExclusive(&lock);*overflow=record_overflow;int got=observed_count!=0;
    if(got){*event=observed[observed_head];observed_head=(observed_head+1)%4096;observed_count--;}
    ReleaseSRWLockExclusive(&lock);return got;
}
