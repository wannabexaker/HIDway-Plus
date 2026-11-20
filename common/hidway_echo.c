#include "hidway_echo.h"

#include <string.h>

/* Key/button credit word: bits 0-3 down credits, 4-7 up credits, 8-31 the
 * expiry time (low 24 bits of the millisecond clock). Wheel word: bits 0-7
 * notches up, 8-15 notches down, 16-31 the expiry time (low 16 bits). A new
 * grant renews the expiry of the credits already waiting in the same slot. */
#define COUNT_MAX 15u
#define WHEEL_MAX 255u

static int swap(hidway_echo_t *e, volatile uint32_t *w, uint32_t expected, uint32_t desired)
{
    if (e->cas)
        return e->cas(w, expected, desired);
    if (*w != expected)
        return 0;
    *w = desired;
    return 1;
}

static int expired24(uint32_t word, uint32_t now)
{
    return ((now - (word >> 8)) & 0xFFFFFFu) < 0x800000u;
}

static int expired16(uint32_t word, uint32_t now)
{
    return ((now - (word >> 16)) & 0xFFFFu) < 0x8000u;
}

static unsigned word_count(uint32_t word, bool up)
{
    return up ? (word >> 4) & 15u : word & 15u;
}

void hidway_echo_reset(hidway_echo_t *e, hidway_echo_cas_fn cas)
{
    memset((void *)e, 0, sizeof *e);
    e->cas = cas;
}

/* ------------------------------------------------------------- GUI side */

static void grant_slot(hidway_echo_t *e, unsigned slot, bool up, uint32_t now)
{
    volatile uint32_t *w = &e->credit[slot];
    for (;;) {
        uint32_t old = *w, base = old;
        unsigned dropped = 0;
        if ((old & 0xFFu) && expired24(old, now)) { /* never revive an expired credit */
            dropped = word_count(old, false) + word_count(old, true);
            base = 0;
        }
        unsigned down = word_count(base, false), ups = word_count(base, true);
        if (up)
            ups = ups < COUNT_MAX ? ups + 1 : COUNT_MAX;
        else
            down = down < COUNT_MAX ? down + 1 : COUNT_MAX;
        uint32_t desired = down | (ups << 4) | (((now + HIDWAY_ECHO_LIFETIME_MS) & 0xFFFFFFu) << 8);
        if (swap(e, w, old, desired)) {
            e->expired_pending += dropped;
            return;
        }
    }
}

static void grant_wheel(hidway_echo_t *e, int notches, uint32_t now)
{
    for (;;) {
        uint32_t old = e->wheel, base = old;
        unsigned dropped = 0;
        if ((old & 0xFFFFu) && expired16(old, now)) {
            dropped = 1;
            base = 0;
        }
        unsigned pos = base & 0xFFu, neg = (base >> 8) & 0xFFu;
        unsigned add = (unsigned)(notches < 0 ? -notches : notches);
        if (notches > 0)
            pos = pos + add > WHEEL_MAX ? WHEEL_MAX : pos + add;
        else
            neg = neg + add > WHEEL_MAX ? WHEEL_MAX : neg + add;
        uint32_t desired = pos | (neg << 8) | (((now + HIDWAY_ECHO_LIFETIME_MS) & 0xFFFFu) << 16);
        if (swap(e, &e->wheel, old, desired)) {
            e->expired_pending += dropped;
            return;
        }
    }
}

static int kro_has(const hidway_kro6_t *k, uint8_t usage)
{
    for (int i = 0; i < HIDWAY_BOOT_KEYS; i++)
        if (k->slots[i] == usage)
            return 1;
    return 0;
}

static void edge(hidway_echo_t *e, unsigned slot, bool down, uint32_t now)
{
    e->pico_down[slot] = (uint8_t)down;
    grant_slot(e, slot, !down, now);
}

void hidway_echo_grant(hidway_echo_t *e, const hidway_echo_frame_t *next, uint32_t now)
{
    uint8_t changed = (uint8_t)(e->sent.mods ^ next->mods);
    for (unsigned i = 0; i < 8; i++)
        if (changed & (1u << i))
            edge(e, 0xE0u + i, (next->mods & (1u << i)) != 0, now);

    /* The Pico reports at most six keys; a seventh waits for a free slot. */
    hidway_kro6_t before = e->kro;
    hidway_kro6_update(&e->kro, next->keys);
    for (int i = 0; i < HIDWAY_BOOT_KEYS; i++) {
        uint8_t was = before.slots[i], is = e->kro.slots[i];
        if (was && !kro_has(&e->kro, was))
            edge(e, was, false, now);
        if (is && !kro_has(&before, is))
            edge(e, is, true, now);
    }

    changed = (uint8_t)(e->sent.buttons ^ next->buttons);
    for (unsigned i = 0; i < 5; i++)
        if (changed & (1u << i))
            edge(e, HIDWAY_ECHO_BUTTON0 + i, (next->buttons & (1u << i)) != 0, now);

    int16_t wheel = (int16_t)(uint16_t)((uint16_t)next->wheel - (uint16_t)e->sent.wheel);
    if (wheel)
        grant_wheel(e, wheel, now);
    e->sent = *next;
}

