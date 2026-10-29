#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "replay.h"
#include "keymap_sc2hid.h"
#include <string.h>
int hidway_replay_binding(const hw_rec_event *e,uint16_t *binding)
{
    *binding=0;if(e->type>HW_REC_KEY_UP)return 1;
    if(e->vk==VK_PACKET||e->code>255)return 0;
    if(e->vk==VK_PAUSE||e->vk==VK_NUMLOCK||e->vk==VK_SNAPSHOT){*binding=(uint16_t)(e->vk==VK_PAUSE?0x48:e->vk==VK_NUMLOCK?0x53:0x46);return 1;}
    unsigned scan=e->code;int e0=(e->flags&RI_KEY_E0)!=0;
    if(e->flags&RI_KEY_E1)return 0;
    if(!scan) {
        UINT mapped=MapVirtualKeyExW(e->vk,MAPVK_VK_TO_VSC_EX,GetKeyboardLayout(0));
        scan=mapped&0xffu;e0=(mapped&0xff00u)==0xe000u;
        switch(e->vk){case VK_LEFT:case VK_RIGHT:case VK_UP:case VK_DOWN:case VK_HOME:case VK_END:case VK_PRIOR:case VK_NEXT:case VK_INSERT:case VK_DELETE:case VK_DIVIDE:e0=1;break;}
    }
    if(e->vk==VK_RSHIFT)e0=0;
    if(!scan||scan>255)return 0;
    uint8_t mod=hidway_sc_to_modifier((uint8_t)scan,e0!=0);
    *binding=mod?(uint16_t)(HIDWAY_BIND_MOD|mod):hidway_sc_to_usage((uint8_t)scan,e0!=0);
    return *binding!=0;
}
int hidway_replay_apply(hidway_replay_state *s,const hw_rec_screen *screen,const hw_rec_event *e)
{
    if(!hw_rec_valid(e))return 0;unsigned d=e->device;
    if(e->type<=HW_REC_KEY_UP) {
        if(e->vk==VK_PACKET||e->code>255)return 0;
        unsigned id=e->code?(e->code+((e->flags&RI_KEY_E0)?256u:(e->flags&RI_KEY_E1)?512u:0u)):768u+e->vk;
        if(e->type==HW_REC_KEY_UP)s->bindings[d][id]=0;
        else if(!s->bindings[d][id]&&!hidway_replay_binding(e,&s->bindings[d][id]))return 0;
    } else if(e->type<=HW_REC_BUTTON_UP) {
        uint8_t bit=(uint8_t)(1u<<e->code);
        if(e->type==HW_REC_BUTTON_DOWN)s->buttons[d]|=bit;else s->buttons[d]&=(uint8_t)~bit;
    } else if(e->type==HW_REC_RELEASE_DEVICE) {
        memset(s->bindings[d],0,sizeof s->bindings[d]);s->buttons[d]=0;s->absolute_valid[d]=0;
    } else if(e->type==HW_REC_MOVE) {
        s->frame.x=(int32_t)((uint32_t)s->frame.x+(uint32_t)e->x);s->frame.y=(int32_t)((uint32_t)s->frame.y+(uint32_t)e->y);
    } else if(e->type==HW_REC_ABSOLUTE) {
        int32_t x=(int32_t)((int64_t)e->x*(e->flags?screen->virtual_width:screen->width)/65535);
        int32_t y=(int32_t)((int64_t)e->y*(e->flags?screen->virtual_height:screen->height)/65535);
        if(s->absolute_valid[d]){s->frame.x=(int32_t)((uint32_t)s->frame.x+(uint32_t)(x-s->absolute_x[d]));s->frame.y=(int32_t)((uint32_t)s->frame.y+(uint32_t)(y-s->absolute_y[d]));}
        s->absolute_valid[d]=1;s->absolute_x[d]=x;s->absolute_y[d]=y;
    } else if(e->type==HW_REC_WHEEL) {s->wheel_rem+=e->x;s->frame.wheel=(int16_t)(s->frame.wheel+s->wheel_rem/120);s->wheel_rem%=120;}
    else {s->pan_rem+=e->x;s->frame.pan=(int16_t)(s->frame.pan+s->pan_rem/120);s->pan_rem%=120;}
    s->frame.keyboard.mods=0;memset(s->frame.keyboard.keys,0,sizeof s->frame.keyboard.keys);s->frame.buttons=0;
    for(unsigned i=0;i<HW_REC_DEVICES;i++) {
        s->frame.buttons|=s->buttons[i];
        for(unsigned k=0;k<1024;k++) {
            uint16_t b=s->bindings[i][k];if(b&HIDWAY_BIND_MOD)s->frame.keyboard.mods|=(uint8_t)b;
            else if(b&&b<=HIDWAY_KEY_USAGE_MAX)hidway_bitmap_set(s->frame.keyboard.keys,(uint8_t)b);
        }
    }
    return 1;
}
