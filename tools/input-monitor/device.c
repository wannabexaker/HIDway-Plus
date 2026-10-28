#include "model.h"
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>
static void property(DEVINST node,ULONG key,wchar_t *out,ULONG chars)
{
    ULONG bytes=chars*(ULONG)sizeof(wchar_t),type=0;out[0]=0;
    if(CM_Get_DevNode_Registry_PropertyW(node,key,&type,out,&bytes,0)!=CR_SUCCESS)out[0]=0;
    out[chars-1]=0;
}
void im_describe(im_device *d)
{
    const wchar_t *kind=d->keyboard?L"Keyboard":L"Mouse";
    swprintf(d->name,IM_NAME_LEN,L"Unidentified %ls",kind);
    wcscpy(d->evidence,d->keyboard?L"Shared no-handle feed. Windows did not identify a source device or application; origin is unknown.":
        L"Shared no-handle feed. May include a Precision Touchpad. Device/application origin is unknown.");
    d->source=IM_UNKNOWN;
    if(!d->handle){im_identity(d->keyboard?L"no-handle-keyboard":L"no-handle-mouse",d->keyboard,d->id);return;}
    UINT n=(UINT)(sizeof d->path/sizeof d->path[0]);
    if(GetRawInputDeviceInfoW(d->handle,RIDI_DEVICENAME,d->path,&n)==(UINT)-1)d->path[0]=0;
    d->path[1023]=0;
    if(!d->path[0]) {
        swprintf(d->path,1024,L"unresolved-handle-%p",d->handle);
        wcscpy(d->evidence,L"Windows could not resolve this device. This identity may change after restart.");
    }else wcscpy(d->evidence,L"Windows device metadata does not establish physical or virtual origin.");
    im_identity(d->path,d->keyboard,d->id);
    wchar_t upper[1024];wcscpy(upper,d->path);CharUpperBuffW(upper,(DWORD)wcslen(upper));
    d->pico=wcsstr(upper,L"VID_CAFE")&&wcsstr(upper,L"PID_4877");
    HDEVINFO set=SetupDiCreateDeviceInfoList(NULL,NULL);
    if(set!=INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA iface={0};iface.cbSize=sizeof iface;
        if(SetupDiOpenDeviceInterfaceW(set,d->path,0,&iface)) {
            DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,NULL,0,&bytes,NULL);
            if(bytes&&bytes<65536) {
                SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes);
                if(detail) {
                    detail->cbSize=sizeof *detail;
                    SP_DEVINFO_DATA info={0};info.cbSize=sizeof info;
                    if(SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,bytes,NULL,&info)) {
                        DEVINST node=info.DevInst;int name_rank=0;
                        CM_Get_Device_IDW(node,d->instance,512,0);
                        property(node,CM_DRP_SERVICE,d->service,96);
                        property(node,CM_DRP_MFG,d->manufacturer,IM_NAME_LEN);
                        for(unsigned level=0;level<8;level++) {
                            wchar_t friendly[IM_NAME_LEN],description[IM_NAME_LEN],bus[96],instance[512];
                            property(node,CM_DRP_FRIENDLYNAME,friendly,IM_NAME_LEN);
                            property(node,CM_DRP_DEVICEDESC,description,IM_NAME_LEN);
                            property(node,CM_DRP_ENUMERATOR_NAME,bus,96);
                            if(friendly[0]&&name_rank<2){wcscpy(d->name,friendly);name_rank=2;}
                            if(!level&&!name_rank&&description[0])wcscpy(d->name,description);
                            DEVPROPTYPE type=0;ULONG cap=sizeof friendly;friendly[0]=0;
                            if(CM_Get_DevNode_PropertyW(node,&DEVPKEY_Device_BusReportedDeviceDesc,&type,
                                (PBYTE)friendly,&cap,0)==CR_SUCCESS&&type==DEVPROP_TYPE_STRING&&friendly[0]) {
                                int rank=(!_wcsicmp(bus,L"USB")||!_wcsnicmp(bus,L"BTH",3))?4:1;
                                if(rank>name_rank){friendly[IM_NAME_LEN-1]=0;wcscpy(d->name,friendly);name_rank=rank;}
                            }
                            instance[0]=0;CM_Get_Device_IDW(node,instance,512,0);
                            unsigned source=im_metadata_source(instance,bus,description);
                            if(source==IM_VIRTUAL||(!d->source&&source)) {
                                d->source=source;wcscpy(d->bus,bus);
                                swprintf(d->evidence,256,L"Windows metadata: %ls / %ls. Bus type is evidence, not authenticated origin.",bus,description);
                            }
                            /* Stop at the actual transport; an ancestor hub or
                               host controller is not the input device's name. */
                            if(source==IM_VIRTUAL||source==IM_USB||source==IM_BLUETOOTH||source==IM_BUILTIN)break;
                            DEVINST parent;if(CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)break;node=parent;
                        }
                    }
                    HeapFree(GetProcessHeap(),0,detail);
                }
            }
        }
        SetupDiDestroyDeviceInfoList(set);
    }
    if(d->pico)swprintf(d->name,IM_NAME_LEN,L"HIDway Pico %ls",kind);
}
void im_load_label(const wchar_t *file,im_device *d)
{
    wchar_t section[40];swprintf(section,40,L"device-%ls",d->id);
    GetPrivateProfileStringW(section,L"Name",L"",d->alias,IM_NAME_LEN,file);
    d->label=GetPrivateProfileIntW(section,L"SourceLabel",0,file);
    if(d->label>IM_LABEL_TOUCHPAD)d->label=IM_LABEL_AUTO;
}
int im_save_label(const wchar_t *file,const im_device *d)
{
    wchar_t temp[1200],section[40],value[16];
    if(wcslen(file)>1100)return 0;
    swprintf(temp,1200,L"%ls.tmp",file);swprintf(section,40,L"device-%ls",d->id);
    if(!CopyFileW(file,temp,FALSE)) {
        DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return 0;
        HANDLE h=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(h==INVALID_HANDLE_VALUE)return 0;
        WORD bom=0xfeff;DWORD done=0;
        int ok=WriteFile(h,&bom,sizeof bom,&done,NULL)&&done==sizeof bom;CloseHandle(h);
        if(!ok)return 0;
    }
    swprintf(value,16,L"%u",d->label);
    if(!WritePrivateProfileStringW(section,L"Name",d->alias,temp)||
       !WritePrivateProfileStringW(section,L"SourceLabel",value,temp))return 0;
    WritePrivateProfileStringW(NULL,NULL,NULL,temp);
    return MoveFileExW(temp,file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
