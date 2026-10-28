#include "hidway_input_source.h"
#include "hidway_input_state.h"
#include <limits.h>
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
int main(void)
{
    hidway_input_state_t s = {0};
    hidway_held_t h;
    unsigned sources[3] = {HIDWAY_SOURCE_PHYSICAL, HIDWAY_SOURCE_TOUCHPAD, HIDWAY_SOURCE_VIRTUAL};
    for (unsigned reverse = 0; reverse < 2; reverse++) {
        memset(&s, 0, sizeof s);
        for (unsigned o = 0; o < 3; o++) {
            hidway_input_key(&s, sources[o], 65, 4, false);
            hidway_input_key(&s, sources[o], 16, HIDWAY_BIND_MOD | 2, false);
            hidway_input_key(&s, sources[o], 119, HIDWAY_BIND_MOUSE | 8, false);
            hidway_input_button(&s, sources[o], 1, false);
        }
        for (unsigned i = 0; i < 3; i++) {
            unsigned o = reverse ? 2 - i : i;
            hidway_input_key(&s, sources[o], 65, 0, true);
            hidway_input_key(&s, sources[o], 16, 0, true);
            hidway_input_key(&s, sources[o], 119, 0, true);
            hidway_input_button(&s, sources[o], 1, true);
            hidway_input_merge(&s, &h);
            CHECK(hidway_bitmap_test(h.keys, 4) == (i < 2));
            CHECK(h.mods == (i < 2 ? 2 : 0) && h.buttons == (i < 2 ? 1 : 0) &&
                  h.aliases == (i < 2 ? 8 : 0));
        }
    }
    hidway_input_key(&s, 1, 65, 4, false);
    hidway_input_key(&s, 1, 65, 5, false);
    hidway_input_merge(&s, &h);
    CHECK(hidway_bitmap_test(h.keys, 4) && !hidway_bitmap_test(h.keys, 5));
    hidway_input_key(&s, 1, 65, 0, true);
    hidway_input_merge(&s, &h);
    CHECK(!hidway_bitmap_test(h.keys, 4));
    hidway_key_queue_t q = {0};
    hidway_key_frame_t f = {0};
    hidway_bitmap_set(f.keys, 4);
    CHECK(hidway_key_queue_push(&q, f));
    memset(&f, 0, sizeof f);
    CHECK(hidway_key_queue_push(&q, f));
    CHECK(hidway_bitmap_test(hidway_key_queue_sample(&q, 0).keys, 4));
    CHECK(hidway_bitmap_test(hidway_key_queue_sample(&q, 7).keys, 4));
    CHECK(!hidway_bitmap_test(hidway_key_queue_sample(&q, 8).keys, 4));
    hidway_pulse_t p = {0};
    hidway_pulse_input(&p, true, 0);
    hidway_pulse_input(&p, false, 1);
    CHECK(hidway_pulse_sample(&p, 4, 10, false));
    CHECK(hidway_pulse_sample(&p, 19, 10, false));
    CHECK(!hidway_pulse_sample(&p, 20, 10, false));
    hidway_pulse_input(&p, true, 21);
    hidway_pulse_input(&p, false, 22);
    CHECK(!hidway_pulse_sample(&p, 105, 10, false) && p.limited == 1);
    hidway_pulse_input(&p, true, 106);
    CHECK(hidway_pulse_sample(&p, 106, 10, false));
    CHECK(hidway_pulse_sample(&p, 100000, 10, false)); /* hold/drag is not a click train */
    hidway_pulse_input(&p, false, 100001);
    CHECK(!hidway_pulse_sample(&p, 100001, 10, false));
    memset(&p, 0, sizeof p);
    unsigned downs = 0, ups = 0;
    bool last = false;
    for (uint32_t t = 0; t < 60000; t++) {
        hidway_pulse_input(&p, (t % 2) == 0, t);
        if (t % 4 == 0) {
            bool v = hidway_pulse_sample(&p, t, 10, false);
            downs += v && !last;
            ups += !v && last;
            last = v;
        }
    }
    hidway_pulse_input(&p, false, 60000);
    for (uint32_t t = 60000; t < 60200; t += 4) {
        bool v = hidway_pulse_sample(&p, t, 10, false);
        ups += !v && last;
        last = v;
    }
    CHECK(downs <= 601 && downs >= 590 && ups == downs && !last && p.limited > 20000);
    for (unsigned cps = 5; cps <= 20; cps *= 2) {
        memset(&p, 0, sizeof p);
        hidway_pulse_input(&p, true, 0);
        downs = 0;
        last = false;
        for (uint32_t t = 0; t < 10000; t += 4) {
            bool v = hidway_pulse_sample(&p, t, cps, true);
            downs += v && !last;
            last = v;
        }
        CHECK(downs >= cps * 9 && downs <= cps * 10 + 1);
        hidway_pulse_input(&p, false, 10000);
        CHECK(!hidway_pulse_sample(&p, 10100, cps, true));
    }
    memset(&p, 0, sizeof p);
    hidway_pulse_input(&p, true, 0);
    CHECK(hidway_pulse_sample(&p, 0, 10, true));
    CHECK(!hidway_pulse_sample(&p, 5000, 10, true)); /* no catch-up burst after a stall */
    CHECK(!hidway_pulse_sample(&p, 5004, 10, true));
    CHECK(hidway_pulse_sample(&p, 5052, 10, true));
    memset(&p, 0, sizeof p);
    hidway_pulse_input(&p, true, UINT_MAX - 8);
    hidway_pulse_input(&p, false, UINT_MAX - 7);
    CHECK(hidway_pulse_sample(&p, UINT_MAX - 8, 10, false));
    CHECK(!hidway_pulse_sample(&p, 8, 10, false));
    /* A single ordered journal preserves cross-device modifier/click order. */
    hidway_event_queue_t journal = {0};
    hidway_event_frame_t frame = {0}, out;
    frame.keyboard.mods = 1;
    CHECK(hidway_event_push(&journal, frame, 0));
    frame.keyboard.mods = 5;
    CHECK(hidway_event_push(&journal, frame, 1));
    frame.buttons = 1;
    CHECK(hidway_event_push(&journal, frame, 2));
    frame.buttons = 0;
    CHECK(hidway_event_push(&journal, frame, 3));
    frame.keyboard.mods = 0;
    CHECK(hidway_event_push(&journal, frame, 4));
    CHECK(hidway_event_sample(&journal, frame, 4, &out) && out.keyboard.mods == 1 && !out.buttons);
    CHECK(hidway_event_sample(&journal, frame, 12, &out) && out.keyboard.mods == 5 && !out.buttons);
    CHECK(hidway_event_sample(&journal, frame, 28, &out) && out.keyboard.mods == 5 &&
          out.buttons == 1);
    CHECK(hidway_event_sample(&journal, frame, 44, &out) && out.keyboard.mods == 5 && !out.buttons);
    CHECK(hidway_event_sample(&journal, frame, 52, &out) && out.keyboard.mods == 0 && !out.buttons);
    frame.x = 19;
    CHECK(hidway_event_sample(&journal, frame, 56, &out) && out.x == 19);
    frame.buttons = 1;
    CHECK(hidway_event_push(&journal, frame, 60));
    CHECK(!hidway_event_sample(&journal, frame, 160, &out));
    printf("Input ownership, ordered journal and bounded pulse tests: %s\n",
           fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
