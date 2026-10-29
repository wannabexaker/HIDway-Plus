#ifndef HIDWAY_RECORDING_H
#define HIDWAY_RECORDING_H
#include <stdint.h>
#include <stdio.h>
#define HW_REC_MAX_EVENTS 500000u
#define HW_REC_MAX_MS 3600000u
#define HW_REC_DEVICES 64u
enum { HW_REC_KEY_DOWN=1, HW_REC_KEY_UP, HW_REC_BUTTON_DOWN, HW_REC_BUTTON_UP,
       HW_REC_MOVE, HW_REC_WHEEL, HW_REC_PAN, HW_REC_ABSOLUTE, HW_REC_RELEASE_DEVICE };
typedef struct { uint32_t ms,device,type,code,flags; int32_t x,y; uint32_t vk; } hw_rec_event;
typedef struct { uint32_t width,height,virtual_width,virtual_height; } hw_rec_screen;
typedef struct { hw_rec_screen screen; hw_rec_event *events; unsigned count; uint32_t duration; } hw_recording;
int hw_rec_header(FILE *f,const hw_rec_screen *s);
int hw_rec_write(FILE *f,const hw_rec_event *e);
int hw_rec_footer(FILE *f,unsigned count,uint32_t duration);
int hw_rec_valid(const hw_rec_event *e);
int hw_rec_load(FILE *f,hw_recording *out,char *error,unsigned capacity);
void hw_rec_free(hw_recording *r);
#endif
