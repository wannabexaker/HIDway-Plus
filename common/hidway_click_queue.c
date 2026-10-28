#include "hidway_click_queue.h"
#include <string.h>

void hidway_click_queue_clear(hidway_click_queue_t *q)
{
    memset(q, 0, sizeof *q);
}

bool hidway_click_queue_push(hidway_click_queue_t *q, hidway_mouse_frame_t frame)
{
    unsigned last = q->count ? q->pending[(q->head + q->count - 1) % HIDWAY_CLICK_QUEUE_CAPACITY].buttons
                             : q->visible.buttons;
    if (last == frame.buttons)
        return true;
    if (q->count == HIDWAY_CLICK_QUEUE_CAPACITY)
        return false;
    q->pending[(q->head + q->count) % HIDWAY_CLICK_QUEUE_CAPACITY] = frame;
    q->count++;
    return true;
}

hidway_mouse_frame_t hidway_click_queue_sample(hidway_click_queue_t *q,
                                              hidway_mouse_frame_t live, uint32_t now)
{
    bool due = !q->pacing || (uint32_t)(now - q->shown_at) >= HIDWAY_CLICK_HOLD_MS;
    if (q->count && due) {
        q->visible = q->pending[q->head];
        q->head = (q->head + 1) % HIDWAY_CLICK_QUEUE_CAPACITY;
        q->count--;
        q->shown_at = now;
        q->pacing = true;
        return q->visible;
    }
    /* Keep event-time motion while draining a burst, but do not freeze an
     * ordinary drag once there are no older button transitions queued. */
    if (q->count)
        return q->visible;
    live.buttons = q->visible.buttons;
    return live;
}
