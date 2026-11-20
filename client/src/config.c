#include "config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

/* --------------------------------------------------------------- hotkeys */

static unsigned key_name_to_vk(const char *k)
{
    if (strlen(k) == 1) {
        char c = (char)toupper((unsigned char)k[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return (unsigned)c;
    }
    if ((k[0] == 'f' || k[0] == 'F') && isdigit((unsigned char)k[1])) {
        int n = atoi(k + 1);
        if (n >= 1 && n <= 24)
            return VK_F1 + (unsigned)(n - 1);
    }
    if (!strcmp(k, "end")) return VK_END;
    if (!strcmp(k, "home")) return VK_HOME;
    if (!strcmp(k, "ins") || !strcmp(k, "insert")) return VK_INSERT;
    if (!strcmp(k, "del") || !strcmp(k, "delete")) return VK_DELETE;
    if (!strcmp(k, "space")) return VK_SPACE;
    if (!strcmp(k, "esc") || !strcmp(k, "escape")) return VK_ESCAPE;
    if (!strcmp(k, "tab")) return VK_TAB;
    if (!strcmp(k, "enter") || !strcmp(k, "return")) return VK_RETURN;
    if (!strcmp(k, "pgup")) return VK_PRIOR;
    if (!strcmp(k, "pgdn")) return VK_NEXT;
    if (!strcmp(k, "up")) return VK_UP;
    if (!strcmp(k, "down")) return VK_DOWN;
    if (!strcmp(k, "left")) return VK_LEFT;
    if (!strcmp(k, "right")) return VK_RIGHT;
    if (!strcmp(k, "capslock") || !strcmp(k, "caps")) return VK_CAPITAL;
    if (!strcmp(k, "grave") || !strcmp(k, "backtick")) return VK_OEM_3;
    if (!strcmp(k, "rctrl")) return VK_RCONTROL;
    if (!strcmp(k, "ralt")) return VK_RMENU;
    if (!strcmp(k, "apps") || !strcmp(k, "menu")) return VK_APPS;
    if (!strcmp(k, "scrolllock")) return VK_SCROLL;
    if (!strcmp(k, "pause")) return VK_PAUSE;
    return 0;
}

static void vk_to_name(unsigned vk, char *out, size_t cap)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        snprintf(out, cap, "%c", (char)tolower((int)vk));
        return;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        snprintf(out, cap, "f%u", vk - VK_F1 + 1);
        return;
    }
    switch (vk) {
    case VK_END: strncpy(out, "end", cap); break;
    case VK_HOME: strncpy(out, "home", cap); break;
    case VK_INSERT: strncpy(out, "ins", cap); break;
    case VK_DELETE: strncpy(out, "del", cap); break;
    case VK_SPACE: strncpy(out, "space", cap); break;
    case VK_ESCAPE: strncpy(out, "esc", cap); break;
    case VK_TAB: strncpy(out, "tab", cap); break;
    case VK_RETURN: strncpy(out, "enter", cap); break;
    case VK_PRIOR: strncpy(out, "pgup", cap); break;
    case VK_NEXT: strncpy(out, "pgdn", cap); break;
    case VK_UP: strncpy(out, "up", cap); break;
    case VK_DOWN: strncpy(out, "down", cap); break;
    case VK_LEFT: strncpy(out, "left", cap); break;
    case VK_RIGHT: strncpy(out, "right", cap); break;
    case VK_CAPITAL: strncpy(out, "capslock", cap); break;
    case VK_OEM_3: strncpy(out, "grave", cap); break;
    case VK_RCONTROL: strncpy(out, "rctrl", cap); break;
    case VK_RMENU: strncpy(out, "ralt", cap); break;
    case VK_APPS: strncpy(out, "apps", cap); break;
    case VK_SCROLL: strncpy(out, "scrolllock", cap); break;
    case VK_PAUSE: strncpy(out, "pause", cap); break;
    default: snprintf(out, cap, "vk0x%02X", vk); break;
    }
    out[cap - 1] = 0;
}

