#ifndef HIDWAY_LOCAL_PICO_H
#define HIDWAY_LOCAL_PICO_H

enum { HIDWAY_PICO_KEYBOARD = 1, HIDWAY_PICO_MOUSE = 2,
       HIDWAY_PICO_READY = HIDWAY_PICO_KEYBOARD | HIDWAY_PICO_MOUSE };

/* Read-only Raw Input inventory. Matching a configured USB path token is a
 * compatibility check, not authenticated identity or proof of UART wiring. */
unsigned hidway_local_pico_interfaces(const char *path_token);

#endif
