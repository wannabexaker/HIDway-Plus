#include "hid_out.h"
#include "hidway_motion.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

#define EDGE_CAPACITY 64u
#define EDGE_MAX_AGE_US 100000
#define REMOTE_WAKEUP_INTERVAL_US 100000

typedef struct {
    uint8_t mods, keys[HIDWAY_BOOT_KEYS], buttons;
    hidway_motion_t motion;
    absolute_time_t deadline;
} output_frame_t;

static struct {
    uint8_t desired_mods, desired_buttons;
    hidway_kro6_t desired_keys;
    output_frame_t pending[EDGE_CAPACITY], current;
    unsigned head, count;
    bool loaded, confirmed, kb_dirty, mouse_dirty, overloaded;
    hidway_motion_t motion;
    absolute_time_t motion_deadline, next_wakeup;
    uint8_t leds;
} st;

static void neutral(bool fault)
{
    st.head = st.count = 0;
    st.loaded = st.confirmed = false;
    st.desired_mods = st.desired_buttons = 0;
    hidway_kro6_reset(&st.desired_keys);
    memset(&st.current, 0, sizeof st.current);
    hidway_motion_clear(&st.motion);
    st.kb_dirty = st.mouse_dirty = true;
    st.overloaded = fault;
}

static void queue_state(void)
{
    if (st.count == EDGE_CAPACITY) {
        neutral(true);
        return;
    }
    output_frame_t *f = &st.pending[(st.head + st.count) % EDGE_CAPACITY];
    memset(f, 0, sizeof *f);
    f->mods = st.desired_mods;
    f->buttons = st.desired_buttons;
    memcpy(f->keys, st.desired_keys.slots, sizeof f->keys);
    f->deadline = make_timeout_time_us(EDGE_MAX_AGE_US);
    st.count++;
}

void hid_out_init(void)
{
    memset(&st, 0, sizeof st);
    st.next_wakeup = get_absolute_time();
}

bool hid_out_overloaded(void) { return st.overloaded; }

void hid_out_set_keyboard(uint8_t modifiers, const uint8_t bitmap[HIDWAY_KEY_BITMAP_BYTES])
{
    if (st.overloaded)
        return;
    bool changed = st.desired_mods != modifiers;
    st.desired_mods = modifiers;
    if (hidway_kro6_update(&st.desired_keys, bitmap))
        changed = true;
    if (changed)
        queue_state();
}

void hid_out_set_buttons(uint8_t buttons)
{
    if (st.overloaded)
        return;
    buttons &= HIDWAY_MOUSE_BUTTONS_MASK;
    if (buttons != st.desired_buttons) {
        st.desired_buttons = buttons;
        queue_state();
    }
}

void hid_out_add_motion(int32_t dx, int32_t dy, int32_t wheel, int32_t pan)
{
    if (st.overloaded)
        return;
    if (st.count) {
        output_frame_t *f = &st.pending[(st.head + st.count - 1) % EDGE_CAPACITY];
        hidway_motion_add(&f->motion, dx, dy, wheel, pan);
    } else {
        if (!hidway_motion_pending(&st.motion))
            st.motion_deadline = make_timeout_time_us(EDGE_MAX_AGE_US);
        hidway_motion_add(&st.motion, dx, dy, wheel, pan);
    }
}

void hid_out_release_all(void) { neutral(false); }
uint8_t hid_out_leds(void) { return st.leds; }

static bool endpoints_ready(void)
{
    return tud_hid_n_ready(ITF_KEYBOARD) && tud_hid_n_ready(ITF_MOUSE);
}

static void load_frame(void)
{
    output_frame_t *f = &st.pending[st.head];
    st.kb_dirty =
        f->mods != st.current.mods || memcmp(f->keys, st.current.keys, sizeof f->keys) != 0;
    st.mouse_dirty = f->buttons != st.current.buttons;
    st.current = *f;
    if (hidway_motion_pending(&f->motion)) {
        st.motion = f->motion;
        st.motion_deadline = f->deadline;
    }
    st.head = (st.head + 1) % EDGE_CAPACITY;
    st.count--;
    st.loaded = true;
    st.confirmed = false;
}

