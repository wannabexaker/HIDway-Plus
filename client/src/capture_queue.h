#ifndef HIDWAY_CAPTURE_QUEUE_H
#define HIDWAY_CAPTURE_QUEUE_H
/* One producer (hook), one consumer (GUI), bounded storage and one wakeup.
 * The lock protects both the wake flag and queue; no input pointers escape. */
#define HIDWAY_CAPTURE_CAPACITY 512u
typedef struct {
    UINT kind;
    WPARAM w;
    LPARAM l;
    uint32_t generation, created_at;
} capture_event_t;
typedef struct {
    SRWLOCK lock;
    capture_event_t events[HIDWAY_CAPTURE_CAPACITY];
    unsigned head, count;
    int notified;
} capture_queue_t;
static capture_queue_t capture_events;
static volatile LONG capture_fault;
static volatile LONG capture_generation;
static void capture_queue_clear(void)
{
    AcquireSRWLockExclusive(&capture_events.lock);
    capture_events.head = capture_events.count = 0;
    capture_events.notified = 0;
    ReleaseSRWLockExclusive(&capture_events.lock);
}
static int capture_enqueue(HWND target, UINT wake, UINT kind, WPARAM w, LPARAM l)
{
    int notify = 0, ok = 1;
    AcquireSRWLockExclusive(&capture_events.lock);
    if (capture_events.count == HIDWAY_CAPTURE_CAPACITY)
        ok = 0;
    else {
        capture_event_t *e =
            &capture_events
                 .events[(capture_events.head + capture_events.count) % HIDWAY_CAPTURE_CAPACITY];
        *e = (capture_event_t){kind, w, l,
                               (uint32_t)InterlockedCompareExchange(&capture_generation, 0, 0),
                               GetTickCount()};
        capture_events.count++;
        if (!capture_events.notified) {
            capture_events.notified = 1;
            notify = 1;
        }
    }
    ReleaseSRWLockExclusive(&capture_events.lock);
    if (!ok || (notify && !PostMessageA(target, wake, 0, 0))) {
        InterlockedExchange(&capture_fault, 1);
        return 0;
    }
    return 1;
}
static int capture_dequeue(capture_event_t *e)
{
    int ok = 0;
    AcquireSRWLockExclusive(&capture_events.lock);
    if (capture_events.count) {
        *e = capture_events.events[capture_events.head];
        capture_events.head = (capture_events.head + 1) % HIDWAY_CAPTURE_CAPACITY;
        capture_events.count--;
        ok = 1;
    } else
        capture_events.notified = 0;
    ReleaseSRWLockExclusive(&capture_events.lock);
    return ok;
}
#endif
