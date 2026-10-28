#include "model.h"
#include <stdio.h>
#include <wchar.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %u: %s\n",(unsigned)__LINE__,#c);failures++;}}while(0)
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
    printf("Input monitor: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
