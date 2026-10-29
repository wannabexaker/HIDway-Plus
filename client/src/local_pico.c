#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include "local_pico.h"
#include "hidway_input_source.h"

unsigned hidway_local_pico_interfaces(const char *path_token)
{
    UINT count = 0;
    if (!path_token || !*path_token ||
        GetRawInputDeviceList(NULL, &count, sizeof(RAWINPUTDEVICELIST)) != 0 ||
        !count || count > 1024)
        return 0;
    RAWINPUTDEVICELIST *list = calloc(count, sizeof *list);
    if (!list) return 0;
    UINT found = GetRawInputDeviceList(list, &count, sizeof *list);
    unsigned result = 0;
    if (found != (UINT)-1) {
        for (UINT i = 0; i < found; ++i) {
            if (list[i].dwType != RIM_TYPEKEYBOARD && list[i].dwType != RIM_TYPEMOUSE)
                continue;
            char path[1024]; UINT size = sizeof path;
            UINT n = GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, path, &size);
            if (n == (UINT)-1 || !n || n >= sizeof path) continue;
            path[n] = 0;
            if (hidway_device_path_matches(path, path_token))
                result |= list[i].dwType == RIM_TYPEKEYBOARD ? HIDWAY_PICO_KEYBOARD : HIDWAY_PICO_MOUSE;
        }
    }
    free(list);
    return result;
}
