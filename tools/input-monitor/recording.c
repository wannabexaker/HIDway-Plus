#include "recording.h"
#include <io.h>
#include <string.h>
#include <wchar.h>
#define WRITER_QUEUE 4096u
typedef struct { hw_rec_event event; char name[512],source[256],id[32],description[256],windows_name[512],evidence[1024]; } entry;
static struct {
    SRWLOCK lock;entry queue[WRITER_QUEUE];unsigned head,count,total;
    HANDLE thread,wake;int stopping,failed,state;uint32_t duration;
    FILE *data,*csv;wchar_t final[2048],partial[2080],csvfinal[2080],csvpartial[2112];
} writer;
void im_event_name(const hw_rec_event *e,wchar_t *text,unsigned capacity)
{
    if(e->type<=HW_REC_KEY_UP) {
        wchar_t name[96]=L"";LONG scan=(LONG)(e->code<<16)|((e->flags&RI_KEY_E0)?1L<<24:0);
        if(e->vk==VK_PAUSE)wcscpy(name,L"Pause");
        else if(e->vk==VK_NUMLOCK)wcscpy(name,L"Num Lock");
        else if(e->vk==VK_SNAPSHOT)wcscpy(name,L"Print Screen");
        else if(e->vk==VK_PACKET)wcscpy(name,L"Unicode packet (not HID-replayable)");
        else if(e->code>255)swprintf(name,96,L"Raw scan %04X (not HID-replayable)",e->code);
        else {
            if(!e->code){UINT mapped=MapVirtualKeyExW(e->vk,MAPVK_VK_TO_VSC_EX,GetKeyboardLayout(0));scan=(LONG)((mapped&255u)<<16)|((mapped&0xff00u)==0xe000u?1L<<24:0);}
            GetKeyNameTextW(scan,name,96);
        }
        if(!name[0])swprintf(name,96,L"VK 0x%02X",e->vk);
        swprintf(text,capacity,L"%ls %ls | scan %02X E0=%u E1=%u VK=%02X",name,e->type==HW_REC_KEY_DOWN?L"DOWN":L"UP",e->code,!!(e->flags&RI_KEY_E0),!!(e->flags&RI_KEY_E1),e->vk);
    } else if(e->type<=HW_REC_BUTTON_UP) {
        const wchar_t *names[]={L"Left",L"Right",L"Middle / wheel press",L"X1 / Back",L"X2 / Forward"};
        swprintf(text,capacity,L"%ls %ls",names[e->code],e->type==HW_REC_BUTTON_DOWN?L"DOWN":L"UP");
    } else if(e->type==HW_REC_RELEASE_DEVICE)swprintf(text,capacity,L"Device disconnected / release its held state");
    else if(e->type==HW_REC_WHEEL||e->type==HW_REC_PAN)
        swprintf(text,capacity,L"%ls %+d units (120/notch)",e->type==HW_REC_WHEEL?L"Wheel rotation":L"Horizontal wheel",e->x);
    else swprintf(text,capacity,L"%ls X=%d Y=%d%ls",e->type==HW_REC_MOVE?L"Relative motion":L"Absolute motion",e->x,e->y,e->flags?L" virtual desktop":L"");
}
void im_raw_events(const RAWINPUT *r,unsigned device,void (*emit)(const hw_rec_event *,void *),void *context)
{
    hw_rec_event e={0};e.device=device;
    if(r->header.dwType==RIM_TYPEKEYBOARD) {
        e.type=r->data.keyboard.Flags&RI_KEY_BREAK?HW_REC_KEY_UP:HW_REC_KEY_DOWN;e.code=r->data.keyboard.MakeCode;
        e.flags=r->data.keyboard.Flags&(RI_KEY_E0|RI_KEY_E1);e.vk=r->data.keyboard.VKey;
        if(hw_rec_valid(&e))emit(&e,context);return;
    }
    if(r->header.dwType!=RIM_TYPEMOUSE)return;
    const RAWMOUSE *m=&r->data.mouse;
    if(m->lLastX||m->lLastY||(m->usFlags&MOUSE_MOVE_ABSOLUTE)) {
        e.type=m->usFlags&MOUSE_MOVE_ABSOLUTE?HW_REC_ABSOLUTE:HW_REC_MOVE;e.flags=(e.type==HW_REC_ABSOLUTE&&(m->usFlags&MOUSE_VIRTUAL_DESKTOP))?1u:0u;
        e.x=m->lLastX;e.y=m->lLastY;if(hw_rec_valid(&e))emit(&e,context);
    }
    e.x=e.y=0;e.flags=0;
    for(unsigned i=0;i<5;i++) {
        e.code=i;if(m->usButtonFlags&(1u<<(i*2))){e.type=HW_REC_BUTTON_DOWN;emit(&e,context);}
        if(m->usButtonFlags&(2u<<(i*2))){e.type=HW_REC_BUTTON_UP;emit(&e,context);}
    }
    e.code=0;e.x=(SHORT)m->usButtonData;
    if(m->usButtonFlags&RI_MOUSE_WHEEL){e.type=HW_REC_WHEEL;emit(&e,context);}
    if(m->usButtonFlags&RI_MOUSE_HWHEEL){e.type=HW_REC_PAN;emit(&e,context);}
}
static void csv_text(FILE *f,const char *s)
{
    const char *first=s;while(*first&&(unsigned char)*first<=32)first++;
    fputc('"',f);if(*first&&strchr("=+-@",*first))fputc('\'',f);
    for(;*s;s++){if(*s=='"')fputc('"',f);fputc(*s,f);}fputc('"',f);
}
static DWORD WINAPI write_thread(void *arg)
{
    (void)arg;
    for(;;) {
        entry item;int got=0,stop=0,failed=0;
        AcquireSRWLockExclusive(&writer.lock);
        if(writer.count){item=writer.queue[writer.head];writer.head=(writer.head+1)%WRITER_QUEUE;writer.count--;got=1;}
        stop=writer.stopping;failed=writer.failed;ReleaseSRWLockExclusive(&writer.lock);
        if(failed)break;
        if(!got){if(stop)break;WaitForSingleObject(writer.wake,1000);continue;}
        const hw_rec_event *e=&item.event;
        int ok=hw_rec_write(writer.data,e);
        fprintf(writer.csv,"%u,%u,%u,%u,%u,%d,%d,%u,",e->ms,e->device,e->type,e->code,e->flags,e->x,e->y,e->vk);
        csv_text(writer.csv,item.id);fputc(',',writer.csv);csv_text(writer.csv,item.name);fputc(',',writer.csv);csv_text(writer.csv,item.source);fputc(',',writer.csv);csv_text(writer.csv,item.description);fputc(',',writer.csv);csv_text(writer.csv,item.windows_name);fputc(',',writer.csv);csv_text(writer.csv,item.evidence);fputc('\n',writer.csv);
        if(!ok||ferror(writer.csv)){AcquireSRWLockExclusive(&writer.lock);writer.failed=1;ReleaseSRWLockExclusive(&writer.lock);break;}
    }
    AcquireSRWLockExclusive(&writer.lock);int ok=!writer.failed;ReleaseSRWLockExclusive(&writer.lock);
    if(ok)ok=hw_rec_footer(writer.data,writer.total,writer.duration);
    if(fflush(writer.data)||fflush(writer.csv)||_commit(_fileno(writer.data))||_commit(_fileno(writer.csv)))ok=0;
    if(fclose(writer.data))ok=0;if(fclose(writer.csv))ok=0;writer.data=writer.csv=NULL;
    if(ok&&!MoveFileExW(writer.csvpartial,writer.csvfinal,MOVEFILE_WRITE_THROUGH))ok=0;
    if(ok&&!MoveFileExW(writer.partial,writer.final,MOVEFILE_WRITE_THROUGH))ok=0;
    AcquireSRWLockExclusive(&writer.lock);writer.failed=!ok;ReleaseSRWLockExclusive(&writer.lock);return 0;
}
int im_record_start(const wchar_t *path,const hw_rec_screen *screen)
{
    if(writer.thread||wcslen(path)>2000)return 0;
    memset(&writer,0,sizeof writer);InitializeSRWLock(&writer.lock);
    swprintf(writer.final,2048,L"%ls",path);swprintf(writer.partial,2080,L"%ls.partial",path);
    swprintf(writer.csvfinal,2080,L"%ls.csv",path);swprintf(writer.csvpartial,2112,L"%ls.csv.partial",path);
    /* Exclusive creation prevents accidental replacement, including stale partials. */
    writer.data=_wfopen(writer.partial,L"wbx");if(!writer.data)return 0;
    writer.csv=_wfopen(writer.csvpartial,L"wbx");
    if(!writer.csv){fclose(writer.data);writer.data=NULL;DeleteFileW(writer.partial);return 0;}
    writer.wake=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(!writer.wake){fclose(writer.data);fclose(writer.csv);writer.data=writer.csv=NULL;return 0;}
    int ok=hw_rec_header(writer.data,screen);
    fputs("\xEF\xBB\xBFms,device_slot,type,scan_or_button,flags,x_or_wheel,y,vk,device_id,device_name,source,event,windows_name,windows_evidence\n",writer.csv);
    if(ok)writer.thread=CreateThread(NULL,0,write_thread,NULL,0,NULL);
    if(!writer.thread){fclose(writer.data);fclose(writer.csv);CloseHandle(writer.wake);writer.data=writer.csv=NULL;return 0;}
    writer.state=1;return 1;
}
int im_record_push(const hw_rec_event *event,const im_device *device)
{
    entry item={0};item.event=*event;wchar_t description[256];im_event_name(event,description,256);
    WideCharToMultiByte(CP_UTF8,0,im_display_name(device),-1,item.name,sizeof item.name,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,im_source_name(device),-1,item.source,sizeof item.source,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,device->id,-1,item.id,sizeof item.id,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,description,-1,item.description,sizeof item.description,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,device->name,-1,item.windows_name,sizeof item.windows_name,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,device->evidence,-1,item.evidence,sizeof item.evidence,NULL,NULL);
    AcquireSRWLockExclusive(&writer.lock);
    int ok=writer.state==1&&!writer.failed&&!writer.stopping&&hw_rec_valid(event)&&writer.total<HW_REC_MAX_EVENTS&&writer.count<WRITER_QUEUE;
    if(ok){writer.queue[(writer.head+writer.count)%WRITER_QUEUE]=item;writer.count++;writer.total++;}
    else writer.failed=1;
    ReleaseSRWLockExclusive(&writer.lock);SetEvent(writer.wake);return ok;
}
void im_record_stop(uint32_t duration)
{
    if(writer.state!=1)return;AcquireSRWLockExclusive(&writer.lock);writer.duration=duration;writer.stopping=1;writer.state=2;ReleaseSRWLockExclusive(&writer.lock);SetEvent(writer.wake);
}
void im_record_abort(void){AcquireSRWLockExclusive(&writer.lock);writer.failed=1;ReleaseSRWLockExclusive(&writer.lock);if(writer.wake)SetEvent(writer.wake);}
int im_record_poll(void)
{
    if(writer.thread&&WaitForSingleObject(writer.thread,0)==WAIT_OBJECT_0) {
        CloseHandle(writer.thread);CloseHandle(writer.wake);writer.thread=writer.wake=NULL;writer.state=writer.failed?-1:3;
    }
    return writer.state;
}
unsigned im_record_count(void){AcquireSRWLockShared(&writer.lock);unsigned n=writer.total;ReleaseSRWLockShared(&writer.lock);return n;}