static void send_keyboard(void)
{
    if (st.kb_dirty && tud_hid_n_ready(ITF_KEYBOARD) &&
        tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, st.current.mods, st.current.keys))
        st.kb_dirty = false;
}

static void put_le16(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)((uint16_t)v & 0xff);
    p[1] = (uint8_t)((uint16_t)v >> 8);
}

static void send_mouse(void)
{
    if ((!st.mouse_dirty && !hidway_motion_pending(&st.motion)) || !tud_hid_n_ready(ITF_MOUSE))
        return;
    /* Confirm the keyboard report before issuing a dependent mouse action.
     * Two independent endpoint queues would reorder Ctrl+Alt+click. */
    if (st.kb_dirty || !tud_hid_n_ready(ITF_KEYBOARD))
        return;
    hidway_motion_t chunk;
    bool sent;
    if (tud_hid_n_get_protocol(ITF_MOUSE) == HID_PROTOCOL_BOOT) {
        st.motion.wheel = st.motion.pan = 0;
        hidway_motion_take(&st.motion, 127, 0, &chunk);
        uint8_t report[HIDWAY_MOUSE_BOOT_REPORT_LEN] = {
            (uint8_t)(st.current.buttons & 7u), (uint8_t)(int8_t)chunk.x, (uint8_t)(int8_t)chunk.y};
        sent = tud_hid_n_report(ITF_MOUSE, 0, report, sizeof report);
    } else {
        hidway_motion_take(&st.motion, 32767, 127, &chunk);
        uint8_t report[HIDWAY_MOUSE_REPORT_LEN];
        report[0] = st.current.buttons;
        put_le16(&report[1], (int16_t)chunk.x);
        put_le16(&report[3], (int16_t)chunk.y);
        report[5] = (uint8_t)(int8_t)chunk.wheel;
        report[6] = (uint8_t)(int8_t)chunk.pan;
        sent = tud_hid_n_report(ITF_MOUSE, 0, report, sizeof report);
    }
    if (sent)
        st.mouse_dirty = false;
    else
        hidway_motion_give_back(&st.motion, &chunk);
}

void hid_out_task(void)
{
    if (!tud_mounted()) {
        hid_out_release_all();
        return;
    }
    if (tud_suspended()) {
        bool changed = st.kb_dirty || st.mouse_dirty || st.count || st.loaded;
        st.count = 0;
        st.loaded = st.confirmed = false;
        st.current.mods = st.desired_mods;
        st.current.buttons = st.desired_buttons;
        memcpy(st.current.keys, st.desired_keys.slots, sizeof st.current.keys);
        hidway_motion_clear(&st.motion);
        st.kb_dirty = st.mouse_dirty = changed;
        if (changed && time_reached(st.next_wakeup)) {
            st.next_wakeup = make_timeout_time_us(REMOTE_WAKEUP_INTERVAL_US);
            tud_remote_wakeup();
        }
        return;
    }
    if (st.loaded && !st.kb_dirty && !st.mouse_dirty && endpoints_ready())
        st.confirmed = true;
    if ((st.count && time_reached(st.pending[st.head].deadline)) ||
        (st.loaded && !st.confirmed && time_reached(st.current.deadline)) ||
        (hidway_motion_pending(&st.motion) && time_reached(st.motion_deadline)))
        neutral(true);
    if (st.loaded && st.confirmed && !hidway_motion_pending(&st.motion) && endpoints_ready())
        st.loaded = false;
    if (!st.loaded && st.count && !st.kb_dirty && !st.mouse_dirty &&
        !hidway_motion_pending(&st.motion) && endpoints_ready())
        load_frame();
    send_keyboard();
    send_mouse();
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t bufsize)
{
    (void)report_id;
    if (instance == ITF_KEYBOARD && report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1)
        st.leds = buffer[0];
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol)
{
    (void)protocol;
    if (instance == ITF_KEYBOARD)
        st.kb_dirty = true;
    else
        st.mouse_dirty = true;
}
