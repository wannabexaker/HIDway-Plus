#include "model.h"
#include "observer.h"
#include "theme.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
enum { ID_LIST=100,ID_PAUSE,ID_RESET,ID_REFRESH,ID_IDENTIFY,ID_BACKGROUND,ID_OBSERVER,ID_NAME,ID_SOURCE,ID_SAVE };
static HWND window,list,pause_button,reset_button,refresh_button,identify_button,background_box,observer_box;
static HWND title,subtitle,observer_text,selection_title,metadata,evidence,name_caption,name_edit,source_caption,source_combo,save_button,details,status,privacy;
static HFONT body_font,title_font;
static HBRUSH background_brush,field_brush,card_brush;
static im_device devices[IM_MAX_DEVICES];
static int selected=-1,paused,background=1,observer_on,report_enabled;
static unsigned overflow_count,epoch=1,legacy_kd,legacy_ku,legacy_md,legacy_mu;
static DWORD identify_until,report_at;
static wchar_t config_file[1200],report_file[1200],report_temp[1200],note[256];
static UINT dpi=96;
static int px(int n){return MulDiv(n,(int)dpi,96);}
static void set_note(const wchar_t *text){wcsncpy(note,text,255);note[255]=0;}
static void move(HWND h,int x,int y,int w,int hgt){MoveWindow(h,x,y,w,hgt,TRUE);}
static LRESULT CALLBACK header_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data)
{
    (void)id;(void)data;
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);FillRect(dc,&r,card_brush);
        SelectObject(dc,body_font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,C_TEXT2);
        for(int i=0;i<Header_GetItemCount(hwnd);i++) {
            wchar_t text[128];HDITEMW item={0};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=128;
            Header_GetItem(hwnd,i,&item);Header_GetItemRect(hwnd,i,&r);r.left+=px(8);
            DrawTextW(dc,text,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        EndPaint(hwnd,&ps);return 0;
    }
    return DefSubclassProc(hwnd,msg,w,l);
}
static void draw_control(const DRAWITEMSTRUCT *d)
{
    HDC dc=d->hDC;RECT r=d->rcItem;SetBkMode(dc,TRANSPARENT);SelectObject(dc,body_font);
    int disabled=(d->itemState&ODS_DISABLED)!=0;SetTextColor(dc,disabled?C_TEXT3:C_TEXT);
    if(d->CtlType==ODT_COMBOBOX) {
        HBRUSH fill=CreateSolidBrush(d->itemState&ODS_SELECTED?C_CHIP:C_FIELD);FillRect(dc,&r,fill);DeleteObject(fill);wchar_t text[128]=L"";
        if(d->itemID!=(UINT)-1)SendMessageW(d->hwndItem,CB_GETLBTEXT,d->itemID,(LPARAM)text);
        r.left+=px(8);DrawTextW(dc,text,-1,&r,DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);return;
    }
    int check=d->CtlID==ID_BACKGROUND||d->CtlID==ID_OBSERVER;
    wchar_t text[128];GetWindowTextW(d->hwndItem,text,128);
    FillRect(dc,&r,background_brush);
    if(check) {
        int on=d->CtlID==ID_BACKGROUND?background:observer_on;
        RECT box={r.left+px(2),(r.bottom-px(16))/2,r.left+px(18),(r.bottom+px(16))/2};
        HBRUSH brush=CreateSolidBrush(on?C_TEXT:C_FIELD);HPEN pen=CreatePen(PS_SOLID,1,C_FIELD_BD);
        HGDIOBJ oldb=SelectObject(dc,brush),oldp=SelectObject(dc,pen);
        RoundRect(dc,box.left,box.top,box.right,box.bottom,px(4),px(4));
        SelectObject(dc,oldb);SelectObject(dc,oldp);DeleteObject(brush);DeleteObject(pen);
        if(on) {
            pen=CreatePen(PS_SOLID,px(2),C_BG);oldp=SelectObject(dc,pen);
            MoveToEx(dc,box.left+px(3),box.top+px(8),NULL);LineTo(dc,box.left+px(6),box.top+px(11));LineTo(dc,box.left+px(13),box.top+px(4));SelectObject(dc,oldp);DeleteObject(pen);
        }
        r.left=box.right+px(8);
    }else {
        HBRUSH brush=CreateSolidBrush((d->itemState&ODS_SELECTED)?C_SEC_H:C_SEC);HPEN pen=CreatePen(PS_SOLID,1,C_SEC_BD);
        HGDIOBJ oldb=SelectObject(dc,brush),oldp=SelectObject(dc,pen);
        RoundRect(dc,r.left,r.top,r.right,r.bottom,px(8),px(8));SelectObject(dc,oldb);SelectObject(dc,oldp);DeleteObject(brush);DeleteObject(pen);
    }
    DrawTextW(dc,text,-1,&r,DT_VCENTER|DT_SINGLELINE|(check?DT_LEFT:DT_CENTER));
    if((d->itemState&ODS_FOCUS)&&!(d->itemState&ODS_NOFOCUSRECT)){InflateRect(&r,-px(4),-px(4));DrawFocusRect(dc,&r);}
}
static void fonts(void)
{
    if(body_font)DeleteObject(body_font);if(title_font)DeleteObject(title_font);
    /* Native fallback keeps the portable tool readable without installing fonts. */
    body_font=CreateFontW(-px(14),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    title_font=CreateFontW(-px(24),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,(WPARAM)body_font,TRUE);
    SendMessageW(title,WM_SETFONT,(WPARAM)title_font,TRUE);
}
static HWND control(const wchar_t *cls,const wchar_t *text,DWORD style,int id)
{
    if(!_wcsicmp(cls,L"BUTTON"))style=(style&~(DWORD)BS_TYPEMASK)|BS_OWNERDRAW;
    if(!_wcsicmp(cls,L"EDIT"))style|=WS_BORDER;
    HWND h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    if(!_wcsicmp(cls,L"BUTTON"))SetWindowTheme(h,L"",L"");
    else if(!_wcsicmp(cls,L"EDIT")||!_wcsicmp(cls,L"COMBOBOX"))SetWindowTheme(h,L"DarkMode_CFD",NULL);
    SendMessageW(h,WM_SETFONT,(WPARAM)body_font,TRUE);return h;
}
static void layout(void)
{
    if(!list)return;RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom,p=px(24),gap=px(8),bh=px(32);
    move(title,p,px(16),w-2*p,px(36));move(subtitle,p,px(56),w-2*p,px(24));
    int x=p;move(pause_button,x,px(88),px(88),bh);x+=px(88)+gap;
    move(reset_button,x,px(88),px(112),bh);x+=px(112)+gap;
    move(refresh_button,x,px(88),px(136),bh);x+=px(136)+gap;
    move(identify_button,x,px(88),px(112),bh);x+=px(112)+px(24);
    move(background_box,x,px(88),w-x-p,bh);
    move(observer_box,p,px(128),px(256),px(32));move(observer_text,px(296),px(124),w-p-px(296),px(40));
    int table_y=px(176),table_h=h-table_y-px(264);if(table_h<px(100))table_h=px(100);
    move(list,p,table_y,w-2*p,table_h);
    int y=table_y+table_h+px(12);
    move(selection_title,p,y,w-2*p,px(24));
    move(metadata,p,y+px(28),w-2*p,px(24));move(evidence,p,y+px(56),w-2*p,px(40));
    move(name_caption,p,y+px(108),px(48),px(24));
    move(name_edit,p+px(56),y+px(104),w-px(536),px(32));
    move(source_caption,w-px(432),y+px(108),px(96),px(24));
    move(source_combo,w-px(336),y+px(104),px(192),px(200));
    move(save_button,w-p-px(112),y+px(104),px(112),px(32));
    move(details,p,y+px(144),w-2*p,px(48));
    move(status,p,h-px(52),w-2*p,px(24));move(privacy,p,h-px(28),w-2*p,px(24));
    const int widths[]={276,88,180,84,84,76,100};int sum=0;
    for(int i=1;i<7;i++)sum+=px(widths[i]);
    ListView_SetColumnWidth(list,0,(w-2*p-sum)>px(180)?w-2*p-sum:px(180));
    for(int i=1;i<7;i++)ListView_SetColumnWidth(list,i,px(widths[i]));
}
static void select_device(int index)
{
    selected=index;
    int valid=index>=0&&index<IM_MAX_DEVICES&&devices[index].used;
    EnableWindow(name_edit,valid);EnableWindow(source_combo,valid);EnableWindow(save_button,valid);
    if(!valid){SetWindowTextW(selection_title,L"Select a device to name it and inspect its counters");SetWindowTextW(name_edit,L"");return;}
    im_device *d=&devices[index];SetWindowTextW(selection_title,im_display_name(d));
    SetWindowTextW(name_edit,d->alias);SendMessageW(source_combo,CB_SETCURSEL,d->label,0);
    wchar_t text[512];swprintf(text,512,L"Windows name: %ls   •   %ls   •   %ls   •   ID %ls",
        d->name,d->manufacturer[0]?d->manufacturer:L"Manufacturer unknown",d->connected?L"Connected":L"Disconnected",d->id);
    SetWindowTextW(metadata,text);
    swprintf(text,512,L"%ls%ls%ls",d->evidence,d->pico?L" Pico label matches VID/PID; identity is not authenticated.":L"",
        d->label?L" Your source label is separate from this evidence.":L"");SetWindowTextW(evidence,text);
}
static void reset_counts(void)
{
    for(unsigned i=0;i<IM_MAX_DEVICES;i++)im_reset(&devices[i]);
    legacy_kd=legacy_ku=legacy_md=legacy_mu=0;epoch++;im_observer_reset();
}
static int register_input(void)
{
    DWORD flags=RIDEV_DEVNOTIFY|(background?RIDEV_INPUTSINK:0);
    RAWINPUTDEVICE input[2]={{1,2,flags,window},{1,6,flags,window}};
    return RegisterRawInputDevices(input,2,sizeof input[0])!=0;
}
static int device_for(HANDLE handle,unsigned keyboard)
{
    for(int i=0;i<IM_MAX_DEVICES;i++)if(devices[i].used&&devices[i].connected&&devices[i].handle==handle&&devices[i].keyboard==keyboard)return i;
    im_device candidate={0};candidate.handle=handle;candidate.keyboard=keyboard;
    candidate.used=candidate.connected=1;im_describe(&candidate);im_load_label(config_file,&candidate);
    int slot=-1;
    for(int i=0;i<IM_MAX_DEVICES;i++)if(devices[i].used&&!wcscmp(devices[i].id,candidate.id)&&devices[i].keyboard==keyboard){slot=i;break;}
    if(slot>=0){im_counts counts=devices[slot].counts;counts.held=0;memset(counts.keys,0,sizeof counts.keys);candidate.counts=counts;devices[slot]=candidate;return slot;}
    for(int i=0;i<IM_MAX_DEVICES;i++)if(!devices[i].used){slot=i;break;}
    if(slot<0){overflow_count++;set_note(L"Device capacity reached. Restart the monitor to remove old disconnected rows.");return -1;}
    devices[slot]=candidate;
    LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=slot;item.pszText=(LPWSTR)im_display_name(&devices[slot]);item.lParam=slot;
    ListView_InsertItem(list,&item);return slot;
}
static void enumerate_devices(void)
{
    UINT count=0;if(GetRawInputDeviceList(NULL,&count,sizeof(RAWINPUTDEVICELIST))==(UINT)-1)return;
    if(count>1024){set_note(L"Windows reported too many devices to enumerate.");return;}
    RAWINPUTDEVICELIST *items=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(count?count:1)*sizeof *items);
    if(!items)return;
    UINT got=GetRawInputDeviceList(items,&count,sizeof *items);
    if(got!=(UINT)-1) {
        unsigned char seen[IM_MAX_DEVICES]={0};
        for(UINT i=0;i<got;i++)if(items[i].dwType==RIM_TYPEMOUSE||items[i].dwType==RIM_TYPEKEYBOARD) {
            int index=device_for(items[i].hDevice,items[i].dwType==RIM_TYPEKEYBOARD);if(index>=0)seen[index]=1;
        }
        for(unsigned i=0;i<IM_MAX_DEVICES;i++)if(devices[i].used&&devices[i].connected&&devices[i].handle&&!seen[i])im_disconnect(&devices[i]);
    }
    HeapFree(GetProcessHeap(),0,items);
}
static void input(HRAWINPUT handle)
{
    if(paused)return;
    RAWINPUT r;UINT n=sizeof r;
    if(GetRawInputData(handle,RID_INPUT,&r,&n,sizeof(RAWINPUTHEADER))==(UINT)-1)return;
    if(r.header.dwType!=RIM_TYPEKEYBOARD&&r.header.dwType!=RIM_TYPEMOUSE)return;
    int index=device_for(r.header.hDevice,r.header.dwType==RIM_TYPEKEYBOARD);if(index<0)return;
    im_record(&devices[index].counts,&r);
    int pressed=r.header.dwType==RIM_TYPEKEYBOARD?!(r.data.keyboard.Flags&RI_KEY_BREAK):(r.data.mouse.usButtonFlags&0x155u)!=0;
    if(identify_until&&pressed) {
        if((int32_t)(identify_until-GetTickCount())>0) {
            ListView_SetItemState(list,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
            ListView_EnsureVisible(list,index,FALSE);select_device(index);set_note(L"Device identified. Give it a name, then Save label.");
        }
        identify_until=0;SetWindowTextW(identify_button,L"Identify");
    }
}
static void refresh_view(void)
{
    wchar_t text[768];unsigned connected=0,feeds=0;
    for(int i=0;i<IM_MAX_DEVICES;i++)if(devices[i].used) {
        im_device *d=&devices[i];connected+=d->connected&&d->handle!=NULL;feeds+=d->connected&&d->handle==NULL;
        ListView_SetItemText(list,i,0,(LPWSTR)im_display_name(d));
        ListView_SetItemText(list,i,1,d->keyboard?L"Keyboard":L"Mouse");
        ListView_SetItemText(list,i,2,(LPWSTR)im_source_name(d));
        swprintf(text,768,L"%llu",(unsigned long long)d->counts.down);ListView_SetItemText(list,i,3,text);
        swprintf(text,768,L"%llu",(unsigned long long)d->counts.up);ListView_SetItemText(list,i,4,text);
        if(d->keyboard)swprintf(text,768,L"%u keys",d->counts.held);
        else if(!d->counts.held)wcscpy(text,L"Released");else swprintf(text,768,L"0x%02X",d->counts.held);
        ListView_SetItemText(list,i,5,text);ListView_SetItemText(list,i,6,d->connected?(!d->handle?L"Unknown feed":d->counts.held?L"Held":L"Connected"):L"Disconnected");
    }
    if(selected>=0&&devices[selected].used) {
        im_device *d=&devices[selected];im_counts *c=&d->counts;
        if(d->keyboard)swprintf(text,768,L"Presses %llu   Releases %llu   Typematic repeats %llu   Held %u   Unmatched releases %llu\r\nDriver: %ls   •   Reset counts only after releasing inputs.",
            (unsigned long long)c->down,(unsigned long long)c->up,(unsigned long long)c->repeat,c->held,(unsigned long long)c->unmatched,d->service[0]?d->service:L"Not reported");
        else swprintf(text,768,L"Motion X %lld / Y %lld   •   Wheel %lld / Horizontal %lld (120 per notch)   •   Unmatched releases %llu\r\nHeld bits: Left=1 Right=2 Middle=4 X1=8 X2=16   •   Driver: %ls",
            (long long)c->dx,(long long)c->dy,(long long)c->wheel,(long long)c->pan,(unsigned long long)c->unmatched,d->service[0]?d->service:L"Not reported");
        SetWindowTextW(details,text);
    }
    if(observer_on) {
        im_counts k,m;LONG ks,ms;im_observer_snapshot(&k,&m);im_observer_seen(&ks,&ms);
        swprintf(text,768,L"Injected: keys %llu/%llu • mouse %llu/%llu. Hook events seen: K%ld / M%ld. Separate totals.",
            (unsigned long long)k.down,(unsigned long long)k.up,(unsigned long long)m.down,(unsigned long long)m.up,ks,ms);
    }else wcscpy(text,L"Windows injection flags are a separate view, without per-device or per-app attribution.");
    SetWindowTextW(observer_text,text);
    swprintf(text,768,L"%ls   •   %u devices / %u unidentified feeds   •   %ls   •   %ls",paused?L"PAUSED":L"LIVE",connected,feeds,
        background?L"Background counting ON":L"Foreground only",note[0]?note:L"Select a row or use Identify.");SetWindowTextW(status,text);
    if(identify_until&&(int32_t)(GetTickCount()-identify_until)>=0){identify_until=0;SetWindowTextW(identify_button,L"Identify");set_note(L"Identify timed out. Click Identify and then press a device button/key.");}
}
static void write_report(void)
{
    if(!report_enabled||GetTickCount()-report_at<500)return;report_at=GetTickCount();
    im_counts sums[2]={{0}};
    for(unsigned i=0;i<IM_MAX_DEVICES;i++)if(devices[i].used) {
        im_device *d=&devices[i];im_counts *s=&sums[d->pico?1:0],*c=&d->counts;
        /* Separate key/mouse fields are emitted below; movement is mouse-only. */
        if(!d->keyboard){s->dx+=c->dx;s->dy+=c->dy;s->wheel+=c->wheel;s->pan+=c->pan;}
    }
    FILE *f=_wfopen(report_temp,L"w");if(!f){set_note(L"Cannot write the optional numeric report beside this executable.");return;}
    fprintf(f,"{\"schema\":2,\"tick_ms\":%llu,\"epoch\":%u,\"foreground\":%s,\"background\":%s,\"paused\":%s,",
        (unsigned long long)GetTickCount64(),epoch,GetForegroundWindow()==window?"true":"false",background?"true":"false",paused?"true":"false");
    for(unsigned group=0;group<2;group++) {
        uint64_t kd=0,ku=0,kr=0,km=0,md=0,mu=0;unsigned kh=0,mh=0;
        for(unsigned i=0;i<IM_MAX_DEVICES;i++)if(devices[i].used&&devices[i].pico==group) {
            im_device *d=&devices[i];im_counts *c=&d->counts;
            if(d->keyboard){kd+=c->down;ku+=c->up;kr+=c->repeat;kh+=c->held;km+=c->unmatched;}
            else{md+=c->down;mu+=c->up;mh|=c->held;}
        }
        fprintf(f,"\"%s\":{\"key_down\":%llu,\"key_up\":%llu,\"key_held\":%u,\"unmatched_up\":%llu,\"mouse_down\":%llu,\"mouse_up\":%llu,\"mouse_held\":%u,\"repeat\":%llu,\"dx\":%lld,\"dy\":%lld,\"wheel_units\":%lld,\"pan_units\":%lld},",
            group?"pico":"other",(unsigned long long)kd,(unsigned long long)ku,kh,(unsigned long long)km,(unsigned long long)md,(unsigned long long)mu,mh,(unsigned long long)kr,
            (long long)sums[group].dx,(long long)sums[group].dy,(long long)sums[group].wheel,(long long)sums[group].pan);
    }
    fprintf(f,"\"legacy\":{\"key_down\":%u,\"key_up\":%u,\"mouse_down\":%u,\"mouse_up\":%u},\"device_overflow\":%u}\n",legacy_kd,legacy_ku,legacy_md,legacy_mu,overflow_count);
    int ok=!ferror(f);if(fclose(f))ok=0;
    if(ok&&!MoveFileExW(report_temp,report_file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))set_note(L"Numeric report update failed; prior snapshot retained.");
}
static void create_controls(void)
{
    title=control(L"STATIC",L"HIDway  /  Input monitor",0,0);
    subtitle=control(L"STATIC",L"Know your devices. Compare presses and releases. Keep names between sessions.",0,0);
    pause_button=control(L"BUTTON",L"Pause",BS_PUSHBUTTON|WS_TABSTOP,ID_PAUSE);
    reset_button=control(L"BUTTON",L"Reset counts",BS_PUSHBUTTON|WS_TABSTOP,ID_RESET);
    refresh_button=control(L"BUTTON",L"Refresh devices",BS_PUSHBUTTON|WS_TABSTOP,ID_REFRESH);
    identify_button=control(L"BUTTON",L"Identify",BS_PUSHBUTTON|WS_TABSTOP,ID_IDENTIFY);
    background_box=control(L"BUTTON",L"Count in background",BS_AUTOCHECKBOX|WS_TABSTOP,ID_BACKGROUND);
    observer_box=control(L"BUTTON",L"Observe injected events",BS_AUTOCHECKBOX|WS_TABSTOP,ID_OBSERVER);
    SendMessageW(background_box,BM_SETCHECK,BST_CHECKED,0);
    observer_text=control(L"STATIC",L"",0,0);
    list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,ID_LIST);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    ListView_SetBkColor(list,C_CARD);ListView_SetTextBkColor(list,C_CARD);ListView_SetTextColor(list,C_TEXT);SetWindowTheme(list,L"DarkMode_Explorer",NULL);
    SetWindowSubclass(ListView_GetHeader(list),header_proc,1,0);
    const wchar_t *columns[]={L"Device name",L"Kind",L"Source / connection",L"Down",L"Up",L"Held",L"State"};
    for(int i=0;i<7;i++){LVCOLUMNW column={0};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=(LPWSTR)columns[i];column.cx=100;ListView_InsertColumn(list,i,&column);}
    selection_title=control(L"STATIC",L"Select a device to name it and inspect its counters",0,0);
    metadata=control(L"STATIC",L"",SS_ENDELLIPSIS,0);evidence=control(L"STATIC",L"Unknown is kept separate from virtual; your labels remain visibly manual.",0,0);
    name_caption=control(L"STATIC",L"Name",0,0);name_edit=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,ID_NAME);SendMessageW(name_edit,EM_SETLIMITTEXT,IM_NAME_LEN-1,0);
    source_caption=control(L"STATIC",L"Source label",0,0);source_combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL|WS_TABSTOP,ID_SOURCE);
    const wchar_t *labels[]={L"Automatic (Windows)",L"Physical (my label)",L"Virtual (my label)",L"Touchpad (my label)"};
    for(unsigned i=0;i<4;i++)SendMessageW(source_combo,CB_ADDSTRING,0,(LPARAM)labels[i]);SendMessageW(source_combo,CB_SETCURSEL,0,0);
    save_button=control(L"BUTTON",L"Save label",BS_PUSHBUTTON|WS_TABSTOP,ID_SAVE);details=control(L"STATIC",L"",0,0);
    status=control(L"STATIC",L"",SS_ENDELLIPSIS,0);
    privacy=control(L"STATIC",report_enabled?L"Numeric report ON • no typed text/key history saved • no network, blocking or input injection.":L"No typed text/key history or counts saved • names stay local • no network, blocking or input injection.",SS_ENDELLIPSIS,0);
    fonts();select_device(-1);layout();
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l)
{
    switch(msg) {
    case WM_CREATE:window=hwnd;dpi=GetDpiForWindow(hwnd);create_controls();if(!register_input())return -1;enumerate_devices();SetTimer(hwnd,1,200,NULL);return 0;
    case WM_SIZE:layout();return 0;
    case WM_DPICHANGED: {
        dpi=HIWORD(w);RECT *r=(RECT *)l;SetWindowPos(hwnd,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);fonts();layout();return 0;
    }
    case WM_GETMINMAXINFO: {
        RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        int mw=work.right-work.left-24,mh=work.bottom-work.top-24;
        ((MINMAXINFO *)l)->ptMinTrackSize.x=px(960)<mw?px(960):mw;
        ((MINMAXINFO *)l)->ptMinTrackSize.y=px(640)<mh?px(640):mh;return 0;
    }
    case WM_ACTIVATE:
        if(!background){reset_counts();im_observer_pause(paused||LOWORD(w)==WA_INACTIVE);set_note(L"Foreground boundary changed; a fresh count epoch started.");}return 0;
    case WM_INPUT:input((HRAWINPUT)l);return DefWindowProcW(hwnd,msg,w,l);
    case WM_INPUT_DEVICE_CHANGE:
        if(w==GIDC_REMOVAL)for(unsigned i=0;i<IM_MAX_DEVICES;i++)if(devices[i].used&&devices[i].handle==(HANDLE)l){im_disconnect(&devices[i]);set_note(L"Device disconnected; held state cleared without inventing releases.");}
        if(w==GIDC_ARRIVAL)enumerate_devices();if(selected>=0)select_device(selected);return 0;
    case WM_TIMER:refresh_view();write_report();return 0;
    case WM_NOTIFY: {
        NMHDR *header=(NMHDR *)l;
        if(header->idFrom==ID_LIST&&header->code==NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW *draw=(NMLVCUSTOMDRAW *)l;
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
                int on=(ListView_GetItemState(list,(int)draw->nmcd.dwItemSpec,LVIS_SELECTED)&LVIS_SELECTED)!=0;
                draw->clrText=C_TEXT;draw->clrTextBk=on?C_CHIP:C_CARD;draw->nmcd.uItemState&=~(UINT)CDIS_SELECTED;return CDRF_NEWFONT;
            }
        }
        if(header->idFrom==ID_LIST&&header->code==LVN_ITEMCHANGED){NMLISTVIEW *v=(NMLISTVIEW *)l;if((v->uChanged&LVIF_STATE)&&(v->uNewState&LVIS_SELECTED))select_device(v->iItem);}return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(w)) {
        case ID_PAUSE:paused=!paused;im_observer_pause(paused);reset_counts();SetWindowTextW(pause_button,paused?L"Resume":L"Pause");set_note(L"Pause/resume starts a fresh count epoch; device labels are retained.");break;
        case ID_RESET:reset_counts();set_note(L"Counts reset. Release all inputs before starting a fresh test.");break;
        case ID_REFRESH:enumerate_devices();set_note(L"Device list refreshed; saved names retained.");break;
        case ID_IDENTIFY:
            if(paused){set_note(L"Resume counting before using Identify.");break;}
            identify_until=GetTickCount()+15000;SetWindowTextW(identify_button,L"Waiting…");set_note(L"Press a key/button on the device within 15 seconds. Motion alone does not identify it.");break;
        case ID_BACKGROUND: {
            int old=background;background=!background;
            if(!register_input()){background=old;SendMessageW(background_box,BM_SETCHECK,old?BST_CHECKED:BST_UNCHECKED,0);set_note(L"Windows did not accept the counting-mode change.");}
            else{reset_counts();set_note(L"Counting mode changed; a fresh count epoch started.");}InvalidateRect(background_box,NULL,TRUE);break;
        }
        case ID_OBSERVER:
            observer_on=!observer_on;
            if(observer_on&&!im_observer_start()){observer_on=0;SendMessageW(observer_box,BM_SETCHECK,BST_UNCHECKED,0);set_note(L"Injected-event observation could not start. Raw device counting is still available.");}
            else if(!observer_on)im_observer_stop();
            im_observer_reset();im_observer_pause(paused);InvalidateRect(observer_box,NULL,TRUE);break;
        case ID_SAVE:
            if(selected>=0) {
                im_device updated=devices[selected];GetWindowTextW(name_edit,updated.alias,IM_NAME_LEN);
                LRESULT label=SendMessageW(source_combo,CB_GETCURSEL,0,0);updated.label=label>=0&&label<=3?(unsigned)label:0;
                if(im_save_label(config_file,&updated)){devices[selected]=updated;select_device(selected);set_note(L"Name and source label saved beside the executable.");}
                else set_note(L"Could not save labels. Choose a writable folder for the portable monitor.");
            }break;
        }
        return 0;
    case WM_DRAWITEM:draw_control((const DRAWITEMSTRUCT *)l);return TRUE;
    case WM_MEASUREITEM:((MEASUREITEMSTRUCT *)l)->itemHeight=(UINT)px(24);return TRUE;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor((HDC)w,C_TEXT);SetBkColor((HDC)w,C_FIELD);return (LRESULT)field_brush;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetTextColor((HDC)w,(HWND)l==title||(HWND)l==selection_title?C_TEXT:C_TEXT2);SetBkColor((HDC)w,C_BG);return (LRESULT)background_brush;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:if(!paused&&!(l&(1L<<30)))legacy_kd++;return 0;
    case WM_KEYUP:case WM_SYSKEYUP:if(!paused)legacy_ku++;return 0;
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:case WM_XBUTTONDOWN:if(!paused)legacy_md++;return 0;
    case WM_LBUTTONUP:case WM_RBUTTONUP:case WM_MBUTTONUP:case WM_XBUTTONUP:if(!paused)legacy_mu++;return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,1);im_observer_stop();if(body_font)DeleteObject(body_font);if(title_font)DeleteObject(title_font);DeleteObject(background_brush);DeleteObject(field_brush);DeleteObject(card_brush);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command,int show)
{
    (void)previous;report_enabled=wcscmp(command,L"--aggregate-report")==0;
    HANDLE instance_guard=NULL;
    if(wcscmp(command,L"--smoke-test")) {
        instance_guard=CreateMutexW(NULL,TRUE,L"Local\\HIDwayInputMonitorOwner");
        if(!instance_guard)return 2;
        if(GetLastError()==ERROR_ALREADY_EXISTS) {
            HWND existing=FindWindowW(L"HIDwayInputMonitor",NULL);
            if(existing){ShowWindowAsync(existing,SW_RESTORE);SetForegroundWindow(existing);}
            CloseHandle(instance_guard);return 0;
        }
    }
    wchar_t folder[1024];DWORD length=GetModuleFileNameW(NULL,folder,1024);if(!length||length>=1024)return 2;
    wchar_t *last=wcsrchr(folder,L'\\');if(!last)return 2;*last=0;
    swprintf(config_file,1200,L"%ls\\hidway-input-monitor.ini",folder);swprintf(report_file,1200,L"%ls\\aggregate-counts.json",folder);swprintf(report_temp,1200,L"%ls\\aggregate-counts.tmp",folder);
    INITCOMMONCONTROLSEX common={sizeof common,ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
    background_brush=CreateSolidBrush(C_BG);field_brush=CreateSolidBrush(C_FIELD);card_brush=CreateSolidBrush(C_CARD);
    WNDCLASSW cls={0};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"HIDwayInputMonitor";cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hbrBackground=background_brush;cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(2));
    if(!RegisterClassW(&cls))return 2;
    RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=work.right-work.left-48;if(width>1320)width=1320;
    int height=work.bottom-work.top-48;if(height>900)height=900;
    HWND hwnd=CreateWindowExW(0,cls.lpszClassName,L"HIDway - Input monitor",WS_OVERLAPPEDWINDOW,
        work.left+24,work.top+24,width,height,NULL,NULL,instance,NULL);
    if(!hwnd)return 3;
    BOOL dark=TRUE;COLORREF caption=C_BG,border=C_CARD_BD,caption_text=C_TEXT;
    DwmSetWindowAttribute(hwnd,20,&dark,sizeof dark);DwmSetWindowAttribute(hwnd,35,&caption,sizeof caption);
    DwmSetWindowAttribute(hwnd,34,&border,sizeof border);DwmSetWindowAttribute(hwnd,36,&caption_text,sizeof caption_text);
    if(wcscmp(command,L"--smoke-test")==0){DestroyWindow(hwnd);return 0;}
    ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(instance_guard)CloseHandle(instance_guard);return 0;
}
