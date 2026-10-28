#ifndef HIDWAY_MONITOR_MODEL_H
#define HIDWAY_MONITOR_MODEL_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#define IM_MAX_DEVICES 64
#define IM_NAME_LEN 128
enum { IM_UNKNOWN, IM_USB, IM_BLUETOOTH, IM_BUILTIN, IM_VIRTUAL };
enum { IM_LABEL_AUTO, IM_LABEL_PHYSICAL, IM_LABEL_VIRTUAL, IM_LABEL_TOUCHPAD };
typedef struct {
    uint64_t down,up,repeat,unmatched;
    unsigned held;
    uint8_t keys[1024];
    int64_t dx,dy,wheel,pan;
} im_counts;
typedef struct {
    HANDLE handle;
    unsigned used,keyboard,connected,pico,source,label;
    wchar_t name[IM_NAME_LEN],alias[IM_NAME_LEN],manufacturer[IM_NAME_LEN];
    wchar_t path[1024],instance[512],service[96],bus[96],evidence[256];
    wchar_t id[24];
    im_counts counts;
} im_device;
void im_identity(const wchar_t *path,unsigned keyboard,wchar_t out[24]);
void im_record(im_counts *counts,const RAWINPUT *input);
void im_reset(im_device *device);
void im_disconnect(im_device *device);
const wchar_t *im_source_name(const im_device *device);
const wchar_t *im_display_name(const im_device *device);
unsigned im_metadata_source(const wchar_t *instance,const wchar_t *bus,const wchar_t *description);
void im_describe(im_device *device);
int im_save_label(const wchar_t *file,const im_device *device);
void im_load_label(const wchar_t *file,im_device *device);
#endif
