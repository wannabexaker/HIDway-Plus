#define WIN32_LEAN_AND_MEAN
#include "config.h"
#include "send_clock.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
static unsigned failures, ticks;
static DWORD seen_generation;
static hidway_send_clock_t clock_state;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_APP + 17) {
        seen_generation = (DWORD)w;
        ticks++;
        InterlockedExchange(&clock_state.posted, 0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}
static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}
static void read_file(const char *path, char *text, size_t cap)
{
    size_t n = 0;
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (f) {
        n = fread(text, 1, cap - 1, f);
        fclose(f);
    }
    text[n] = 0;
}
static int profile_same(const hidway_route_profile_t *a, const hidway_route_profile_t *b)
{
    return a->valid && b->valid && !strcmp(a->target, b->target) && a->port == b->port &&
           a->relay_keyboard == b->relay_keyboard && a->relay_mouse == b->relay_mouse &&
           a->source_physical == b->source_physical && a->source_touchpad == b->source_touchpad &&
           a->source_virtual == b->source_virtual && a->loopback_pointer_pico == b->loopback_pointer_pico &&
           a->repeat_enabled == b->repeat_enabled && a->repeat_button == b->repeat_button &&
           a->repeat_cps == b->repeat_cps && a->repeat_sources == b->repeat_sources &&
           a->virtual_click_cps == b->virtual_click_cps;
}
/* The remembered route `p` holds exactly the route values in use of `c`. */
static int profile_is(const hidway_route_profile_t *p, const hidway_config_t *c)
{
    hidway_config_t copy = *c;
    hidway_profile_store(&copy);
    return profile_same(p, &copy.profiles[copy.loopback_mode]);
}
/* Every value of the 0.4.1 file used below. */
static int legacy_values(const hidway_config_t *d)
{
    return !strcmp(d->target, "192.0.2.44") && d->port == 47811 && d->send_rate_hz == 333 &&
           d->auto_disarm_ms == 2500 && d->relay_keyboard == 1 && d->relay_mouse == 0 &&
           d->source_physical == 1 && d->source_touchpad == 1 && d->source_virtual == 1 &&
           d->loopback_mode == 0 && d->loopback_pointer_pico == 1 && d->virtual_click_cps == 7 &&
           d->repeat_enabled == 1 && d->repeat_button == 3 && d->repeat_cps == 14 && d->repeat_sources == 6 &&
           d->history_enabled == 0 && !strcmp(d->toggle.text, "ctrl+alt+f9") && d->toggle.vk == VK_F9 &&
           d->toggle.mods == (MOD_CONTROL | MOD_ALT) && !strcmp(d->panic.text, "ctrl+alt+shift+pause") &&
           d->mouse4.vk == VK_OEM_3 && !d->mouse5.vk && d->badge == 0 && d->badge_x == 640 && d->badge_y == 12 &&
           d->window_x == -1200 && d->window_y == 80 && !strcmp(d->loopback_device, "VID_1234&PID_5678") &&
           !strcmp(d->key_hex, "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
}
static void drain(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE))
        DispatchMessageA(&m);
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/platform-config.ini", argv[1]);
    static hidway_config_t c, d, e;
    memset(&c, 0xcc, sizeof c);
    hidway_config_load("missing-hidway-test-config", &c);
    CHECK(c.virtual_click_cps == 10 && c.repeat_cps == 10 && !c.repeat_enabled &&
          c.repeat_sources == 1 && !c.mouse4.vk && !c.loopback_pointer_pico);
    c.source_virtual = 1;
    c.source_physical = 0;
    c.repeat_enabled = 1;
    c.repeat_button = 4;
    c.repeat_cps = 25;
    c.repeat_sources = 7;
    c.loopback_mode = 1;
    c.loopback_pointer_pico = 1;
    CHECK(hidway_config_save(path, &c));
    CHECK(hidway_config_load(path, &d));
    CHECK(d.repeat_enabled && d.repeat_button == 4 && d.repeat_cps == 25 && d.repeat_sources == 7 &&
          d.loopback_mode == 1 && d.loopback_pointer_pico);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("source_physical=0\nsource_virtual=1\nrepeat_cps=999\nrepeat_button=-4\nrepeat_"
              "sources=0\n",
              f);
        fclose(f);
    }
    hidway_config_load(path, &d);
    CHECK(d.loopback_mode == 1 && d.repeat_cps == 25 && d.repeat_button == 0 &&
          d.repeat_sources == 1);
    /* ---- Route profiles ---- */
    /* No file: Remote PC in use, Gaming PC created as the same-PC preset. */
    hidway_config_load("missing-hidway-test-config", &c);
    CHECK(!c.loopback_mode && c.profiles[0].valid && c.profiles[1].valid);
    CHECK(profile_is(&c.profiles[0], &c));
    CHECK(c.profiles[1].source_virtual && !c.profiles[1].source_physical && !c.profiles[1].source_touchpad &&
          !c.profiles[1].loopback_pointer_pico && !c.profiles[1].repeat_enabled &&
          c.profiles[1].repeat_sources == 4 && c.profiles[1].relay_keyboard && c.profiles[1].relay_mouse &&
          !strcmp(c.profiles[1].target, c.target) && c.profiles[1].port == c.port);

    /* A 0.4.1 file (no prefixed keys, with `compact`) keeps every value on
     * load and on save; `compact` is accepted and no longer written. */
    static const char legacy[] =
        "# HIDway client configuration.\n\ntarget = 192.0.2.44\nport = 47811\n\nsend_rate_hz = 333\n"
        "auto_disarm_ms = 2500   # 0 = disabled\n\nrelay_keyboard = 1\nrelay_mouse = 0\n"
        "source_physical = 1\nsource_touchpad = 1\nsource_virtual = 1\nloopback_mode = 0\n"
        "loopback_pointer_pico = 1\nvirtual_click_cps = 7\nrepeat_enabled = 1\nrepeat_button = 3\n"
        "repeat_cps = 14\nrepeat_sources = 6\nhistory_enabled = 0\n\ntoggle_hotkey = ctrl+alt+f9\n"
        "panic_hotkey = ctrl+alt+shift+pause\n\nmouse4_key = grave\nbadge = 0   # note\nbadge_x = 640\n"
        "badge_y = 12\ncompact = 1   # 1 = compact window\nwindow_x = -1200\nwindow_y = 80\n\n"
        "loopback_device = VID_1234&PID_5678\n\nkey = 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n";
    write_file(path, legacy);
    memset(&d, 0xcc, sizeof d);
    CHECK(hidway_config_load(path, &d));
    CHECK(legacy_values(&d) && !d.loopback_mode && profile_is(&d.profiles[0], &d));
    /* the other route: same relay and speeds, preset choices */
    CHECK(!strcmp(d.profiles[1].target, "192.0.2.44") && d.profiles[1].port == 47811 &&
          d.profiles[1].source_virtual && !d.profiles[1].source_physical && !d.profiles[1].loopback_pointer_pico &&
          !d.profiles[1].repeat_enabled && d.profiles[1].repeat_sources == 4 && d.profiles[1].repeat_cps == 14 &&
          d.profiles[1].virtual_click_cps == 7 && d.profiles[1].relay_mouse);
    CHECK(hidway_config_save(path, &d));
    char text[8192];
    read_file(path, text, sizeof text);
    CHECK(!strstr(text, "compact") && strstr(text, "\nremote.target = 192.0.2.44\n") &&
          strstr(text, "\ngaming.source_virtual = 1\n") && strstr(text, "\ntarget = 192.0.2.44\n") &&
          strstr(text, "\nkey = 00112233"));
    memset(&e, 0xcc, sizeof e);
    CHECK(hidway_config_load(path, &e) && legacy_values(&e) && profile_is(&e.profiles[0], &e) &&
          profile_same(&e.profiles[1], &d.profiles[1]));

    /* A 0.4.1 file in Gaming PC mode: the top-level values stay the route in
     * use; the Remote PC route gets the source choices 0.4.1 kept for it. */
    write_file(path, "target = 192.0.2.45\nsource_physical = 1\nsource_touchpad = 1\nsource_virtual = 1\n"
                     "loopback_mode = 1\nrepeat_sources = 4\nrepeat_cps = 9\n");
    CHECK(hidway_config_load(path, &d) && d.loopback_mode == 1 && d.source_physical && d.source_touchpad &&
          d.source_virtual && d.repeat_cps == 9 && !strcmp(d.target, "192.0.2.45") && profile_is(&d.profiles[1], &d));
    CHECK(d.profiles[0].source_physical && d.profiles[0].source_touchpad && d.profiles[0].repeat_cps == 9 &&
          !strcmp(d.profiles[0].target, "192.0.2.45"));
    /* ... and source_physical = 1 there never switches physical-through-Pico on */
    CHECK(!d.loopback_physical);

    /* loopback_physical: off by default, its own top-level key, kept across
     * saves and route switches, never derived from source_physical. */
    hidway_config_load("missing-hidway-test-config", &c);
    CHECK(!c.loopback_physical);
    write_file(path, "loopback_mode = 1\nsource_physical = 1\nloopback_physical = 1\n");
    CHECK(hidway_config_load(path, &d) && d.loopback_physical && d.loopback_mode == 1);
    CHECK(hidway_config_save(path, &d));
    read_file(path, text, sizeof text);
    CHECK(strstr(text, "\nloopback_physical = 1") != NULL && !strstr(text, "remote.loopback_physical") &&
          !strstr(text, "gaming.loopback_physical"));
    CHECK(hidway_config_load(path, &e) && e.loopback_physical);
    hidway_profile_select(&e, 0);
    CHECK(!e.loopback_mode && e.loopback_physical); /* kept; it only acts on the Gaming PC route */
    hidway_profile_select(&e, 1);
    CHECK(e.loopback_mode && e.loopback_physical);
    e.loopback_physical = 0;
    CHECK(hidway_config_save(path, &e) && hidway_config_load(path, &d) && !d.loopback_physical);
    write_file(path, "loopback_physical = 7   # any non-zero is on\nloopback_mode = 0\n");
    CHECK(hidway_config_load(path, &d) && d.loopback_physical == 1 && !d.loopback_mode);
    /* A virtual-only Remote PC route from such a file is kept as it was. */
    write_file(path, "source_physical = 0\nsource_touchpad = 0\nsource_virtual = 1\nloopback_mode = 1\n");
    CHECK(hidway_config_load(path, &d) && !d.profiles[0].source_physical && d.profiles[0].source_virtual);

    /* Round trip of two different routes; the top-level keys win for the
     * route in use (the wizard and older clients edit only those). */
    hidway_config_load("missing-hidway-test-config", &c);
    strcpy(c.target, "192.0.2.50");
    c.port = 47820;
    c.source_touchpad = 1;
    c.repeat_button = 2;
    hidway_profile_select(&c, 1); /* Gaming PC was created at load: its own (default) address */
    CHECK(c.loopback_mode == 1 && !strcmp(c.target, "100.100.100.100") && c.source_virtual && !c.source_physical &&
          !strcmp(c.profiles[0].target, "192.0.2.50") && c.profiles[0].port == 47820);
    strcpy(c.target, "relay.example");
    c.port = 47900;
    c.relay_keyboard = 0;
    c.loopback_pointer_pico = 1;
    c.repeat_enabled = 1;
    c.repeat_cps = 22;
    c.virtual_click_cps = 3;
    CHECK(hidway_config_save(path, &c));
    CHECK(hidway_config_load(path, &d) && d.loopback_mode == 1 && !strcmp(d.target, "relay.example") &&
          d.port == 47900 && !d.relay_keyboard && d.loopback_pointer_pico && d.repeat_enabled && d.repeat_cps == 22 &&
          d.virtual_click_cps == 3 && profile_is(&d.profiles[1], &d));
    CHECK(!strcmp(d.profiles[0].target, "192.0.2.50") && d.profiles[0].port == 47820 && d.profiles[0].source_touchpad &&
          d.profiles[0].repeat_button == 2 && d.profiles[0].source_physical && !d.profiles[0].loopback_pointer_pico);
    hidway_profile_select(&d, 0); /* and back: the Remote PC values in use again */
    CHECK(!d.loopback_mode && !strcmp(d.target, "192.0.2.50") && d.port == 47820 && d.source_touchpad &&
          d.repeat_button == 2 && profile_is(&d.profiles[1], &c));
    write_file(path, "target = 192.0.2.60\nloopback_mode = 0\nremote.target = 192.0.2.61\nremote.port = 1\n"
                     "gaming.port = 1234\ngaming.repeat_cps = 999\ngaming.repeat_button = -3\n"
                     "gaming.source_physical = 1\ngaming.nonsense = 5\nremote.\n");
    CHECK(hidway_config_load(path, &d) && !strcmp(d.target, "192.0.2.60") && d.port == 47800 &&
          profile_is(&d.profiles[0], &d));
    CHECK(d.profiles[1].port == 1234 && d.profiles[1].repeat_cps == 25 && d.profiles[1].repeat_button == 0 &&
          d.profiles[1].source_physical && d.profiles[1].source_virtual && !strcmp(d.profiles[1].target, "192.0.2.60"));
    /* `compact` alone, in any spelling, is just ignored. */
    write_file(path, "compact = YES\nport = 47001\n");
    CHECK(hidway_config_load(path, &d) && d.port == 47001);
    DeleteFileA(path);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = "HIDwayClockTest";
    CHECK(RegisterClassA(&wc) != 0);
    HWND h = CreateWindowExA(0, wc.lpszClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                             wc.hInstance, NULL);
    CHECK(h != NULL);
    CHECK(hidway_send_clock_start(&clock_state, h, WM_APP + 17, 4, 17));
    Sleep(120);
    CHECK(hidway_send_clock_stop(&clock_state));
    drain();
    CHECK(ticks == 1 &&
          seen_generation == 17); /* a paused GUI gets one wakeup, not 30 queued ticks */
    ticks = 0;
    CHECK(hidway_send_clock_start(&clock_state, h, WM_APP + 17, 4, 18));
    ULONGLONG start = GetTickCount64();
    while (ticks < 100 && GetTickCount64() - start < 2000) {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
        drain();
    }
    ULONGLONG elapsed = GetTickCount64() - start;
    CHECK(hidway_send_clock_stop(&clock_state));
    CHECK(ticks >= 20 && !clock_state.failed && seen_generation == 18);
    printf("Sender clock: %u ticks in %llu ms\n", ticks, (unsigned long long)elapsed);
    DestroyWindow(h);
    printf("Config migration, atomic save and sender clock: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
