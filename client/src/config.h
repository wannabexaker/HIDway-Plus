#ifndef HIDWAY_CLIENT_CONFIG_H
#define HIDWAY_CLIENT_CONFIG_H

#include <stddef.h>

/* A hotkey as Win32 RegisterHotKey() inputs, plus its text form for display. */
typedef struct {
    unsigned mods; /* MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN */
    unsigned vk;   /* virtual-key code */
    char text[40];
} hidway_hotkey_t;

/* What each route (Remote PC / Gaming PC) remembers on its own. */
typedef struct {
    int valid;              /* 0: not created yet; derived from the active values on first use */
    char target[128];
    int port;
    int relay_keyboard, relay_mouse;
    int source_physical, source_touchpad, source_virtual;
    int loopback_pointer_pico;
    int repeat_enabled, repeat_button, repeat_cps, repeat_sources;
    int virtual_click_cps;
} hidway_route_profile_t;

enum { HIDWAY_PROFILE_REMOTE = 0, HIDWAY_PROFILE_GAMING = 1 };

typedef struct {
    char target[128];       /* Tailscale IP / hostname of the relay */
    int port;               /* UDP port of hidwayd */
    int send_rate_hz;       /* state send rate while armed */
    int auto_disarm_ms;     /* auto-disarm if the link is silent this long (0 = off) */
    int relay_keyboard;     /* relay keyboard input */
    int relay_mouse;        /* relay mouse input */
    int source_physical;    /* hardware keyboard / relative mouse */
    int source_touchpad;    /* absolute-position mouse/touchpad reports */
    int source_virtual;     /* injected/device-less input (AnyDesk etc.) */
    int loopback_mode;      /* 0: another PC, 1: injected-only return to this PC */
    int loopback_pointer_pico; /* opt-in relative HID motion; default native cursor placement */
    int loopback_physical;  /* experimental, Gaming PC route only: this PC's own keys, buttons
                               and wheel also go through the Pico. Its own key, outside the
                               route profiles; never derived from source_physical. */
    int virtual_click_cps;  /* 1..25 accepted clicks/sec per virtual button */
    int repeat_enabled;     /* hold selected button to generate paced clicks */
    int repeat_button;      /* 0..4: left, right, middle, back, forward */
    int repeat_cps;         /* 1..25 requested clicks/sec */
    int repeat_sources;     /* bitmask: physical=1, touchpad=2, virtual=4 */
    int history_enabled;    /* record recent-input history in the UI */
    hidway_hotkey_t toggle; /* arm/disarm hotkey */
    hidway_hotkey_t panic;  /* release everything + disarm */
    hidway_hotkey_t mouse4; /* key that acts as mouse button 4 while armed (vk 0 = none) */
    hidway_hotkey_t mouse5; /* key that acts as mouse button 5 while armed (vk 0 = none) */
    int window_x, window_y; /* last window position; INT_MIN = unset */
    int badge;              /* show the badge while disarmed (click to arm) */
    int badge_x, badge_y;   /* badge top centre on screen; INT_MIN = unset */
    int update_check;       /* 1 = one read-only check for a newer release at start (default) */
    char loopback_device[64]; /* Raw Input path token excluded from capture */
    char key_hex[80];       /* end-to-end key (64 hex chars); empty = encryption off */
    /* Both routes, indexed by loopback_mode. The flat fields above always
     * hold the route in use; profiles[loopback_mode] is refreshed from them
     * by hidway_profile_store() and by every save. */
    hidway_route_profile_t profiles[2];
} hidway_config_t;

/* Load from `path`. Missing file/keys keep defaults; always fully populated.
 * The top-level keys describe the route in use; prefixed keys (remote.*,
 * gaming.*) hold each route. A file without prefixed keys keeps every value
 * and gets the other route derived from it. `compact` (older clients) is
 * accepted and ignored. Returns 1 if a file was read, else 0. */
int hidway_config_load(const char *path, hidway_config_t *cfg);

/* Write the config back to `path` atomically. Returns 1 on success. */
int hidway_config_save(const char *path, const hidway_config_t *cfg);

/* Copy the flat (in use) route values into profiles[loopback_mode]. */
void hidway_profile_store(hidway_config_t *cfg);

/* Store the route in use, then make `mode` the route in use with its own
 * remembered values (created from sensible defaults if it never existed). */
void hidway_profile_select(hidway_config_t *cfg, int mode);

/* Parse "ctrl+shift+f12" into a hotkey. Returns 1 on success. */
int hidway_hotkey_parse(const char *s, hidway_hotkey_t *hk);

#endif /* HIDWAY_CLIENT_CONFIG_H */
