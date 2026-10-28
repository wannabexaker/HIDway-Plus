#include "model.h"
#include <wchar.h>
#include <wctype.h>
#include <string.h>
void im_identity(const wchar_t *path,unsigned keyboard,wchar_t out[24])
{
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const wchar_t *s=path;*s;s++) {
        unsigned c=(unsigned)towupper(*s);
        hash^=c&255u;hash*=UINT64_C(1099511628211);
        hash^=c>>8;hash*=UINT64_C(1099511628211);
    }
    hash^=keyboard?1u:0u;hash*=UINT64_C(1099511628211);
    swprintf(out,24,L"%016llX",(unsigned long long)hash);
}
void im_record(im_counts *c,const RAWINPUT *r)
{
    if(r->header.dwType==RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD *k=&r->data.keyboard;
        unsigned code=(k->MakeCode&255u)|((k->Flags&RI_KEY_E0)?256u:0u)|((k->Flags&RI_KEY_E1)?512u:0u);
        if(!k->MakeCode)code=768u+(k->VKey&255u);
        if(k->Flags&RI_KEY_BREAK) {
            c->up++;if(c->keys[code]){c->keys[code]=0;c->held--;}else c->unmatched++;
        }else if(c->keys[code])c->repeat++;
        else {c->keys[code]=1;c->down++;c->held++;}
    }else if(r->header.dwType==RIM_TYPEMOUSE) {
        const RAWMOUSE *m=&r->data.mouse;
        if(!(m->usFlags&MOUSE_MOVE_ABSOLUTE)){c->dx+=m->lLastX;c->dy+=m->lLastY;}
        if(m->usButtonFlags&RI_MOUSE_WHEEL)c->wheel+=(SHORT)m->usButtonData;
        if(m->usButtonFlags&RI_MOUSE_HWHEEL)c->pan+=(SHORT)m->usButtonData;
        for(unsigned i=0;i<5;i++) {
            unsigned bit=1u<<i;
            if(m->usButtonFlags&(1u<<(2*i))){c->down++;c->held|=bit;}
            if(m->usButtonFlags&(2u<<(2*i))){c->up++;if(!(c->held&bit))c->unmatched++;c->held&=~bit;}
        }
    }
}
void im_reset(im_device *d){memset(&d->counts,0,sizeof d->counts);}
void im_disconnect(im_device *d)
{
    d->connected=0;d->handle=NULL;d->counts.held=0;
    memset(d->counts.keys,0,sizeof d->counts.keys);
}
const wchar_t *im_display_name(const im_device *d){return d->alias[0]?d->alias:d->name;}
const wchar_t *im_source_name(const im_device *d)
{
    if(d->label==IM_LABEL_PHYSICAL)return L"Physical (your label)";
    if(d->label==IM_LABEL_VIRTUAL)return L"Virtual (your label)";
    if(d->label==IM_LABEL_TOUCHPAD)return L"Touchpad (your label)";
    switch(d->source) {
    case IM_USB:return L"USB device";
    case IM_BLUETOOTH:return L"Bluetooth device";
    case IM_BUILTIN:return L"Built-in device";
    case IM_VIRTUAL:return L"Virtual driver";
    default:return L"Unknown";
    }
}
static int contains(const wchar_t *s,const wchar_t *part)
{
    wchar_t upper[1024];wcsncpy(upper,s,1023);upper[1023]=0;
    for(wchar_t *p=upper;*p;p++)*p=(wchar_t)towupper(*p);
    return wcsstr(upper,part)!=NULL;
}
unsigned im_metadata_source(const wchar_t *instance,const wchar_t *bus,const wchar_t *description)
{
    if(contains(instance,L"RDP_MOU")||contains(instance,L"RDP_KBD")||
       contains(instance,L"VIRTUAL")||contains(description,L"VIRTUAL")||
       contains(description,L"VHF"))return IM_VIRTUAL;
    if(contains(bus,L"BTH")||contains(instance,L"BTHENUM\\")||contains(instance,L"BTHLEDEVICE\\"))return IM_BLUETOOTH;
    if(!_wcsicmp(bus,L"USB")||!_wcsnicmp(instance,L"USB\\",4))return IM_USB;
    if(!_wcsicmp(bus,L"ACPI")||contains(description,L"I8042PRT"))return IM_BUILTIN;
    return IM_UNKNOWN; /* ROOT alone does not prove virtual input. */
}
