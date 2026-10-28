#ifndef HIDWAY_INPUT_STATE_H
#define HIDWAY_INPUT_STATE_H
#include "hidway_kbd.h"
#include <stdbool.h>
#include <stdint.h>
#define HIDWAY_BIND_MOD 0x100u
#define HIDWAY_BIND_MOUSE 0x200u
#define HIDWAY_INPUT_ORIGINS 3u
typedef struct {
    uint16_t binding[HIDWAY_INPUT_ORIGINS][256];
    uint8_t mouse[HIDWAY_INPUT_ORIGINS];
} hidway_input_state_t;
typedef struct {
    uint8_t mods, keys[HIDWAY_KEY_BITMAP_BYTES], buttons, aliases;
    uint8_t source_buttons[HIDWAY_INPUT_ORIGINS];
} hidway_held_t;
unsigned hidway_origin_slot(unsigned source);
void hidway_input_key(hidway_input_state_t *s, unsigned source, unsigned key, uint16_t binding,
                      bool up);
void hidway_input_button(hidway_input_state_t *s, unsigned source, uint8_t bit, bool up);
void hidway_input_merge(const hidway_input_state_t *s, hidway_held_t *out);

#define HIDWAY_KEY_QUEUE_CAPACITY 64u
#define HIDWAY_KEY_DWELL_MS 8u
typedef struct {
    uint8_t mods, keys[HIDWAY_KEY_BITMAP_BYTES];
} hidway_key_frame_t;
typedef struct {
    hidway_key_frame_t pending[HIDWAY_KEY_QUEUE_CAPACITY], visible;
    unsigned head, count;
    uint32_t shown_at;
    bool pacing;
} hidway_key_queue_t;
bool hidway_key_queue_push(hidway_key_queue_t *q, hidway_key_frame_t f);
hidway_key_frame_t hidway_key_queue_sample(hidway_key_queue_t *q, uint32_t now);

/* One bounded pulse/hold lane. Required release never depends on queue space.
 * New short pairs during overload are discarded as pairs; a still-held input
 * may be admitted at the next slot. No deferred click debt is accumulated. */
typedef struct {
    bool input, active, release, sampled, cooldown;
    uint32_t shown_at, next_allowed, limited;
} hidway_pulse_t;
void hidway_pulse_input(hidway_pulse_t *p, bool down, uint32_t now);
bool hidway_pulse_sample(hidway_pulse_t *p, uint32_t now, unsigned cps, bool repeat);
/* Immediate release for a downstream journal that provides the minimum dwell. */
void hidway_pulse_release(hidway_pulse_t *p, uint32_t now, unsigned cps);

typedef struct {
    hidway_key_frame_t keyboard;
    uint8_t buttons;
    int32_t x, y;
    int16_t wheel, pan;
} hidway_event_frame_t;
#define HIDWAY_EVENT_CAPACITY 64u
#define HIDWAY_EVENT_MAX_AGE_MS 100u
typedef struct {
    hidway_event_frame_t pending[HIDWAY_EVENT_CAPACITY], visible;
    uint32_t created[HIDWAY_EVENT_CAPACITY], shown_at;
    unsigned head, count;
    bool pacing;
} hidway_event_queue_t;
bool hidway_event_push(hidway_event_queue_t *q, hidway_event_frame_t f, uint32_t now);
unsigned hidway_event_backlog_ms(const hidway_event_queue_t *q);
/* False means expired queued input: release rather than replaying it. */
bool hidway_event_sample(hidway_event_queue_t *q, hidway_event_frame_t live, uint32_t now,
                         hidway_event_frame_t *out);
#endif
