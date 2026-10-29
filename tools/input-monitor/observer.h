#ifndef HIDWAY_MONITOR_OBSERVER_H
#define HIDWAY_MONITOR_OBSERVER_H
#include "model.h"
#include "hidway_recording.h"
typedef struct { hw_rec_event event; DWORD tick; unsigned keyboard; } im_observed_event;
void im_observer_record(int on);
int im_observer_take(im_observed_event *event,int *overflow);
int im_observer_start(void);
void im_observer_stop(void);
void im_observer_pause(int paused);
void im_observer_reset(void);
void im_observer_snapshot(im_counts *keyboard,im_counts *mouse);
void im_observer_seen(LONG *keyboard,LONG *mouse);
#endif