static void hotkey_format(const hidway_hotkey_t *hk, char *out, size_t cap)
{
    char key[16];
    vk_to_name(hk->vk, key, sizeof key);
    snprintf(out, cap, "%s%s%s%s%s",
             (hk->mods & MOD_CONTROL) ? "ctrl+" : "",
             (hk->mods & MOD_ALT) ? "alt+" : "",
             (hk->mods & MOD_SHIFT) ? "shift+" : "",
             (hk->mods & MOD_WIN) ? "win+" : "",
             key);
    out[cap - 1] = 0;
}

int hidway_hotkey_parse(const char *s, hidway_hotkey_t *hk)
{
    char buf[64];
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (char *p = buf; *p; p++)
        *p = (char)tolower((unsigned char)*p);

    unsigned mods = 0, vk = 0;
    char *save = NULL;
    for (char *tok = strtok_s(buf, "+", &save); tok; tok = strtok_s(NULL, "+", &save)) {
        trim(tok);
        if (!*tok)
            continue;
        if (!strcmp(tok, "ctrl") || !strcmp(tok, "control")) mods |= MOD_CONTROL;
        else if (!strcmp(tok, "alt")) mods |= MOD_ALT;
        else if (!strcmp(tok, "shift")) mods |= MOD_SHIFT;
        else if (!strcmp(tok, "win") || !strcmp(tok, "cmd")) mods |= MOD_WIN;
        else {
            unsigned k = key_name_to_vk(tok);
            if (k)
                vk = k;
        }
    }
    if (!vk)
        return 0;
    hk->mods = mods;
    hk->vk = vk;
    hotkey_format(hk, hk->text, sizeof hk->text);
    return 1;
}

/* ---------------------------------------------------------------- config */

