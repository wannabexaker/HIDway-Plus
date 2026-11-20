/* Echo credits for physical keys through the Pico (Gaming PC route).
 * Portable: built and run on Windows and on the Raspberry Pi. */
#include "hidway_echo.h"

#include <stdio.h>
#include <string.h>

static unsigned fail;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                 \
            fail++;                                                                                \
        }                                                                                          \
    } while (0)

static hidway_echo_t e;
static unsigned cas_calls;
static int counting_cas(volatile uint32_t *w, uint32_t expected, uint32_t desired)
{
    cas_calls++;
    if (*w != expected)
        return 0;
    *w = desired;
    return 1;
}

#define KEY_A 0x04u
#define BTN_LEFT HIDWAY_ECHO_BUTTON0

static hidway_echo_frame_t frame_keys(const uint8_t *usages, unsigned n)
{
    hidway_echo_frame_t f;
    memset(&f, 0, sizeof f);
    for (unsigned i = 0; i < n; i++)
        hidway_bitmap_set(f.keys, usages[i]);
    return f;
}

/* ---- A small closed-loop model: this PC's keyboard, HIDway, the Pico ----
 * Events are processed in time order. OWN events change what HIDway holds
 * for the physical source; the state is sent one millisecond later and the
 * Pico performs every edge `latency` ms after that, as hardware input that
 * reaches the same hook. PASS events reach this PC's applications. */
enum { EV_PHYS, EV_ECHO };
typedef struct { uint32_t t; int kind; unsigned usage; bool up; } sim_event;
static sim_event queue[4096];
static unsigned queued;
static uint8_t phys_held[256], virt_held[256], os_down[256];
static hidway_echo_frame_t sent;

static void push(uint32_t t, int kind, unsigned usage, bool up)
{
    if (queued < sizeof queue / sizeof queue[0])
        queue[queued++] = (sim_event){t, kind, usage, up};
}

static int pop(sim_event *out)
{
    if (!queued)
        return 0;
    unsigned best = 0;
    for (unsigned i = 1; i < queued; i++)
        if (queue[i].t < queue[best].t)
            best = i;
    *out = queue[best];
    queue[best] = queue[--queued];
    return 1;
}

static void sim_send(uint32_t t, uint32_t latency)
{
    hidway_echo_frame_t f;
    memset(&f, 0, sizeof f);
    for (unsigned u = HIDWAY_KEY_USAGE_MIN; u <= HIDWAY_KEY_USAGE_MAX; u++)
        if (phys_held[u] || virt_held[u])
            hidway_bitmap_set(f.keys, (uint8_t)u);
    if (!memcmp(&f, &sent, sizeof f))
        return;
    for (unsigned u = HIDWAY_KEY_USAGE_MIN; u <= HIDWAY_KEY_USAGE_MAX; u++) {
        bool was = hidway_bitmap_test(sent.keys, (uint8_t)u), is = hidway_bitmap_test(f.keys, (uint8_t)u);
        if (was != is)
            push(t + latency, EV_ECHO, u, !is);
    }
    hidway_echo_grant(&e, &f, t);
    sent = f;
}

static void sim_reset(void)
{
    hidway_echo_reset(&e, counting_cas);
    queued = 0;
    memset(phys_held, 0, sizeof phys_held);
    memset(virt_held, 0, sizeof virt_held);
    memset(os_down, 0, sizeof os_down);
    memset(&sent, 0, sizeof sent);
}

/* Runs until nothing is in flight. Returns the number of processed events,
 * or 0 when it did not settle (an endless loop). */
static unsigned sim_run(uint32_t latency, int *fault)
{
    unsigned steps = 0;
    sim_event ev;
    uint32_t last = 0;
    *fault = 0;
    while (pop(&ev)) {
        if (++steps > 2000)
            return 0;
        last = ev.t;
        int v = hidway_echo_input(&e, ev.usage, ev.up, ev.t);
        if (v == HIDWAY_ECHO_PASS)
            os_down[ev.usage] = (uint8_t)!ev.up;
        else if (v == HIDWAY_ECHO_OWN) {
            phys_held[ev.usage] = (uint8_t)!ev.up;
            sim_send(ev.t + 1, latency);
        }
        int f = hidway_echo_expire(&e, ev.t);
        if (f && !*fault)
            *fault = f;
    }
    int f = hidway_echo_expire(&e, last + 1000);
    if (f && !*fault)
        *fault = f;
    return steps;
}

