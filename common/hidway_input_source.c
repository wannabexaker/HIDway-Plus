#include "hidway_input_source.h"

#include <ctype.h>
#include <stddef.h>

unsigned hidway_keyboard_source(bool injected)
{
    return injected ? HIDWAY_SOURCE_VIRTUAL : HIDWAY_SOURCE_PHYSICAL;
}

unsigned hidway_mouse_source(bool has_device, bool absolute)
{
    if (!has_device)
        return HIDWAY_SOURCE_VIRTUAL;
    return absolute ? HIDWAY_SOURCE_TOUCHPAD : HIDWAY_SOURCE_PHYSICAL;
}

int32_t hidway_absolute_axis_delta(int32_t sample, int extent, int32_t *previous, bool *valid)
{
    if (extent <= 0)
        return 0;
    if (sample < 0)
        sample = 0;
    if (sample > 65535)
        sample = 65535;
    int32_t pixel = (int32_t)(((int64_t)sample * (extent - 1) + 32767) / 65535);
    if (!*valid) {
        *previous = pixel;
        *valid = true;
        return 0;
    }
    int32_t delta = pixel - *previous;
    *previous = pixel;
    return delta;
}

bool hidway_device_path_matches(const char *path, const char *token)
{
    if (!path || !token || !*token)
        return false;
    for (; *path; path++) {
        const char *a = path, *b = token;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b)
            return true;
    }
    return false;
}