static void set_defaults(hidway_config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    strcpy(cfg->target, "100.100.100.100");
    cfg->port = 47800;
    cfg->send_rate_hz = 250;
    cfg->auto_disarm_ms = 3000;
    cfg->relay_keyboard = 1;
    cfg->relay_mouse = 1;
    cfg->source_physical = 1;
    cfg->source_touchpad = 0;
    cfg->source_virtual = 0;
    cfg->loopback_mode = 0;
    cfg->virtual_click_cps = 10;
    cfg->repeat_cps = 10;
    cfg->repeat_sources = 1;
    cfg->history_enabled = 1;
    hidway_hotkey_parse("ctrl+shift+f12", &cfg->toggle);
    hidway_hotkey_parse("ctrl+alt+shift+end", &cfg->panic);
    cfg->window_x = cfg->window_y = INT_MIN;
    cfg->badge = 1;
    cfg->badge_x = cfg->badge_y = INT_MIN;
    strcpy(cfg->loopback_device, "VID_CAFE&PID_4877");
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ------------------------------------------------------- route profiles */

/* Same bit as HIDWAY_SOURCE_VIRTUAL (common/hidway_input_source.h). */
#define PROFILE_SOURCE_VIRTUAL 4

enum {
    PF_TARGET, PF_PORT, PF_KEYBOARD, PF_MOUSE, PF_PHYSICAL, PF_TOUCHPAD, PF_VIRTUAL,
    PF_POINTER, PF_REPEAT, PF_REPEAT_BUTTON, PF_REPEAT_CPS, PF_REPEAT_SOURCES, PF_CLICK_CPS,
    PF_COUNT
};
static const char *const PROFILE_KEYS[PF_COUNT] = {
    "target", "port", "relay_keyboard", "relay_mouse", "source_physical", "source_touchpad",
    "source_virtual", "loopback_pointer_pico", "repeat_enabled", "repeat_button", "repeat_cps",
    "repeat_sources", "virtual_click_cps"};
static const char *const PROFILE_PREFIX[2] = {"remote.", "gaming."};

static int *profile_int(hidway_route_profile_t *p, int field)
{
    switch (field) {
    case PF_PORT: return &p->port;
    case PF_KEYBOARD: return &p->relay_keyboard;
    case PF_MOUSE: return &p->relay_mouse;
    case PF_PHYSICAL: return &p->source_physical;
    case PF_TOUCHPAD: return &p->source_touchpad;
    case PF_VIRTUAL: return &p->source_virtual;
    case PF_POINTER: return &p->loopback_pointer_pico;
    case PF_REPEAT: return &p->repeat_enabled;
    case PF_REPEAT_BUTTON: return &p->repeat_button;
    case PF_REPEAT_CPS: return &p->repeat_cps;
    case PF_REPEAT_SOURCES: return &p->repeat_sources;
    case PF_CLICK_CPS: return &p->virtual_click_cps;
    default: return NULL;
    }
}

/* Parse one route value with the same rules as the top-level key.
 * Returns 1 when the value was taken. */
static int profile_parse(hidway_route_profile_t *p, int field, const char *val)
{
    if (field == PF_TARGET) {
        if (!*val)
            return 0;
        strncpy(p->target, val, sizeof p->target - 1);
        p->target[sizeof p->target - 1] = 0;
        return 1;
    }
    int *v = profile_int(p, field), n = atoi(val);
    switch (field) {
    case PF_PORT: *v = clampi(n, 1, 65535); break;
    case PF_REPEAT_BUTTON: *v = clampi(n, 0, 4); break;
    case PF_REPEAT_CPS: case PF_CLICK_CPS: *v = clampi(n, 1, 25); break;
    case PF_REPEAT_SOURCES: *v = clampi(n, 1, 7); break;
    default: *v = n ? 1 : 0; break;
    }
    return 1;
}

static int profile_field(const char *key)
{
    for (int i = 0; i < PF_COUNT; i++)
        if (!strcmp(key, PROFILE_KEYS[i]))
            return i;
    return -1;
}

static void profile_from_flat(hidway_route_profile_t *p, const hidway_config_t *c)
{
    memset(p, 0, sizeof *p);
    p->valid = 1;
    memcpy(p->target, c->target, sizeof p->target);
    p->port = c->port;
    p->relay_keyboard = c->relay_keyboard;
    p->relay_mouse = c->relay_mouse;
    p->source_physical = c->source_physical;
    p->source_touchpad = c->source_touchpad;
    p->source_virtual = c->source_virtual;
    p->loopback_pointer_pico = c->loopback_pointer_pico;
    p->repeat_enabled = c->repeat_enabled;
    p->repeat_button = c->repeat_button;
    p->repeat_cps = c->repeat_cps;
    p->repeat_sources = c->repeat_sources;
    p->virtual_click_cps = c->virtual_click_cps;
}

static void profile_to_flat(hidway_config_t *c, const hidway_route_profile_t *p)
{
    memcpy(c->target, p->target, sizeof c->target);
    c->target[sizeof c->target - 1] = 0;
    c->port = p->port;
    c->relay_keyboard = p->relay_keyboard;
    c->relay_mouse = p->relay_mouse;
    c->source_physical = p->source_physical;
    c->source_touchpad = p->source_touchpad;
    c->source_virtual = p->source_virtual;
    c->loopback_pointer_pico = p->loopback_pointer_pico;
    c->repeat_enabled = p->repeat_enabled;
    c->repeat_button = p->repeat_button;
    c->repeat_cps = p->repeat_cps;
    c->repeat_sources = p->repeat_sources;
    c->virtual_click_cps = p->virtual_click_cps;
}

/* Create route `mode` from the values in use: same relay address and port,
 * same speeds. Gaming PC starts as the "Pico on this PC" preset (virtual
 * input, keyboard and mouse, native pointer, repeat off); Remote PC keeps
 * the source choices that the Gaming PC route preserved for it, and falls
 * back to Physical when none of them is on. */
static void profile_derive(hidway_config_t *c, int mode)
{
    hidway_route_profile_t *p = &c->profiles[mode];
    profile_from_flat(p, c);
    if (mode == HIDWAY_PROFILE_GAMING) {
        p->relay_keyboard = p->relay_mouse = 1;
        p->source_virtual = 1;
        p->source_physical = p->source_touchpad = 0;
        p->loopback_pointer_pico = 0;
        p->repeat_enabled = 0;
        p->repeat_sources = PROFILE_SOURCE_VIRTUAL;
    } else if (!p->source_physical && !p->source_touchpad && !p->source_virtual) {
        p->source_physical = 1;
    }
}

void hidway_profile_store(hidway_config_t *cfg)
{
    int mode = cfg->loopback_mode ? 1 : 0;
    profile_from_flat(&cfg->profiles[mode], cfg);
}

void hidway_profile_select(hidway_config_t *cfg, int mode)
{
    mode = mode ? 1 : 0;
    hidway_profile_store(cfg);
    if (!cfg->profiles[mode].valid)
        profile_derive(cfg, mode);
    profile_to_flat(cfg, &cfg->profiles[mode]);
    cfg->loopback_mode = mode;
}

/* ---------------------------------------------------------------- load */

int hidway_config_load(const char *path, hidway_config_t *cfg)
{
    set_defaults(cfg);
    hidway_profile_store(cfg);
    profile_derive(cfg, HIDWAY_PROFILE_GAMING);

    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    /* Prefixed route values are collected first and laid over the derived
     * route once the top-level values (and so the route in use) are known. */
    hidway_route_profile_t prefixed[2];
    unsigned prefixed_seen[2] = {0, 0};
    memset(prefixed, 0, sizeof prefixed);
    int route_seen = 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *hash = strpbrk(line, "#;");
        if (hash)
            *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        trim(key);
        trim(val);
        if (!*key)
            continue;

        int prefixed_profile = -1;
        for (int p = 0; p < 2; p++)
            if (!strncmp(key, PROFILE_PREFIX[p], strlen(PROFILE_PREFIX[p])))
                prefixed_profile = p;
        if (prefixed_profile >= 0) {
            int field = profile_field(key + strlen(PROFILE_PREFIX[prefixed_profile]));
            if (field >= 0 && profile_parse(&prefixed[prefixed_profile], field, val))
                prefixed_seen[prefixed_profile] |= 1u << field;
            continue;
        }

        if (!strcmp(key, "target") && *val) {
            strncpy(cfg->target, val, sizeof cfg->target - 1);
            cfg->target[sizeof cfg->target - 1] = 0;
        } else if (!strcmp(key, "port")) {
            cfg->port = clampi(atoi(val), 1, 65535);
        } else if (!strcmp(key, "send_rate_hz")) {
            cfg->send_rate_hz = clampi(atoi(val), 10, 1000);
        } else if (!strcmp(key, "auto_disarm_ms")) {
            cfg->auto_disarm_ms = clampi(atoi(val), 0, 60000);
        } else if (!strcmp(key, "relay_keyboard")) {
            cfg->relay_keyboard = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "relay_mouse")) {
            cfg->relay_mouse = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "source_physical")) {
            cfg->source_physical = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "source_touchpad")) {
            cfg->source_touchpad = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "source_virtual")) {
            cfg->source_virtual = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "loopback_mode")) {
            cfg->loopback_mode = atoi(val) ? 1 : 0; route_seen = 1;
        } else if (!strcmp(key, "loopback_pointer_pico")) {
            cfg->loopback_pointer_pico = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "loopback_physical")) {
            cfg->loopback_physical = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "virtual_click_cps")) {
            cfg->virtual_click_cps = clampi(atoi(val), 1, 25);
        } else if (!strcmp(key, "repeat_enabled")) {
            cfg->repeat_enabled = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "repeat_button")) {
            cfg->repeat_button = clampi(atoi(val), 0, 4);
        } else if (!strcmp(key, "repeat_cps")) {
            cfg->repeat_cps = clampi(atoi(val), 1, 25);
        } else if (!strcmp(key, "repeat_sources")) {
            cfg->repeat_sources = clampi(atoi(val), 1, 7);
        } else if (!strcmp(key, "history_enabled")) {
            cfg->history_enabled = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "toggle_hotkey")) {
            hidway_hotkey_parse(val, &cfg->toggle);
        } else if (!strcmp(key, "panic_hotkey")) {
            hidway_hotkey_parse(val, &cfg->panic);
        } else if (!strcmp(key, "mouse4_key")) {
            hidway_hotkey_parse(val, &cfg->mouse4);
        } else if (!strcmp(key, "mouse5_key")) {
            hidway_hotkey_parse(val, &cfg->mouse5);
        } else if (!strcmp(key, "window_x")) {
            cfg->window_x = (int)strtol(val, NULL, 10);
        } else if (!strcmp(key, "window_y")) {
            cfg->window_y = (int)strtol(val, NULL, 10);
        } else if (!strcmp(key, "badge")) {
            cfg->badge = atoi(val) ? 1 : 0;
        } else if (!strcmp(key, "badge_x")) {
            cfg->badge_x = (int)strtol(val, NULL, 10);
        } else if (!strcmp(key, "badge_y")) {
            cfg->badge_y = (int)strtol(val, NULL, 10);
        } else if (!strcmp(key, "compact")) {
            /* written by 0.4.x for its compact layout; the tabbed window has none */
        } else if (!strcmp(key, "loopback_device")) {
            strncpy(cfg->loopback_device, val, sizeof cfg->loopback_device - 1);
            cfg->loopback_device[sizeof cfg->loopback_device - 1] = 0;
        } else if (!strcmp(key, "key")) {
            strncpy(cfg->key_hex, val, sizeof cfg->key_hex - 1);
            cfg->key_hex[sizeof cfg->key_hex - 1] = 0;
        }
    }
    fclose(f);
    if (!route_seen) /* preserve old virtual-only loopback once during migration */
        cfg->loopback_mode = cfg->source_virtual && !cfg->source_physical && !cfg->source_touchpad;

    /* The top-level keys always win for the route in use: the setup wizard,
     * hidway-probe and older clients read and write only those. */
    int active = cfg->loopback_mode ? 1 : 0, other = !active;
    hidway_profile_store(cfg);
    profile_derive(cfg, other);
    for (int field = 0; field < PF_COUNT; field++) {
        if (!(prefixed_seen[other] & (1u << field)))
            continue;
        if (field == PF_TARGET)
            memcpy(cfg->profiles[other].target, prefixed[other].target, sizeof prefixed[other].target);
        else
            *profile_int(&cfg->profiles[other], field) = *profile_int(&prefixed[other], field);
    }
    return 1;
}

