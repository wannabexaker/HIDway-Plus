/*
 * HIDway - echo credits for "Physical keys through the Pico" on the Gaming PC
 * route (the Pico is plugged into the same PC that HIDway runs on).
 *
 * A low-level hook cannot tell which keyboard or mouse produced a hardware
 * event, so the key the Pico sends back looks exactly like the physical key
 * that caused it. Every edge sent to the Pico (key or modifier down/up,
 * button down/up, wheel notches) grants one short-lived credit; a hardware
 * event that finds a matching credit is the Pico's echo and passes, anything
 * else is this PC's own device and is consumed and sent to the Pico.
 *
 * Threads: the GUI thread grants and expires credits, the hook thread takes
 * them. Every update of a shared credit word goes through `cas`, so neither
 * side ever waits for the other. Time is a millisecond counter supplied by
 * the caller (the same clock on both threads). Portable C, no OS headers.
 */
#ifndef HIDWAY_ECHO_H
#define HIDWAY_ECHO_H

#include "hidway_kbd.h"

#include <stdbool.h>
#include <stdint.h>

/* Slots: 0..255 HID keyboard usages (modifiers at 0xE0..0xE7), then the five
 * mouse buttons (left, right, middle, back, forward). The wheel has its own
 * signed credit. */
#define HIDWAY_ECHO_BUTTON0 256u
#define HIDWAY_ECHO_SLOTS 261u

#define HIDWAY_ECHO_LIFETIME_MS 250u    /* an unanswered credit is dropped after this */
#define HIDWAY_ECHO_REPEAT_MS 25u       /* typematic repeats pass at most this often per key */
#define HIDWAY_ECHO_SILENT_LIMIT 5u     /* this many unanswered edges ... */
#define HIDWAY_ECHO_SILENT_WINDOW_MS 3000u /* ... within this time, with no echo at all, is a fault */
#define HIDWAY_ECHO_LOOP_LIMIT 50u      /* more consumed edges of one key/button per second is a fault */

/* What the hook does with a hardware (non-injected) event. */
enum {
    HIDWAY_ECHO_PASS = 0, /* the Pico's echo (or not ours): let this PC see it */
    HIDWAY_ECHO_OWN,      /* this PC's own device: consume it and send it to the Pico */
    HIDWAY_ECHO_SWALLOW   /* a surplus repeat of a key we hold: consume, send nothing */
};

/* Faults reported by hidway_echo_expire(); the caller disarms. */
enum { HIDWAY_ECHO_OK = 0, HIDWAY_ECHO_FAULT_SILENT, HIDWAY_ECHO_FAULT_LOOP };

/* Compare-and-swap on one credit word: returns non-zero when *word was
 * `expected` and is now `desired`. NULL means a plain (single-thread) swap. */
typedef int (*hidway_echo_cas_fn)(volatile uint32_t *word, uint32_t expected, uint32_t desired);

/* What one state packet tells the Pico to hold. */
typedef struct {
    uint8_t mods;
    uint8_t keys[HIDWAY_KEY_BITMAP_BYTES];
    uint8_t buttons;
    int16_t wheel; /* cumulative, wrapping */
} hidway_echo_frame_t;

typedef struct {
    /* shared: credit words (GUI grants/expires, hook takes, all through cas) */
    volatile uint32_t credit[HIDWAY_ECHO_SLOTS]; /* down count | up count << 4 | expiry << 8 */
    volatile uint32_t wheel; /* notches up | notches down << 8 | expiry << 16 */
    /* written by the GUI only: held in the last state sent (after the six-key limit) */
    volatile uint8_t pico_down[HIDWAY_ECHO_SLOTS];
    /* written by the hook only */
    volatile uint32_t echoes;    /* credits taken so far */
    volatile uint32_t loop_slot; /* slot + 1 that exceeded the loop limit, 0 = none */
    hidway_echo_cas_fn cas;
    /* hook thread only */
    uint8_t ours[HIDWAY_ECHO_SLOTS];
    uint8_t repeat_valid[HIDWAY_ECHO_SLOTS];
    uint32_t repeat_at[HIDWAY_ECHO_SLOTS];
    uint32_t rate_at[HIDWAY_ECHO_SLOTS];
    uint16_t rate_count[HIDWAY_ECHO_SLOTS];
    /* GUI thread only */
    hidway_echo_frame_t sent;
    hidway_kro6_t kro;
    uint32_t seen_echoes;
    unsigned expired_pending; /* expired edges found while granting */
    uint32_t silent_at[HIDWAY_ECHO_SILENT_LIMIT];
    unsigned silent_head, silent_count;
} hidway_echo_t;

/* Clear every credit, ownership and guard (at ARM and at DISARM). */
void hidway_echo_reset(hidway_echo_t *e, hidway_echo_cas_fn cas);

/* GUI: `next` is about to be sent to the Pico. Grants one credit per edge the
 * Pico will perform compared with the previous state sent, honouring the
 * Pico's six-key limit. */
void hidway_echo_grant(hidway_echo_t *e, const hidway_echo_frame_t *next, uint32_t now);

/* GUI, every send tick: drop expired credits and check the guards. */
int hidway_echo_expire(hidway_echo_t *e, uint32_t now);

/* Hook: a hardware key (slot = usage) or button (HIDWAY_ECHO_BUTTON0 + i)
 * event. Returns HIDWAY_ECHO_PASS / OWN / SWALLOW. */
int hidway_echo_input(hidway_echo_t *e, unsigned slot, bool up, uint32_t now);

/* Hook: a hardware vertical wheel event of `delta` (120 per notch). Passes
 * only when a live wheel credit has the same sign and covers it. Returns
 * HIDWAY_ECHO_PASS or HIDWAY_ECHO_OWN. */
int hidway_echo_wheel(hidway_echo_t *e, int delta, uint32_t now);

/* Live (unexpired) credits, for tests and diagnostics. */
unsigned hidway_echo_live(const hidway_echo_t *e, unsigned slot, bool up, uint32_t now);
int hidway_echo_live_wheel(const hidway_echo_t *e, uint32_t now);

#endif /* HIDWAY_ECHO_H */
