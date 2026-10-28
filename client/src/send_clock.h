#ifndef HIDWAY_SEND_CLOCK_H
#define HIDWAY_SEND_CLOCK_H
#include <windows.h>
typedef struct {
    HANDLE thread, stop;
    HWND target;
    UINT message;
    unsigned interval;
    DWORD generation;
    volatile LONG posted, failed;
} hidway_send_clock_t;
int hidway_send_clock_start(hidway_send_clock_t *c, HWND target, UINT message, unsigned interval,
                            DWORD generation);
int hidway_send_clock_stop(hidway_send_clock_t *c);
#endif