static int settled_clean(void)
{
    for (unsigned u = 0; u < 256; u++)
        if (phys_held[u] || os_down[u] || e.ours[u] || e.pico_down[u])
            return 0;
    return !hidway_bitmap_test(sent.keys, KEY_A);
}

int main(void)
{
    uint32_t t = 1000;

    /* ---- press and release with their echoes ---- */
    hidway_echo_reset(&e, counting_cas);
    CHECK(hidway_echo_input(&e, KEY_A, false, t) == HIDWAY_ECHO_OWN && e.ours[KEY_A]);
    uint8_t a[] = {KEY_A};
    hidway_echo_frame_t down = frame_keys(a, 1), none = frame_keys(a, 0);
    hidway_echo_grant(&e, &down, t + 2);
    CHECK(hidway_echo_live(&e, KEY_A, false, t + 2) == 1 && e.pico_down[KEY_A]);
    CHECK(hidway_echo_input(&e, KEY_A, false, t + 20) == HIDWAY_ECHO_PASS); /* echo */
    CHECK(hidway_echo_live(&e, KEY_A, false, t + 20) == 0 && e.echoes == 1);
    CHECK(hidway_echo_input(&e, KEY_A, true, t + 100) == HIDWAY_ECHO_OWN && !e.ours[KEY_A]);
    hidway_echo_grant(&e, &none, t + 102);
    CHECK(hidway_echo_live(&e, KEY_A, true, t + 102) == 1 && !e.pico_down[KEY_A]);
    CHECK(hidway_echo_input(&e, KEY_A, true, t + 120) == HIDWAY_ECHO_PASS);
    CHECK(hidway_echo_expire(&e, t + 2000) == HIDWAY_ECHO_OK && cas_calls > 0);
    /* a release of a key held before ARM passes and changes nothing */
    CHECK(hidway_echo_input(&e, 0x05, true, t + 130) == HIDWAY_ECHO_PASS && !e.ours[0x05]);
    /* no CAS function: plain single-thread updates behave the same */
    hidway_echo_reset(&e, NULL);
    hidway_echo_grant(&e, &down, t);
    CHECK(hidway_echo_input(&e, KEY_A, false, t + 5) == HIDWAY_ECHO_PASS);

    /* ---- modifiers and buttons use their own slots ---- */
    hidway_echo_reset(&e, counting_cas);
    hidway_echo_frame_t m = none;
    m.mods = 0x11; /* LCtrl + RCtrl */
    m.buttons = 0x01;
    hidway_echo_grant(&e, &m, t);
    CHECK(hidway_echo_live(&e, 0xE0, false, t) == 1 && hidway_echo_live(&e, 0xE4, false, t) == 1 &&
          hidway_echo_live(&e, 0xE1, false, t) == 0 && hidway_echo_live(&e, BTN_LEFT, false, t) == 1);
    CHECK(hidway_echo_input(&e, BTN_LEFT, false, t + 5) == HIDWAY_ECHO_PASS);
    CHECK(hidway_echo_input(&e, BTN_LEFT + 1, false, t + 6) == HIDWAY_ECHO_OWN); /* right: physical */
    hidway_echo_grant(&e, &none, t + 10);
    CHECK(hidway_echo_live(&e, 0xE0, true, t + 10) == 1 && hidway_echo_live(&e, BTN_LEFT, true, t + 10) == 1);
    CHECK(hidway_echo_input(&e, HIDWAY_ECHO_SLOTS, false, t) == HIDWAY_ECHO_PASS); /* out of range */

    /* ---- the Pico reports six keys; a seventh waits for a free slot ---- */
    hidway_echo_reset(&e, counting_cas);
    uint8_t seven[] = {4, 5, 6, 7, 8, 9, 10};
    hidway_echo_frame_t f7 = frame_keys(seven, 7);
    hidway_echo_grant(&e, &f7, t);
    CHECK(hidway_echo_live(&e, 9, false, t) == 1 && hidway_echo_live(&e, 10, false, t) == 0 &&
          e.pico_down[9] && !e.pico_down[10]);
    hidway_echo_frame_t f6 = frame_keys(seven + 1, 6); /* 4 released: 10 takes its slot */
    hidway_echo_grant(&e, &f6, t + 10);
    CHECK(hidway_echo_live(&e, 4, true, t + 10) == 1 && hidway_echo_live(&e, 10, false, t + 10) == 1 &&
          e.pico_down[10] && !e.pico_down[4]);

    /* ---- expiry: about 250 ms, then dropped ---- */
    hidway_echo_reset(&e, counting_cas);
    hidway_echo_grant(&e, &down, t);
    CHECK(hidway_echo_live(&e, KEY_A, false, t + HIDWAY_ECHO_LIFETIME_MS - 1) == 1);
    CHECK(hidway_echo_live(&e, KEY_A, false, t + HIDWAY_ECHO_LIFETIME_MS) == 0);
    /* an expired credit is not an echo any more: a late echo is a press */
    CHECK(hidway_echo_input(&e, KEY_A, false, t + 300) == HIDWAY_ECHO_PASS); /* Pico holds A: repeat rule */
    hidway_echo_reset(&e, counting_cas);
    hidway_echo_grant(&e, &down, t);
    hidway_echo_grant(&e, &none, t + 10);
    CHECK(hidway_echo_expire(&e, t + 300) == HIDWAY_ECHO_OK && hidway_echo_live(&e, KEY_A, false, t + 300) == 0 &&
          e.credit[KEY_A] == 0 && e.silent_count == 2);
    /* a new grant never revives an expired credit */
    hidway_echo_reset(&e, counting_cas);
    hidway_echo_grant(&e, &down, t);
    hidway_echo_grant(&e, &none, t + 400);
    CHECK(hidway_echo_live(&e, KEY_A, false, t + 400) == 0 && hidway_echo_live(&e, KEY_A, true, t + 400) == 1 &&
          e.expired_pending == 1);

    /* ---- guard: unanswered edges with no echo at all ---- */
    hidway_echo_reset(&e, counting_cas);
    uint32_t s = t;
    for (int i = 0; i < 2; i++) { /* four edges: not yet */
        hidway_echo_grant(&e, &down, s);
        hidway_echo_grant(&e, &none, s + 10);
        s += 300;
        CHECK(hidway_echo_expire(&e, s) == HIDWAY_ECHO_OK);
    }
    hidway_echo_grant(&e, &down, s); /* the fifth within three seconds */
    CHECK(hidway_echo_expire(&e, s + 300) == HIDWAY_ECHO_FAULT_SILENT);
    /* an echo in between starts the count again */
    hidway_echo_reset(&e, counting_cas);
    s = t;
    for (int i = 0; i < 6; i++) {
        hidway_echo_grant(&e, i % 2 ? &none : &down, s);
        if (i == 3)
            hidway_echo_input(&e, KEY_A, true, s + 5); /* answered */
        s += 300;
        CHECK(hidway_echo_expire(&e, s) == HIDWAY_ECHO_OK);
    }
    /* five unanswered edges spread over more than three seconds: no fault */
    hidway_echo_reset(&e, counting_cas);
    s = t;
    for (int i = 0; i < 6; i++) {
        hidway_echo_grant(&e, i % 2 ? &none : &down, s);
        s += 800;
        CHECK(hidway_echo_expire(&e, s) == HIDWAY_ECHO_OK);
    }

    /* ---- guard: an implausible rate of consumed edges of one key ---- */
    hidway_echo_reset(&e, counting_cas);
    s = t;
    for (unsigned i = 0; i < HIDWAY_ECHO_LOOP_LIMIT / 2; i++) { /* 50 edges within a second: allowed */
        hidway_echo_input(&e, KEY_A, false, s);
        hidway_echo_input(&e, KEY_A, true, s + 5);
        s += 15;
    }
    CHECK(!e.loop_slot && hidway_echo_expire(&e, s) == HIDWAY_ECHO_OK);
    CHECK(hidway_echo_input(&e, KEY_A, false, s) == HIDWAY_ECHO_OWN && e.loop_slot == KEY_A + 1);
    CHECK(hidway_echo_expire(&e, s) == HIDWAY_ECHO_FAULT_LOOP);
    /* the same count spread over two seconds is fine */
    hidway_echo_reset(&e, counting_cas);
    s = t;
    for (unsigned i = 0; i < HIDWAY_ECHO_LOOP_LIMIT; i++) {
        hidway_echo_input(&e, KEY_A, false, s);
        hidway_echo_input(&e, KEY_A, true, s + 5);
        s += 40;
    }
    CHECK(!e.loop_slot);

    /* ---- typematic repeats from both keyboards: at most one per 25 ms ---- */
    hidway_echo_reset(&e, counting_cas);
    CHECK(hidway_echo_input(&e, KEY_A, false, t) == HIDWAY_ECHO_OWN);
    hidway_echo_grant(&e, &down, t + 1);
    CHECK(hidway_echo_input(&e, KEY_A, false, t + 15) == HIDWAY_ECHO_PASS);
    unsigned passed = 0, swallowed = 0, owned = 0;
    for (uint32_t r = t + 500; r < t + 500 + 33 * 30; r += 33) {
        int v1 = hidway_echo_input(&e, KEY_A, false, r);     /* this PC's keyboard repeats */
        int v2 = hidway_echo_input(&e, KEY_A, false, r + 3); /* the Pico's keyboard repeats */
        passed += (v1 == HIDWAY_ECHO_PASS) + (v2 == HIDWAY_ECHO_PASS);
        swallowed += (v1 == HIDWAY_ECHO_SWALLOW) + (v2 == HIDWAY_ECHO_SWALLOW);
        owned += (v1 == HIDWAY_ECHO_OWN) + (v2 == HIDWAY_ECHO_OWN);
    }
    CHECK(passed == 30 && swallowed == 30 && owned == 0);
    CHECK(hidway_echo_input(&e, KEY_A, true, t + 2000) == HIDWAY_ECHO_OWN);
    hidway_echo_grant(&e, &none, t + 2001);
    CHECK(hidway_echo_input(&e, KEY_A, true, t + 2020) == HIDWAY_ECHO_PASS && !e.ours[KEY_A]);
    /* a held key that is not sent as a key (a mouse-button alias): repeats swallowed */
    CHECK(hidway_echo_input(&e, 0x35, false, t + 3000) == HIDWAY_ECHO_OWN);
    CHECK(hidway_echo_input(&e, 0x35, false, t + 3500) == HIDWAY_ECHO_SWALLOW);
    CHECK(hidway_echo_input(&e, 0x35, true, t + 3600) == HIDWAY_ECHO_OWN);

    /* ---- wheel: sign and amount ---- */
    hidway_echo_reset(&e, counting_cas);
    CHECK(hidway_echo_wheel(&e, 120, t) == HIDWAY_ECHO_OWN); /* no credit: this PC's wheel */
    hidway_echo_frame_t w = none;
    w.wheel = 1;
    hidway_echo_grant(&e, &w, t + 1);
    CHECK(hidway_echo_live_wheel(&e, t + 1) == 1);
    CHECK(hidway_echo_wheel(&e, -120, t + 5) == HIDWAY_ECHO_OWN); /* wrong sign */
    CHECK(hidway_echo_wheel(&e, 30, t + 5) == HIDWAY_ECHO_OWN);   /* high-resolution: never the Pico */
    CHECK(hidway_echo_wheel(&e, 240, t + 5) == HIDWAY_ECHO_OWN);  /* more than the credit */
    CHECK(hidway_echo_wheel(&e, 120, t + 6) == HIDWAY_ECHO_PASS && hidway_echo_live_wheel(&e, t + 6) == 0);
    w.wheel = 4; /* +3 */
    hidway_echo_grant(&e, &w, t + 10);
    w.wheel = 2; /* then -2 */
    hidway_echo_grant(&e, &w, t + 12);
    CHECK(hidway_echo_wheel(&e, 240, t + 20) == HIDWAY_ECHO_PASS);
    CHECK(hidway_echo_wheel(&e, -240, t + 21) == HIDWAY_ECHO_PASS);
    CHECK(hidway_echo_wheel(&e, 120, t + 22) == HIDWAY_ECHO_PASS && e.wheel == 0);
    w.wheel = (int16_t)-32767; /* wrap: a large jump stays one bounded credit */
    hidway_echo_grant(&e, &w, t + 30);
    CHECK(hidway_echo_live_wheel(&e, t + 30) != 0);
    CHECK(hidway_echo_live_wheel(&e, t + 30 + HIDWAY_ECHO_LIFETIME_MS) == 0);
    CHECK(hidway_echo_wheel(&e, 0, t) == HIDWAY_ECHO_PASS);

    /* ---- a key held by Virtual while this PC presses the same key ---- */
    hidway_echo_reset(&e, counting_cas);
    hidway_echo_grant(&e, &down, t);                                         /* virtual A */
    CHECK(hidway_echo_input(&e, KEY_A, false, t + 10) == HIDWAY_ECHO_PASS); /* its echo */
    CHECK(hidway_echo_input(&e, KEY_A, false, t + 100) == HIDWAY_ECHO_PASS); /* physical: shown as held */
    CHECK(!e.ours[KEY_A]);
    CHECK(hidway_echo_input(&e, KEY_A, true, t + 150) == HIDWAY_ECHO_PASS && !e.ours[KEY_A]);
    hidway_echo_grant(&e, &none, t + 200);                                   /* virtual releases */
    CHECK(hidway_echo_input(&e, KEY_A, true, t + 210) == HIDWAY_ECHO_PASS);
    CHECK(!e.ours[KEY_A] && !e.pico_down[KEY_A] && hidway_echo_expire(&e, t + 1000) == HIDWAY_ECHO_OK);

    /* ---- closed loop: fast taps, second press before the first echo ---- */
    static const uint32_t latencies[] = {1, 5, 20, 50, 100, 200};
    static const uint32_t gaps[] = {3, 10, 25, 40, 80};
    for (size_t li = 0; li < sizeof latencies / sizeof latencies[0]; li++)
        for (size_t gi = 0; gi < sizeof gaps / sizeof gaps[0]; gi++) {
            sim_reset();
            uint32_t at = t, gap = gaps[gi];
            for (int tap = 0; tap < 4; tap++) {
                push(at, EV_PHYS, KEY_A, false);
                push(at + gap, EV_PHYS, KEY_A, true);
                at += 2 * gap;
            }
            int fault = 0;
            unsigned steps = sim_run(latencies[li], &fault);
            if (!steps || !settled_clean() || fault)
                printf("taps: latency %u gap %u steps %u fault %d\n", (unsigned)latencies[li], (unsigned)gap,
                       steps, fault);
            CHECK(steps != 0);       /* settles: no endless loop */
            CHECK(settled_clean());  /* no stuck key, here or on the Pico */
            CHECK(fault == 0);
        }
    /* two keys interleaved, and a Virtual hold during physical taps */
    sim_reset();
    push(t, EV_PHYS, 4, false);
    push(t + 5, EV_PHYS, 5, false);
    push(t + 8, EV_PHYS, 4, true);
    push(t + 9, EV_PHYS, 4, false);
    push(t + 12, EV_PHYS, 5, true);
    push(t + 14, EV_PHYS, 4, true);
    int fault2 = 0;
    CHECK(sim_run(30, &fault2) != 0 && settled_clean() && !os_down[5] && !fault2);
    sim_reset();
    virt_held[KEY_A] = 1;
    sim_send(t, 20);
    push(t + 50, EV_PHYS, KEY_A, false);
    push(t + 70, EV_PHYS, KEY_A, true);
    /* Known limit: this PC's own release passes, so this PC shows A released
     * while Virtual still holds it on the Pico; nothing is left owned. */
    CHECK(sim_run(20, &fault2) != 0 && !fault2 && !os_down[KEY_A] && !phys_held[KEY_A] && !e.ours[KEY_A] &&
          e.pico_down[KEY_A]);
    virt_held[KEY_A] = 0;
    sim_send(t + 500, 20);
    CHECK(sim_run(20, &fault2) != 0 && !fault2 && settled_clean());

    /* ---- the Pico does not answer at all: the silent guard trips ---- */
    sim_reset();
    hidway_echo_input(&e, KEY_A, false, t);
    hidway_echo_grant(&e, &down, t + 1);
    hidway_echo_input(&e, KEY_A, true, t + 50);
    hidway_echo_grant(&e, &none, t + 51);
    hidway_echo_input(&e, 5, false, t + 100);
    uint8_t b5[] = {5};
    hidway_echo_frame_t d5 = frame_keys(b5, 1);
    hidway_echo_grant(&e, &d5, t + 101);
    hidway_echo_input(&e, 5, true, t + 150);
    hidway_echo_grant(&e, &none, t + 151);
    CHECK(hidway_echo_expire(&e, t + 500) == HIDWAY_ECHO_OK); /* four */
    hidway_echo_grant(&e, &down, t + 600);
    CHECK(hidway_echo_expire(&e, t + 900) == HIDWAY_ECHO_FAULT_SILENT);

    /* ---- disarm clears every credit, ownership and guard ---- */
    hidway_echo_input(&e, 6, false, t);
    hidway_echo_reset(&e, counting_cas);
    unsigned live = 0, owned_after = 0;
    for (unsigned slot = 0; slot < HIDWAY_ECHO_SLOTS; slot++) {
        live += hidway_echo_live(&e, slot, false, t + 900) + hidway_echo_live(&e, slot, true, t + 900);
        owned_after += e.ours[slot] + e.pico_down[slot];
    }
    CHECK(!live && !owned_after && !e.loop_slot && !e.silent_count && !hidway_echo_live_wheel(&e, t) &&
          e.cas == counting_cas && hidway_echo_expire(&e, t + 5000) == HIDWAY_ECHO_OK);

    printf("Echo credit tests: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
