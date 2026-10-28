#ifndef HIDWAY_MONITOR_OBSERVER_H
#define HIDWAY_MONITOR_OBSERVER_H
#include "model.h"
int im_observer_start(void);
void im_observer_stop(void);
void im_observer_pause(int paused);
void im_observer_reset(void);
void im_observer_snapshot(im_counts *keyboard,im_counts *mouse);
void im_observer_seen(LONG *keyboard,LONG *mouse);
#endif