int hidway_config_save(const char *path, const hidway_config_t *in)
{
    /* The route in use is written from the flat values, never from a stale
     * copy; the other route is written as remembered (or derived). */
    hidway_config_t copy = *in;
    const hidway_config_t *cfg = &copy;
    SecureZeroMemory(copy.key_hex, sizeof copy.key_hex); /* written from `in`; no stack copy */
    hidway_profile_store(&copy);
    if (!copy.profiles[!copy.loopback_mode].valid)
        profile_derive(&copy, !copy.loopback_mode);
    char temporary[MAX_PATH];
    if(snprintf(temporary,sizeof temporary,"%s.tmp",path)<0||strlen(path)+4>=sizeof temporary)return 0;
    FILE *f = fopen(temporary, "w");
    if (!f)
        return 0;
    fprintf(f, "# HIDway client configuration.\n\n");
    fprintf(f, "target = %s\n", cfg->target);
    fprintf(f, "port = %d\n\n", cfg->port);
    fprintf(f, "send_rate_hz = %d\n", cfg->send_rate_hz);
    fprintf(f, "auto_disarm_ms = %d   # 0 = disabled\n\n", cfg->auto_disarm_ms);
    fprintf(f, "relay_keyboard = %d\n", cfg->relay_keyboard);
    fprintf(f, "relay_mouse = %d\n", cfg->relay_mouse);
    fprintf(f, "source_physical = %d\n", cfg->source_physical);
    fprintf(f, "source_touchpad = %d\n", cfg->source_touchpad);
    fprintf(f, "source_virtual = %d\n", cfg->source_virtual);
    fprintf(f, "loopback_mode = %d\n", cfg->loopback_mode);
    fprintf(f, "loopback_pointer_pico = %d\n", cfg->loopback_pointer_pico);
    fprintf(f, "loopback_physical = %d   # Gaming PC route: this PC's keys through the Pico (experimental)\n",
            cfg->loopback_physical);
    fprintf(f, "virtual_click_cps = %d\n", cfg->virtual_click_cps);
    fprintf(f, "repeat_enabled = %d\nrepeat_button = %d\nrepeat_cps = %d\nrepeat_sources = %d\n",
            cfg->repeat_enabled, cfg->repeat_button, cfg->repeat_cps, cfg->repeat_sources);
    fprintf(f, "history_enabled = %d\n\n", cfg->history_enabled);
    fprintf(f, "toggle_hotkey = %s\n", cfg->toggle.text);
    fprintf(f, "panic_hotkey = %s\n\n", cfg->panic.text);
    if (cfg->mouse4.vk || cfg->mouse5.vk) {
        fprintf(f, "# While armed, these keys act as mouse side buttons on the target.\n");
        if (cfg->mouse4.vk)
            fprintf(f, "mouse4_key = %s\n", cfg->mouse4.text);
        if (cfg->mouse5.vk)
            fprintf(f, "mouse5_key = %s\n", cfg->mouse5.text);
        fprintf(f, "\n");
    }
    fprintf(f, "badge = %d   # 1 = always on top (click to arm), 0 = only while armed\n", cfg->badge);
    if (cfg->badge_x != INT_MIN && cfg->badge_y != INT_MIN) {
        fprintf(f, "badge_x = %d\n", cfg->badge_x);
        fprintf(f, "badge_y = %d\n", cfg->badge_y);
    }
    if (cfg->window_x != INT_MIN && cfg->window_y != INT_MIN) {
        fprintf(f, "window_x = %d\n", cfg->window_x);
        fprintf(f, "window_y = %d\n", cfg->window_y);
    }
    fprintf(f, "\n# Ignore this Raw Input device path token to prevent Pico mouse loopback.\n");
    fprintf(f, "loopback_device = %s\n", cfg->loopback_device);
    if (in->key_hex[0]) /* keep the end-to-end key across saves */
        fprintf(f, "\n# End-to-end key, same as the relay's key file. Keep secret.\nkey = %s\n",
                in->key_hex);
    fprintf(f, "\n# Route profiles. The keys above always describe the route in use\n"
               "# (loopback_mode); each route also keeps its own copy here.\n");
    for (int p = 0; p < 2; p++) {
        hidway_route_profile_t *r = &copy.profiles[p];
        fprintf(f, "\n%starget = %s\n", PROFILE_PREFIX[p], r->target);
        for (int field = PF_PORT; field < PF_COUNT; field++)
            fprintf(f, "%s%s = %d\n", PROFILE_PREFIX[p], PROFILE_KEYS[field], *profile_int(r, field));
    }
    int ok=!ferror(f);
    if(fclose(f)!=0)ok=0;
    if(ok&&MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return 1;
    DeleteFileA(temporary);return 0;
}