int hidway_echo_expire(hidway_echo_t *e, uint32_t now)
{
    unsigned expired = e->expired_pending;
    e->expired_pending = 0;
    for (unsigned s = 0; s < HIDWAY_ECHO_SLOTS; s++) {
        volatile uint32_t *w = &e->credit[s];
        for (;;) {
            uint32_t old = *w;
            if (!(old & 0xFFu) || !expired24(old, now))
                break;
            if (swap(e, w, old, 0)) {
                expired += word_count(old, false) + word_count(old, true);
                break;
            }
        }
    }
    for (;;) {
        uint32_t old = e->wheel;
        if (!(old & 0xFFFFu) || !expired16(old, now))
            break;
        if (swap(e, &e->wheel, old, 0)) {
            expired++;
            break;
        }
    }
    uint32_t echoes = e->echoes;
    if (echoes != e->seen_echoes) { /* the Pico answered: start counting again */
        e->seen_echoes = echoes;
        e->silent_count = 0;
    }
    for (unsigned i = 0; i < expired && i < HIDWAY_ECHO_SILENT_LIMIT; i++) {
        e->silent_at[e->silent_head] = now;
        e->silent_head = (e->silent_head + 1) % HIDWAY_ECHO_SILENT_LIMIT;
        if (e->silent_count < HIDWAY_ECHO_SILENT_LIMIT)
            e->silent_count++;
    }
    /* When the ring is full, silent_head is the oldest entry. */
    if (e->silent_count >= HIDWAY_ECHO_SILENT_LIMIT &&
        now - e->silent_at[e->silent_head] <= HIDWAY_ECHO_SILENT_WINDOW_MS)
        return HIDWAY_ECHO_FAULT_SILENT;
    if (e->loop_slot)
        return HIDWAY_ECHO_FAULT_LOOP;
    return HIDWAY_ECHO_OK;
}

/* ------------------------------------------------------------ hook side */

static int take(hidway_echo_t *e, unsigned slot, bool up, uint32_t now)
{
    volatile uint32_t *w = &e->credit[slot];
    for (;;) {
        uint32_t old = *w;
        if (!word_count(old, up) || expired24(old, now))
            return 0;
        if (swap(e, w, old, old - (up ? 16u : 1u))) {
            e->echoes = e->echoes + 1;
            return 1;
        }
    }
}

static void note_own(hidway_echo_t *e, unsigned slot, uint32_t now)
{
    if (!e->rate_count[slot] || now - e->rate_at[slot] >= 1000u) {
        e->rate_at[slot] = now;
        e->rate_count[slot] = 0;
    }
    if (e->rate_count[slot] < 0xFFFFu)
        e->rate_count[slot]++;
    if (e->rate_count[slot] > HIDWAY_ECHO_LOOP_LIMIT && !e->loop_slot)
        e->loop_slot = slot + 1;
}

int hidway_echo_input(hidway_echo_t *e, unsigned slot, bool up, uint32_t now)
{
    if (slot >= HIDWAY_ECHO_SLOTS)
        return HIDWAY_ECHO_PASS;
    if (!up) {
        if (take(e, slot, false, now))
            return HIDWAY_ECHO_PASS; /* the Pico's echo */
        if (e->pico_down[slot]) {
            /* The Pico already holds it: a typematic repeat (from either
             * keyboard), or this PC's key pressed while another source holds
             * the same key. This PC already shows it down; pass one now and
             * then, not both devices' repeats. */
            if (!e->repeat_valid[slot] || now - e->repeat_at[slot] >= HIDWAY_ECHO_REPEAT_MS) {
                e->repeat_valid[slot] = 1;
                e->repeat_at[slot] = now;
                return HIDWAY_ECHO_PASS;
            }
            return HIDWAY_ECHO_SWALLOW;
        }
        if (e->ours[slot])
            return HIDWAY_ECHO_SWALLOW; /* held by us but not sent as a key (e.g. a mouse alias) */
        e->ours[slot] = 1;
        note_own(e, slot, now);
        return HIDWAY_ECHO_OWN;
    }
    if (take(e, slot, true, now))
        return HIDWAY_ECHO_PASS;
    if (e->ours[slot]) {
        e->ours[slot] = 0;
        note_own(e, slot, now);
        return HIDWAY_ECHO_OWN;
    }
    return HIDWAY_ECHO_PASS; /* released after being held before ARM, or not ours */
}

int hidway_echo_wheel(hidway_echo_t *e, int delta, uint32_t now)
{
    if (!delta)
        return HIDWAY_ECHO_PASS;
    if (delta % 120 == 0) {
        unsigned n = (unsigned)(delta < 0 ? -delta : delta) / 120u;
        unsigned shift = delta > 0 ? 0u : 8u;
        for (;;) {
            uint32_t old = e->wheel;
            unsigned have = (old >> shift) & 0xFFu;
            if (!(old & 0xFFFFu) || expired16(old, now) || have < n)
                break;
            uint32_t desired = old - ((uint32_t)n << shift);
            if (!(desired & 0xFFFFu))
                desired = 0;
            if (swap(e, &e->wheel, old, desired)) {
                e->echoes = e->echoes + 1;
                return HIDWAY_ECHO_PASS;
            }
        }
    }
    return HIDWAY_ECHO_OWN;
}

unsigned hidway_echo_live(const hidway_echo_t *e, unsigned slot, bool up, uint32_t now)
{
    if (slot >= HIDWAY_ECHO_SLOTS)
        return 0;
    uint32_t w = e->credit[slot];
    return (w & 0xFFu) && !expired24(w, now) ? word_count(w, up) : 0;
}

int hidway_echo_live_wheel(const hidway_echo_t *e, uint32_t now)
{
    uint32_t w = e->wheel;
    if (!(w & 0xFFFFu) || expired16(w, now))
        return 0;
    return (int)(w & 0xFFu) - (int)((w >> 8) & 0xFFu);
}
