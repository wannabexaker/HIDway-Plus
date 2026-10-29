#ifndef IM_RECORDING_H
#define IM_RECORDING_H
#include "model.h"
#include "hidway_recording.h"
void im_event_name(const hw_rec_event *e,wchar_t *text,unsigned capacity);
void im_raw_events(const RAWINPUT *raw,unsigned device,void (*emit)(const hw_rec_event *,void *),void *context);
int im_record_start(const wchar_t *path,const hw_rec_screen *screen);
int im_record_push(const hw_rec_event *event,const im_device *device);
void im_record_stop(uint32_t duration);
void im_record_abort(void);
/* 0 idle, 1 recording, 2 finishing, 3 saved, -1 failed. Only UI calls poll. */
int im_record_poll(void);
unsigned im_record_count(void);
#endif
