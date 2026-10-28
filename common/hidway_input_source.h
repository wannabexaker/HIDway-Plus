#ifndef HIDWAY_INPUT_SOURCE_H
#define HIDWAY_INPUT_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    HIDWAY_SOURCE_PHYSICAL = 1u << 0,
    HIDWAY_SOURCE_TOUCHPAD = 1u << 1,
    HIDWAY_SOURCE_VIRTUAL  = 1u << 2,
};

/* Windows source classification kept platform-neutral for unit tests. */
unsigned hidway_keyboard_source(bool injected);
unsigned hidway_mouse_source(bool has_device, bool absolute);

/* Convert a normalized Windows absolute-mouse axis (0..65535) to a delta.
 * The first sample establishes the origin and deliberately returns zero. */
int32_t hidway_absolute_axis_delta(int32_t sample, int extent, int32_t *previous, bool *valid);

/* Raw Input device paths contain VID_xxxx&PID_xxxx. Matching is ASCII and
 * case-insensitive so it also works with paths supplied by test fixtures. */
bool hidway_device_path_matches(const char *path, const char *token);

#endif /* HIDWAY_INPUT_SOURCE_H */
