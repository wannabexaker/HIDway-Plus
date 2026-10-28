#include "hid_out.h"
#include "pico_mock.h"
#include "usb_descriptors.h"
#include <stdio.h>
#include <string.h>
static bool ready, mounted, suspended, send_ok;
static bool key_busy, stall_after_key;
static uint8_t mouse_mods[256];
static int mouse_x[256];
static unsigned mouse_count, key_count, failures, wakes;
static int64_t mock_now;
static uint8_t buttons[256], keys_out[256][6], mods_out[256];
bool tud_hid_n_ready(uint8_t n) { return ready && (n != ITF_KEYBOARD || !key_busy); }
bool tud_hid_n_keyboard_report(uint8_t n, uint8_t id, uint8_t mods, const uint8_t keys[6])
{
    (void)n;
    (void)id;
    if (!send_ok || key_count >= 256)
        return false;
    memcpy(keys_out[key_count], keys, 6);
    mods_out[key_count++] = mods;
    if (stall_after_key)
        key_busy = true;
    return true;
}
bool tud_hid_n_report(uint8_t n, uint8_t id, const void *data, uint16_t len)
{
    (void)n;
    (void)id;
    (void)len;
    if (!send_ok || mouse_count >= 256)
        return false;
    const uint8_t *bytes = data;
    buttons[mouse_count] = bytes[0];
    mouse_mods[mouse_count] = key_count ? mods_out[key_count - 1] : 0;
    mouse_x[mouse_count] = len == 7 ? (int16_t)((unsigned)bytes[1] | ((unsigned)bytes[2] << 8)) : 0;
    mouse_count++;
    return true;
}
uint8_t tud_hid_n_get_protocol(uint8_t n)
{
    (void)n;
    return 1;
}
bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return suspended; }
bool tud_remote_wakeup(void)
{
    wakes++;
    return true;
}
absolute_time_t get_absolute_time(void) { return mock_now; }
bool time_reached(absolute_time_t t) { return t <= mock_now; }
absolute_time_t make_timeout_time_us(uint32_t us) { return mock_now + us; }
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
static void reset(void)
{
    ready = mounted = send_ok = true;
    key_busy = stall_after_key = false;
    suspended = false;
    mouse_count = key_count = wakes = 0;
    mock_now = 0;
    memset(buttons, 0, sizeof buttons);
    memset(keys_out, 0, sizeof keys_out);
    hid_out_init();
}
int main(void)
{
    uint8_t key[HIDWAY_KEY_BITMAP_BYTES] = {0}, none[HIDWAY_KEY_BITMAP_BYTES] = {0};
    hidway_bitmap_set(key, 4);
    reset();
    hid_out_set_buttons(1);
    hid_out_set_buttons(0);
    hid_out_task();
    hid_out_task();
    CHECK(mouse_count == 2 && buttons[0] == 1 && buttons[1] == 0);
    reset();
    hid_out_set_keyboard(2, key);
    hid_out_set_keyboard(0, none);
    hid_out_task();
    hid_out_task();
    CHECK(key_count == 2 && keys_out[0][0] == 4 && mods_out[0] == 2 && keys_out[1][0] == 0 &&
          mods_out[1] == 0);
    reset();
    ready = false;
    hid_out_set_buttons(1);
    hid_out_set_buttons(0);
    mock_now = 100001;
    hid_out_task();
    CHECK(hid_out_overloaded());
    ready = true;
    hid_out_task();
    CHECK(mouse_count == 1 && !buttons[0]); /* expired click is cancelled, never replayed */
    reset();
    ready = false;
    hid_out_set_buttons(1);
    hid_out_task();
    hid_out_set_buttons(0);
    hid_out_task();
    CHECK(mouse_count == 0);
    ready = true;
    hid_out_task();
    hid_out_task();
    CHECK(mouse_count == 2 && buttons[0] == 1 && !buttons[1]);
    reset();
    send_ok = false;
    hid_out_set_buttons(1);
    hid_out_task();
    CHECK(mouse_count == 0);
    send_ok = true;
    hid_out_task();
    CHECK(mouse_count == 1 && buttons[0] == 1);
    reset();
    hid_out_set_buttons(1);
    hid_out_set_keyboard(2, key);
    hid_out_release_all();
    hid_out_task();
    CHECK(mouse_count == 1 && !buttons[0] && key_count == 1 && !keys_out[0][0] && !mods_out[0]);
    reset();
    for (unsigned i = 0; i < 65; i++)
        hid_out_set_buttons((i % 2) ? 0 : 1);
    CHECK(hid_out_overloaded());
    hid_out_task();
    CHECK(mouse_count == 1 && !buttons[0]);
    hid_out_set_buttons(1);
    hid_out_task();
    CHECK(mouse_count == 1);
    hid_out_release_all();
    CHECK(!hid_out_overloaded());
    hid_out_set_buttons(1);
    hid_out_task();
    hid_out_task(); /* finish priority neutral report before accepting a new hold */
    CHECK(buttons[mouse_count - 1] == 1);
    reset();
    for (unsigned i = 0; i < 65; i++)
        hid_out_set_keyboard(0, (i % 2) ? none : key);
    CHECK(hid_out_overloaded());
    hid_out_task();
    CHECK(key_count == 1 && !keys_out[0][0]);
    reset();
    hid_out_set_buttons(1);
    hid_out_set_buttons(0);
    mounted = false;
    hid_out_task();
    mounted = true;
    hid_out_task();
    CHECK(mouse_count == 1 && !buttons[0]);
    reset();
    hid_out_set_buttons(1);
    hid_out_set_buttons(0);
    suspended = true;
    hid_out_task();
    CHECK(mouse_count == 0 && wakes == 1);
    suspended = false;
    hid_out_task();
    CHECK(mouse_count == 1 && !buttons[0]);
    reset();
    for (uint8_t i = 5; i < 11; i++)
        hidway_bitmap_set(key, i);
    hid_out_set_keyboard(0, key);
    hid_out_task();
    CHECK(keys_out[0][0] == 4 && keys_out[0][5] == 9);
    reset();
    hid_out_set_keyboard(1, none);
    hid_out_set_keyboard(5, none);
    hid_out_set_buttons(1);
    hid_out_add_motion(10, 0, 0, 0);
    hid_out_set_buttons(0);
    hid_out_set_keyboard(0, none);
    for (unsigned i = 0; i < 6; i++)
        hid_out_task();
    CHECK(key_count == 3 && mods_out[0] == 1 && mods_out[1] == 5 && mods_out[2] == 0);
    CHECK(mouse_count == 2 && buttons[0] == 1 && buttons[1] == 0 && mouse_mods[0] == 5 &&
          mouse_mods[1] == 5 && mouse_x[0] == 10);
    reset();
    hid_out_set_keyboard(1, key);
    hid_out_set_buttons(1);
    stall_after_key = true;
    hid_out_task();
    hid_out_task();
    CHECK(key_count == 1 && mouse_count == 0);
    key_busy = stall_after_key = false;
    hid_out_task();
    CHECK(mouse_count == 1 && buttons[0] == 1);
    reset();
    hid_out_add_motion(10, 0, 0, 0);
    hid_out_set_buttons(1);
    hid_out_task();
    hid_out_task();
    CHECK(mouse_count == 2 && !buttons[0] && mouse_x[0] == 10 && buttons[1] == 1);
    printf("Firmware ordered USB edge tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
