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
    hidway_config_t c, d;
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
