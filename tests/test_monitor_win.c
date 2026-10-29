#include "model.h"
#include "recording.h"
/* Exercise production hook normalization/queueing without installing hooks or
 * injecting any Windows input. */
#include "../tools/input-monitor/observer.c"
#include <stdio.h>
#include <wchar.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %u: %s\n",(unsigned)__LINE__,#c);failures++;}}while(0)
static hw_rec_event emitted[16];static unsigned event_count;
static void collect(const hw_rec_event *e,void *context){(void)context;if(event_count<16)emitted[event_count++]=*e;}
static void recording_tests(void)
{
    RAWINPUT raw={0};raw.header.dwType=RIM_TYPEMOUSE;
    raw.data.mouse.usButtonFlags=RI_MOUSE_MIDDLE_BUTTON_DOWN|RI_MOUSE_MIDDLE_BUTTON_UP|RI_MOUSE_WHEEL;raw.data.mouse.usButtonData=(USHORT)(SHORT)-120;
    im_raw_events(&raw,2,collect,NULL);CHECK(event_count==3);CHECK(emitted[0].type==HW_REC_BUTTON_DOWN&&emitted[0].code==2);CHECK(emitted[1].type==HW_REC_BUTTON_UP);CHECK(emitted[2].type==HW_REC_WHEEL&&emitted[2].x==-120);
    wchar_t name[256];im_event_name(&emitted[2],name,256);CHECK(wcsstr(name,L"Wheel rotation")!=NULL);
    hw_rec_event special={0,0,HW_REC_KEY_DOWN,0x45,RI_KEY_E1,0,0,VK_PAUSE};im_event_name(&special,name,256);CHECK(wcsstr(name,L"Pause DOWN")!=NULL);
    event_count=0;raw.header.dwType=RIM_TYPEKEYBOARD;memset(&raw.data,0,sizeof raw.data);raw.data.keyboard.MakeCode=0x1d;raw.data.keyboard.Flags=RI_KEY_E0;raw.data.keyboard.VKey=VK_CONTROL;
    im_raw_events(&raw,1,collect,NULL);CHECK(event_count==1&&emitted[0].code==0x1d&&emitted[0].flags==RI_KEY_E0);
    event_count=0;raw.data.keyboard.MakeCode=0x03b1;raw.data.keyboard.VKey=VK_PACKET;raw.data.keyboard.Flags=0;
    im_raw_events(&raw,1,collect,NULL);CHECK(event_count==1&&emitted[0].code==0x03b1&&emitted[0].vk==VK_PACKET);
    im_event_name(&emitted[0],name,256);CHECK(wcsstr(name,L"not HID-replayable")!=NULL);
    wchar_t path[1200];GetCurrentDirectoryW(1000,path);wcscat(path,L"\\monitor-recording-test.hidrec");
    wchar_t csv[1300];swprintf(csv,1300,L"%ls.csv",path);DeleteFileW(path);DeleteFileW(csv);
    hw_rec_screen screen={1920,1080,1920,1080};im_device device={0};wcscpy(device.alias,L"\t=Dangerous,\"CSV\"");wcscpy(device.id,L"test");
    CHECK(im_record_start(path,&screen));
    for(unsigned i=0;i<200;i++){hw_rec_event e={i,1,i%2?HW_REC_KEY_UP:HW_REC_KEY_DOWN,30,0,0,0,65};CHECK(im_record_push(&e,&device));}
    im_record_stop(200);int state=2;
    for(unsigned i=0;i<500&&state==2;i++){Sleep(10);state=im_record_poll();}
    CHECK(state==3&&im_record_count()==200);
    FILE *f=_wfopen(path,L"rb");hw_recording recording={0};char error[128];CHECK(f!=NULL);if(f){CHECK(hw_rec_load(f,&recording,error,sizeof error));CHECK(recording.count==200);hw_rec_free(&recording);fclose(f);}
    f=_wfopen(csv,L"rb");CHECK(f!=NULL);if(f){char text[4096];size_t n=fread(text,1,sizeof text-1,f);text[n]=0;CHECK(strstr(text,"\"'\t=Dangerous,\"\"CSV\"\"\"")!=NULL);fclose(f);}
    /* Existing outputs are not replaced. Writer completion reports failure. */
    CHECK(im_record_start(path,&screen));hw_rec_event e={0,0,HW_REC_MOVE,0,0,1,2,0};CHECK(im_record_push(&e,&device));im_record_stop(1);
    for(unsigned i=0;i<500&&im_record_poll()==2;i++)Sleep(10);CHECK(im_record_poll()==-1);
    DeleteFileW(path);DeleteFileW(csv);wchar_t partial[1400];swprintf(partial,1400,L"%ls.partial",path);DeleteFileW(partial);swprintf(partial,1400,L"%ls.csv.partial",path);DeleteFileW(partial);
}
static void observer_recording_tests(void)
{
    im_observer_record(1);KBDLLHOOKSTRUCT k={0};k.vkCode='A';k.scanCode=30;
    observe_keyboard(&k);im_observed_event event;int overflow=0;CHECK(!im_observer_take(&event,&overflow));
    k.flags=LLKHF_INJECTED;observe_keyboard(&k);CHECK(im_observer_take(&event,&overflow)&&event.keyboard&&event.event.type==HW_REC_KEY_DOWN&&event.event.code==30);
    k.flags|=LLKHF_UP;observe_keyboard(&k);CHECK(im_observer_take(&event,&overflow)&&event.event.type==HW_REC_KEY_UP);
    k.vkCode=VK_F9;observe_keyboard(&k);CHECK(!im_observer_take(&event,&overflow));
    MSLLHOOKSTRUCT m={0};m.pt.x=100;m.pt.y=100;observe_mouse(WM_MOUSEMOVE,&m);
    m.flags=LLMHF_INJECTED;m.pt.x=110;m.pt.y=95;observe_mouse(WM_MOUSEMOVE,&m);
    CHECK(im_observer_take(&event,&overflow)&&!event.keyboard&&event.event.type==HW_REC_MOVE&&event.event.x==10&&event.event.y==-5);
    m.mouseData=(DWORD)(USHORT)(SHORT)-120<<16;observe_mouse(WM_MOUSEWHEEL,&m);
    CHECK(im_observer_take(&event,&overflow)&&event.event.type==HW_REC_WHEEL&&event.event.x==-120);
    observe_mouse(WM_MBUTTONDOWN,&m);CHECK(im_observer_take(&event,&overflow)&&event.event.type==HW_REC_BUTTON_DOWN&&event.event.code==2);
    im_observer_record(0);k.vkCode='B';observe_keyboard(&k);CHECK(!im_observer_take(&event,&overflow));
    im_observer_record(1);for(unsigned i=0;i<4100;i++)observe_keyboard(&k);
    CHECK(im_observer_take(&event,&overflow)&&overflow);im_observer_record(0);
    while(im_observer_take(&event,&overflow)){}im_observer_reset();
}
int main(void)
{
    im_device d={0};RAWINPUT r={0};d.used=d.connected=1;d.keyboard=1;
    wcscpy(d.name,L"Windows keyboard");wcscpy(d.alias,L"Πληκτρολόγιο γραφείου");
    im_identity(L"\\\\?\\HID#VID_CAFE&PID_4877",1,d.id);
    wchar_t same[24],other[24];im_identity(L"\\\\?\\hid#vid_cafe&pid_4877",1,same);
    im_identity(L"\\\\?\\HID#VID_CAFE&PID_4877",0,other);
    CHECK(!wcscmp(d.id,same));CHECK(wcscmp(d.id,other)!=0);
    CHECK(im_metadata_source(L"ROOT\\UNKNOWN",L"ROOT",L"HID-compliant mouse")==IM_UNKNOWN);
    CHECK(im_metadata_source(L"ROOT\\RDP_MOU\\0000",L"ROOT",L"Mouse")==IM_VIRTUAL);
    CHECK(im_metadata_source(L"USB\\VID_1234",L"USB",L"Mouse")==IM_USB);
    CHECK(im_metadata_source(L"BTHENUM\\ABC",L"BTHENUM",L"Mouse")==IM_BLUETOOTH);
    CHECK(im_metadata_source(L"ACPI\\PNP0303",L"ACPI",L"Keyboard")==IM_BUILTIN);
    CHECK(im_metadata_source(L"ROOT\\X",L"ROOT",L"Virtual input device")==IM_VIRTUAL);
    r.header.dwType=RIM_TYPEKEYBOARD;r.data.keyboard.MakeCode=0x1e;
    im_record(&d.counts,&r);im_record(&d.counts,&r);
    CHECK(d.counts.down==1&&d.counts.repeat==1&&d.counts.held==1);
    r.data.keyboard.Flags=RI_KEY_BREAK;im_record(&d.counts,&r);im_record(&d.counts,&r);
    CHECK(d.counts.up==2&&!d.counts.held&&d.counts.unmatched==1);
    r.data.keyboard.MakeCode=0x1d;r.data.keyboard.Flags=0;im_record(&d.counts,&r);
    r.data.keyboard.Flags=RI_KEY_E0;im_record(&d.counts,&r);CHECK(d.counts.held==2);
    r.data.keyboard.MakeCode=0;r.data.keyboard.VKey='A';r.data.keyboard.Flags=0;im_record(&d.counts,&r);
    r.data.keyboard.VKey='B';im_record(&d.counts,&r);CHECK(d.counts.held==4);
    im_reset(&d);CHECK(!d.counts.down&&!d.counts.held&&!wcscmp(d.alias,L"Πληκτρολόγιο γραφείου"));
    im_counts mouse={0};memset(&r,0,sizeof r);r.header.dwType=RIM_TYPEMOUSE;
    for(unsigned i=0;i<5;i++){r.data.mouse.usButtonFlags=(USHORT)(1u<<(2*i));im_record(&mouse,&r);}
    CHECK(mouse.down==5&&mouse.held==31);
    for(unsigned i=0;i<5;i++){r.data.mouse.usButtonFlags=(USHORT)(2u<<(2*i));im_record(&mouse,&r);}
    CHECK(mouse.up==5&&!mouse.held&&!mouse.unmatched);
    r.data.mouse.usButtonFlags=RI_MOUSE_WHEEL;r.data.mouse.usButtonData=(USHORT)(SHORT)-240;
    r.data.mouse.lLastX=12;r.data.mouse.lLastY=-7;im_record(&mouse,&r);
    CHECK(mouse.wheel==-240&&mouse.dx==12&&mouse.dy==-7);
    r.data.mouse.usFlags=MOUSE_MOVE_ABSOLUTE;r.data.mouse.usButtonFlags=RI_MOUSE_HWHEEL;r.data.mouse.usButtonData=120;
    im_record(&mouse,&r);CHECK(mouse.pan==120&&mouse.dx==12&&mouse.dy==-7);
    d.counts.held=1;d.counts.down=9;im_disconnect(&d);CHECK(!d.connected&&!d.counts.held&&d.counts.down==9);
    d.label=IM_LABEL_VIRTUAL;CHECK(!wcscmp(im_source_name(&d),L"Virtual (your label)"));
    wchar_t file[1024];CHECK(GetCurrentDirectoryW(900,file)!=0);wcscat(file,L"\\monitor-test-aliases.ini");
    DeleteFileW(file);CHECK(im_save_label(file,&d));
    im_device loaded={0};wcscpy(loaded.id,d.id);im_load_label(file,&loaded);
    CHECK(!wcscmp(loaded.alias,d.alias)&&loaded.label==IM_LABEL_VIRTUAL);
    wcscpy(d.alias,L"Renamed device");d.label=IM_LABEL_AUTO;CHECK(im_save_label(file,&d));im_load_label(file,&loaded);
    CHECK(!wcscmp(loaded.alias,L"Renamed device")&&loaded.label==IM_LABEL_AUTO);
    im_device second={0};im_identity(L"second device",0,second.id);wcscpy(second.alias,L"Second mouse");
    CHECK(im_save_label(file,&second));im_load_label(file,&loaded);CHECK(!wcscmp(loaded.alias,L"Renamed device"));
    im_load_label(file,&second);CHECK(!wcscmp(second.alias,L"Second mouse"));
    wchar_t parent[1100],bad[1200];swprintf(parent,1100,L"%ls.parent",file);swprintf(bad,1200,L"%ls\\labels.ini",parent);
    HANDLE h=CreateFileW(parent,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(h!=INVALID_HANDLE_VALUE);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    CHECK(!im_save_label(bad,&d));DeleteFileW(parent);
    DeleteFileW(file);
    recording_tests();
    observer_recording_tests();
    printf("Input monitor: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
