#include "send_clock.h"
static DWORD WINAPI clock_thread(LPVOID arg)
{
    hidway_send_clock_t *c = arg;
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002, TIMER_ALL_ACCESS);
    if (!timer)
        timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)c->interval * 10000;
    if (!timer || !SetWaitableTimer(timer, &due, (LONG)c->interval, NULL, NULL, FALSE)) {
        InterlockedExchange(&c->failed, 1);
        if (timer)
            CloseHandle(timer);
        return 1;
    }
    HANDLE wait[2] = {c->stop, timer};
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0)
            break;
        if (r != WAIT_OBJECT_0 + 1) {
            InterlockedExchange(&c->failed, 1);
            break;
        }
        if (!InterlockedCompareExchange(&c->posted, 1, 0) &&
            !PostMessageA(c->target, c->message, (WPARAM)c->generation, (LPARAM)GetTickCount())) {
            InterlockedExchange(&c->failed, 1);
            break;
        }
    }
    CancelWaitableTimer(timer);
    CloseHandle(timer);
    return 0;
}
int hidway_send_clock_stop(hidway_send_clock_t *c)
{
    if (!c->thread)
        return 1;
    SetEvent(c->stop);
    if (WaitForSingleObject(c->thread, 2000) != WAIT_OBJECT_0)
        return 0;
    CloseHandle(c->thread);
    CloseHandle(c->stop);
    c->thread = c->stop = NULL;
    InterlockedExchange(&c->posted, 0);
    return 1;
}
int hidway_send_clock_start(hidway_send_clock_t *c, HWND target, UINT message, unsigned interval,
                            DWORD generation)
{
    if (!hidway_send_clock_stop(c))
        return 0;
    c->target = target;
    c->message = message;
    c->interval = interval ? interval : 1;
    c->generation = generation;
    InterlockedExchange(&c->failed, 0);
    InterlockedExchange(&c->posted, 0);
    c->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!c->stop)
        return 0;
    c->thread = CreateThread(NULL, 0, clock_thread, c, 0, NULL);
    if (!c->thread) {
        CloseHandle(c->stop);
        c->stop = NULL;
        return 0;
    }
    return 1;
}
