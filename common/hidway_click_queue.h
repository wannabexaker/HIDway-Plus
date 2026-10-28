#ifndef HIDWAY_CLICK_QUEUE_H
#define HIDWAY_CLICK_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define HIDWAY_CLICK_QUEUE_CAPACITY 32u
#define HIDWAY_CLICK_HOLD_MS 16u

typedef struct {
    uint8_t buttons;
    int32_t x, y;
    int16_t wheel, pan;
} hidway_mouse_frame_t;

/* Preserve short clicks between sender ticks. Bounded, no allocations.
 * Each button transition has its own mouse snapshot and minimum dwell.
 * This is pacing, not a guarantee against UDP loss or downstream batching. */
typedef struct {
    hidway_mouse_frame_t pending[HIDWAY_CLICK_QUEUE_CAPACITY];
    hidway_mouse_frame_t visible;
    unsigned head, count;
    uint32_t shown_at;
    bool pacing;
} hidway_click_queue_t;

void hidway_click_queue_clear(hidway_click_queue_t *q);
/* False means overload: the caller must release/disarm, not drop an up edge. */
bool hidway_click_queue_push(hidway_click_queue_t *q, hidway_mouse_frame_t frame);
hidway_mouse_frame_t hidway_click_queue_sample(hidway_click_queue_t *q,
                                              hidway_mouse_frame_t live, uint32_t now);

#endif
