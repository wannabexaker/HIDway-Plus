/*
 * HIDway client - remote PC interface.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <timeapi.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
#include <wtsapi32.h>

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "hidway_crypto.h"
#include "hidway_input_source.h"
#include "hidway_input_state.h"
#include "hidway_status_order.h"
#include "hidway_kbd.h"
#include "hidway_version.h"
#include "keymap_sc2hid.h"
#include "net.h"
#include "protocol.h"
#include "capture_queue.h"
#include "send_clock.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "wtsapi32.lib")

/* ---- DWM attributes (Windows 11) ---------------------------------------- */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define HW_DWMWA_CORNER   33
#define HW_DWMWA_BORDER   34
#define HW_DWMWA_CAPTION  35
#define HW_DWMWA_TEXT     36
#define HW_DWMWCP_ROUND   2

/* ---- minimal GDI+ flat API (C has no gdiplus headers) ------------------- */
typedef DWORD HW_ARGB;
typedef struct { float X, Y; } HW_PointF;
typedef struct {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} HW_GdiplusStartupInput;
int WINAPI GdiplusStartup(ULONG_PTR *token, const HW_GdiplusStartupInput *in, void *out);
void WINAPI GdiplusShutdown(ULONG_PTR token);
int WINAPI GdipCreateFromHDC(HDC hdc, void **graphics);
int WINAPI GdipDeleteGraphics(void *graphics);
int WINAPI GdipSetSmoothingMode(void *graphics, int mode);
int WINAPI GdipSetPixelOffsetMode(void *graphics, int mode);
int WINAPI GdipCreatePath(int fill_mode, void **path);
int WINAPI GdipDeletePath(void *path);
int WINAPI GdipAddPathArc(void *path, float x, float y, float w, float h, float start, float sweep);
int WINAPI GdipClosePathFigure(void *path);
int WINAPI GdipCreateSolidFill(HW_ARGB color, void **brush);
int WINAPI GdipDeleteBrush(void *brush);
int WINAPI GdipFillPath(void *graphics, void *brush, void *path);
int WINAPI GdipFillEllipse(void *graphics, void *brush, float x, float y, float w, float h);
int WINAPI GdipCreatePen1(HW_ARGB color, float width, int unit, void **pen);
int WINAPI GdipDeletePen(void *pen);
int WINAPI GdipSetPenStartCap(void *pen, int cap);
int WINAPI GdipSetPenEndCap(void *pen, int cap);
int WINAPI GdipSetPenLineJoin(void *pen, int join);
int WINAPI GdipDrawLines(void *graphics, void *pen, const HW_PointF *points, int count);

/* ---- constants ----------------------------------------------------------- */
#define HK_TOGGLE 1
#define HK_PANIC  2
#define TIMER_UI    1
#define TIMER_PROBE 3
#define TIMER_SAVED 4
#define TIMER_SHOT  5
#define UI_REFRESH_MS 50
#define PROBE_MS      400
#define LINK_RECENT_US 900000u
#define HISTORY_MAX 80
#define WM_TRAYICON (WM_APP + 1)
#define WM_CAPTURE_KEY (WM_APP + 2) /* from the hook thread: wp vk, lp scan | KEYF_* */
#define WM_NET_READ (WM_APP + 3)    /* a relay reply is waiting */
#define WM_CAPTURE_MOUSE (WM_APP + 4) /* injected mouse: wp WM_*, lp packed data */
#define WM_CAPTURE_READY (WM_APP + 5)
#define WM_SEND_TICK (WM_APP + 6)
#define KEYF_E0 0x10000
#define KEYF_UP 0x20000
#define KEYF_VIRTUAL 0x40000
#define BADGE_FADE_MS 2500u
#define WAKE_WAIT_MS 180000u /* show "Waking..." this long after a wake request */
#define WIN_STYLE (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN)

enum {
    ID_HISTORY = 1001, ID_CLEAR, ID_TARGET, ID_PORT, ID_RATE, ID_AUTO,
    ID_CB_KBD, ID_CB_MOUSE, ID_CB_HIST, ID_SAVE, ID_ARM, ID_PANIC,
    ID_SRC_PHYSICAL, ID_SRC_TOUCHPAD, ID_SRC_VIRTUAL,
    ID_OPTIONS, ID_ROUTE, ID_REPEAT, ID_REPEAT_BUTTON, ID_REPEAT_CPS, ID_REPEAT_SOURCE, ID_CLICK_CPS, ID_POINTER,
    IDM_ARM, IDM_PANIC, IDM_WAKE, IDM_SHOW, IDM_BADGE, IDM_QUIT
};

enum { BTN_L = 1, BTN_R = 2, BTN_M = 4, BTN_X1 = 8, BTN_X2 = 16 };

/* ---- palette: neutral dark "mouse grey" ----------------------------------- */
#define C_BG        RGB(30, 31, 34)
#define C_CARD      RGB(37, 38, 42)
#define C_CARD_BD   RGB(50, 52, 57)
#define C_TILE      RGB(44, 46, 51)
#define C_FIELD     RGB(43, 45, 50)
#define C_FIELD_BD  RGB(62, 65, 71)
#define C_FOCUS_BD  RGB(140, 145, 154)
#define C_CHIP      RGB(54, 56, 62)
#define C_CHIP_BD   RGB(72, 75, 82)
#define C_TEXT      RGB(236, 237, 239)
#define C_TEXT2     RGB(168, 171, 178)
#define C_TEXT3     RGB(118, 121, 129)
#define C_WHITE     RGB(255, 255, 255)
#define C_BTN       RGB(95, 99, 107)   /* mouse grey */
#define C_BTN_H     RGB(111, 115, 123)
#define C_BTN_P     RGB(83, 86, 93)
#define C_SEC       RGB(48, 50, 55)
#define C_SEC_H     RGB(60, 62, 68)
#define C_SEC_BD    RGB(68, 71, 77)
#define C_DNG       RGB(64, 31, 34)
#define C_DNG_H     RGB(82, 37, 41)
#define C_DNG_P     RGB(54, 27, 30)
#define C_DNG_BD    RGB(140, 58, 64)
#define C_DNG_BD_H  RGB(182, 76, 84)
#define C_DNG_TX    RGB(255, 154, 146)
#define C_LIVE      RGB(63, 185, 80)
#define C_LIVE_F    RGB(30, 46, 35)
#define C_LIVE_BD   RGB(48, 102, 62)
#define C_LIVE_TX   RGB(130, 230, 140)
#define C_WARN      RGB(248, 96, 88)
#define C_AMBER     RGB(214, 160, 56)

typedef struct {
    int W, H;
    RECT mark, pill;
    int title_x, title_y, sub_y;
    RECT stats, tiles[4];
    RECT live;
    RECT recent, clear, list;
    RECT settings, save, frame[4];
    int label_y;
    RECT cb[3];
    RECT src[3];
    int hk_cy;
    RECT arm, panic;
    int band_bottom;
} layout_t;

static struct {
    hidway_config_t cfg;
    int preview; /* 0 normal, 1 preview disarmed, 2 preview armed */
    char shot_path[MAX_PATH];
    char hot_list[96];

    int armed;
    int net_ok;

    uint8_t modifiers;
    uint8_t keys[HIDWAY_KEY_BITMAP_BYTES];
    uint8_t buttons;
    hidway_event_queue_t events;
    hidway_event_frame_t live_frame;
    hidway_event_frame_t logged_frame;
    hidway_input_state_t inputs;
    hidway_held_t held;
    hidway_pulse_t pulse[HIDWAY_INPUT_ORIGINS][5];
    uint32_t generation;
    int unicode_warned;
    uint32_t last_limited;
    hidway_send_clock_t send_clock;

    uint8_t key_buttons; /* mouse buttons held through mouse4_key / mouse5_key */

    int32_t cum_x, cum_y;
    int16_t cum_wheel, cum_pan;
    int wheel_rem_v, wheel_rem_h;
    int32_t disp_x, disp_y;
    int16_t disp_wheel, disp_pan;
    int32_t show_dx, show_dy;
    int show_dw, show_dp;

    uint32_t session_id;
    uint32_t seq;
    unsigned sent_in_sec, pps;
    uint32_t sec_start_us;
    int rtt_ms;
    uint32_t pi_frames_ok;
    uint16_t pi_gaps;
    uint16_t pi_flags;
    int status_seen;
    hidway_status_order_t status_order;
    uint32_t wake_sent_ms; /* last wake request, 0 = none pending */
    int shown_target;      /* last target state written to the history */
    int wake_hot;          /* pointer over the TARGET PC tile while it offers wake */
    int ever_status;
    uint32_t last_status_us;
    uint32_t last_status_received_ms;
    uint32_t armed_at_ms;
    int prev_link_up;
    int toggle_hk_ok;
    int probes_unanswered;
    int focus_id;
    int saved_flash;

    int crypto_on;                /* a valid key is configured: seal everything */
    int key_error;                /* a key is configured but invalid: send nothing */
    uint8_t key[HIDWAY_KEY_LEN];

    UINT dpi;
    layout_t L;
    HINSTANCE inst;
    HWND hwnd;
    HWND history, clear, edit[4], cb[3], src[3], save, arm, panic;
    HWND options,options_button,route,pointer_route,repeat,repeat_button,repeat_cps,repeat_source,click_cps;
    int options_repeat;
    HFONT f_title, f_sub, f_section, f_label, f_body, f_pill, f_tile, f_mono, f_mono_s,
          f_btn, f_btn_big, f_chip;
    HICON mark_icon;
    HBRUSH b_bg, b_field;
    ULONG_PTR gdip;

    NOTIFYICONDATAA nid;
    int tray_shown;
    HICON ic_grey, ic_green, ic_red;
    int cur_icon;

    /* input capture while armed */
    HWND badge;
    UINT badge_dpi;
    HFONT bf_main, bf_hint;
    BYTE badge_alpha;
    COLORREF badge_border;
    int badge_layout_armed;
    char badge_text[64];
    int badge_hover, badge_pressed, badge_drag;
    POINT drag_pt;
    RECT drag_rc;
    HWND prev_fg;
    POINT cursor_home;
    int exclusive_capture; /* badge owns focus/cursor; false for virtual-only loopback */
    HANDLE hook_thread,hook_ready;
    DWORD hook_tid;
    volatile LONG hook_ok; /* bit 0 keyboard, bit 1 mouse */
    int end_as_del;

    struct {
        HANDLE device;
        int32_t x, y;
        bool x_valid, y_valid;
        int touchpad;
    } absolute[8];
} g;

static int S(int v)
{
    return MulDiv(v, (int)g.dpi, 96);
}

/* ======================================================================== */
/*  Text and shape primitives                                               */
/* ======================================================================== */

static void to_wide(const char *s, WCHAR *w, int cap)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, cap))
        w[0] = 0;
}

static void text_at(HDC dc, HFONT f, COLORREF c, int x, int y, const char *s)
{
    WCHAR w[256];
    to_wide(s, w, 256);
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, w, lstrlenW(w));
}

static int text_w(HDC dc, HFONT f, const char *s)
{
    WCHAR w[256];
    SIZE sz = {0};
    to_wide(s, w, 256);
    SelectObject(dc, f);
    GetTextExtentPoint32W(dc, w, lstrlenW(w), &sz);
    return sz.cx;
}

static int font_h(HDC dc, HFONT f)
{
    TEXTMETRICW tm;
    SelectObject(dc, f);
    GetTextMetricsW(dc, &tm);
    return tm.tmHeight;
}

static int font_ascent(HDC dc, HFONT f)
{
    TEXTMETRICW tm;
    SelectObject(dc, f);
    GetTextMetricsW(dc, &tm);
    return tm.tmAscent;
}

/* Draw text vertically centred on the line y = cy. */
static void text_vc(HDC dc, HFONT f, COLORREF c, int x, int cy, const char *s)
{
    text_at(dc, f, c, x, cy - font_h(dc, f) / 2, s);
}

static void text_in(HDC dc, HFONT f, COLORREF c, RECT r, UINT fmt, const char *s)
{
    WCHAR w[256];
    to_wide(s, w, 256);
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, w, -1, &r, fmt | DT_NOPREFIX);
}

/* Uppercase label with letter-spacing (section titles, tile labels). */
static void tracked(HDC dc, HFONT f, COLORREF c, int x, int y, const char *s)
{
    SetTextCharacterExtra(dc, S(1));
    text_at(dc, f, c, x, y, s);
    SetTextCharacterExtra(dc, 0);
}

static int tracked_w(HDC dc, HFONT f, const char *s)
{
    SetTextCharacterExtra(dc, S(1));
    int w = text_w(dc, f, s);
    SetTextCharacterExtra(dc, 0);
    return w;
}

static HW_ARGB argb(COLORREF c)
{
    return 0xFF000000u | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
}

static void *gp_begin(HDC dc)
{
    void *gr = NULL;
    GdipCreateFromHDC(dc, &gr);
    GdipSetSmoothingMode(gr, 4);   /* AntiAlias */
    GdipSetPixelOffsetMode(gr, 4); /* Half */
    return gr;
}

static void *path_round(float x, float y, float w, float h, float r)
{
    void *p = NULL;
    float d = r * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    GdipCreatePath(0, &p);
    if (d < 1.0f) {
        GdipAddPathArc(p, x, y, 0.01f, 0.01f, 180, 90);
        GdipAddPathArc(p, x + w, y, 0.01f, 0.01f, 270, 90);
        GdipAddPathArc(p, x + w, y + h, 0.01f, 0.01f, 0, 90);
        GdipAddPathArc(p, x, y + h, 0.01f, 0.01f, 90, 90);
    } else {
        GdipAddPathArc(p, x, y, d, d, 180, 90);
        GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
        GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
        GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    }
    GdipClosePathFigure(p);
    return p;
}

static void fill_path(void *gr, void *path, COLORREF c)
{
    void *b = NULL;
    GdipCreateSolidFill(argb(c), &b);
    GdipFillPath(gr, b, path);
    GdipDeleteBrush(b);
}

/* Anti-aliased rounded rectangle; the 1px border (if different from the
 * fill) is drawn inside the rectangle so edges stay crisp. */
static void round_rect(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border)
{
    void *gr = gp_begin(dc);
    float x = (float)r.left, y = (float)r.top;
    float w = (float)(r.right - r.left), h = (float)(r.bottom - r.top);
    void *outer = path_round(x, y, w, h, (float)rad);
    fill_path(gr, outer, border);
    GdipDeletePath(outer);
    if (border != fill) {
        float t = (float)(S(1) > 0 ? S(1) : 1);
        void *inner = path_round(x + t, y + t, w - 2 * t, h - 2 * t, (float)rad - t);
        fill_path(gr, inner, fill);
        GdipDeletePath(inner);
    }
    GdipDeleteGraphics(gr);
}

static void dot(HDC dc, int cx, int cy, int r, COLORREF c)
{
    void *gr = gp_begin(dc);
    void *b = NULL;
    GdipCreateSolidFill(argb(c), &b);
    GdipFillEllipse(gr, b, (float)(cx - r), (float)(cy - r), (float)(2 * r), (float)(2 * r));
    GdipDeleteBrush(b);
    GdipDeleteGraphics(gr);
}

static void check_mark(HDC dc, RECT box, COLORREF c)
{
    void *gr = gp_begin(dc);
    void *pen = NULL;
    float w = (float)(box.right - box.left), h = (float)(box.bottom - box.top);
    float x = (float)box.left, y = (float)box.top;
    GdipCreatePen1(argb(c), (float)g.dpi / 96.0f * 1.9f, 2 /* UnitPixel */, &pen);
    GdipSetPenStartCap(pen, 2); /* round */
    GdipSetPenEndCap(pen, 2);
    GdipSetPenLineJoin(pen, 2);
    HW_PointF pts[3] = {{x + w * 0.26f, y + h * 0.53f}, {x + w * 0.44f, y + h * 0.71f}, {x + w * 0.76f, y + h * 0.33f}};
    GdipDrawLines(gr, pen, pts, 3);
    GdipDeletePen(pen);
    GdipDeleteGraphics(gr);
}

/* Keycap-style chip centred on cy; returns its width. */
static int chip(HDC dc, int x, int cy, int h, const char *label, COLORREF fill, COLORREF bd, COLORREF tx)
{
    int pad = S(8);
    int w = text_w(dc, g.f_chip, label) + 2 * pad;
    if (w < h)
        w = h;
    RECT r = {x, cy - h / 2, x + w, cy - h / 2 + h};
    round_rect(dc, r, S(5), fill, bd);
    text_in(dc, g.f_chip, tx, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, label);
    return w;
}

static void fill_solid(HDC dc, RECT r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

/* ======================================================================== */
/*  Naming helpers                                                          */
/* ======================================================================== */

static const struct { uint8_t bit; const char *name; } MODS[] = {
    {0x01, "Ctrl"}, {0x02, "Shift"}, {0x04, "Alt"}, {0x08, "Win"},
    {0x10, "RCtrl"}, {0x20, "RShift"}, {0x40, "AltGr"}, {0x80, "RWin"},
};

/* "ctrl+shift+f12" -> tokens {"Ctrl","Shift","F12"} */
static int hotkey_tokens(const char *text, char tok[6][12])
{
    char buf[64];
    int n = 0;
    strncpy(buf, text, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *save = NULL;
    for (char *t = strtok_s(buf, "+", &save); t && n < 6; t = strtok_s(NULL, "+", &save)) {
        char *o = tok[n++];
        if (!strcmp(t, "ctrl")) strcpy(o, "Ctrl");
        else if (!strcmp(t, "alt")) strcpy(o, "Alt");
        else if (!strcmp(t, "shift")) strcpy(o, "Shift");
        else if (!strcmp(t, "win")) strcpy(o, "Win");
        else if (!strcmp(t, "pgup")) strcpy(o, "PgUp");
        else if (!strcmp(t, "pgdn")) strcpy(o, "PgDn");
        else {
            snprintf(o, 12, "%s", t);
            o[0] = (char)toupper((unsigned char)o[0]);
        }
    }
    return n;
}

static void hotkey_pretty(const char *text, char *out, size_t cap)
{
    char tok[6][12];
    int n = hotkey_tokens(text, tok);
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        strncat(out, tok[i], cap - strlen(out) - 1);
        if (i + 1 < n)
            strncat(out, "+", cap - strlen(out) - 1);
    }
}

/* ======================================================================== */
/*  History (memory only)                                                   */
/* ======================================================================== */

/* kind: 'd' key/button down, 'u' up, 'i' info, 'w' warning */
static void history_add(char kind, const char *label)
{
    /* The setting covers input only; arm/disarm events and their reasons
     * are always listed. */
    if (!g.history || (!g.cfg.history_enabled && (kind == 'd' || kind == 'u')))
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[128];
    snprintf(line, sizeof line, "%02d:%02d:%02d.%03d" "\x1f" "%s" "\x1f" "%c",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, label, kind);
    SendMessageA(g.history, LB_INSERTSTRING, 0, (LPARAM)line);
    int n = (int)SendMessageA(g.history, LB_GETCOUNT, 0, 0);
    while (n-- > HISTORY_MAX)
        SendMessageA(g.history, LB_DELETESTRING, HISTORY_MAX, 0);
}

/* Record paced output intent, not every rejected injected attempt. This is
 * still not an acknowledgment that the target USB report was consumed. */
static void history_state(const hidway_event_frame_t *f)
{
    for(unsigned i=0;i<8;i++) {
        uint8_t bit=(uint8_t)(1u<<i);
        if((f->keyboard.mods^g.logged_frame.keyboard.mods)&bit)
            history_add((f->keyboard.mods&bit)?'d':'u',MODS[i].name);
    }
    for(unsigned u=HIDWAY_KEY_USAGE_MIN;u<=HIDWAY_KEY_USAGE_MAX;u++) {
        bool now=hidway_bitmap_test(f->keyboard.keys,(uint8_t)u);
        if(now!=hidway_bitmap_test(g.logged_frame.keyboard.keys,(uint8_t)u)) {
            char name[8];history_add(now?'d':'u',hidway_usage_name((uint8_t)u,name));
        }
    }
    static const char *buttons[]={"Left click","Right click","Middle click","Mouse back","Mouse forward"};
    for(unsigned i=0;i<5;i++)if((f->buttons^g.logged_frame.buttons)&(1u<<i))
        history_add((f->buttons&(1u<<i))?'d':'u',buttons[i]);
    g.logged_frame=*f;
}

/* ======================================================================== */
/*  Transport                                                               */
/* ======================================================================== */

static FILE *latency_trace_file;
static unsigned latency_trace_lines;
static void set_armed(int on);
static uint8_t paced_buttons(uint32_t now);
static unsigned pulse_speed(unsigned origin,unsigned button);
static hidway_event_frame_t input_frame(void);
static int publish_input(void);
static void drain_capture(void);

/* Opt-in, bounded diagnostics: timings/counters only, never input or keys. */
static void latency_trace(const char *event, uint32_t value, unsigned detail)
{
    if (!latency_trace_file || latency_trace_lines >= 20000) return;
    fprintf(latency_trace_file, "%lu %s value=%lu detail=%u\n",
            (unsigned long)timeGetTime(), event, (unsigned long)value, detail);
    fflush(latency_trace_file);
    latency_trace_lines++;
}

static void net_send_state(uint8_t type)
{
    if (!g.net_ok || g.key_error)
        return;
    hidway_input_pkt_t p;
    memset(&p, 0, sizeof p);
    p.type = type;
    p.session_id = g.session_id;
    p.seq = ++g.seq;
    p.client_time_us = hidway_now_us();
    if (type == HIDWAY_MSG_STATE) {
        if(!publish_input())return;
        hidway_event_frame_t frame;
        if(!hidway_event_sample(&g.events,g.live_frame,timeGetTime(),&frame)) {
            set_armed(0);history_add('w',"Queued input expired - released");return;
        }
        if(!g.cfg.relay_keyboard)memset(&frame.keyboard,0,sizeof frame.keyboard);
        if(!g.cfg.relay_mouse)frame.buttons=0;
        history_state(&frame);
        if (g.cfg.relay_keyboard) {
            p.mods=frame.keyboard.mods;
            memcpy(p.keys,frame.keyboard.keys,sizeof p.keys);
        }
        if (g.cfg.relay_mouse) {
            p.buttons = frame.buttons;
            p.x = frame.x;
            p.y = frame.y;
            p.wheel = frame.wheel;
            p.pan = frame.pan;
        }
    }
    uint8_t buf[HIDWAY_INPUT_PKT_LEN];
    size_t n = hidway_input_encode(buf, &p);
    if (!g.crypto_on) {
        if (hidway_net_send(buf, n) > 0)
            g.sent_in_sec++;
        return;
    }
    /* End-to-end: only ciphertext leaves this PC. */
    uint8_t nonce[HIDWAY_NONCE_LEN], env[HIDWAY_SEAL_MAX];
    if (hidway_random(nonce, sizeof nonce) != 0)
        return; /* no randomness: send nothing rather than weaken the nonce */
    size_t el = hidway_seal(env, sizeof env, g.key, nonce, hidway_wall_us(), buf, n);
    hidway_wipe(buf, sizeof buf);
    if (el && hidway_net_send(env, el) > 0)
        g.sent_in_sec++;
}

static void net_drain_status(void)
{
    uint8_t buf[HIDWAY_SEAL_MAX + 16];
    /* Drain a delayed burst in one bounded pass instead of showing old RTT
     * samples over many GUI turns. Yield and continue if the budget is hit. */
    for (int i = 0; i < 256; i++) {
        int n = hidway_net_recv(buf, sizeof buf);
        if (n <= 0)
            return;
        const uint8_t *pkt = buf;
        size_t len = (size_t)n;
        uint8_t inner[HIDWAY_SEAL_MAX_INNER];
        if (g.crypto_on) {
            /* accept only authentic replies sealed with our key */
            len = hidway_open(inner, sizeof inner, g.key, buf, (size_t)n, NULL);
            if (!len)
                continue;
            pkt = inner;
        } else if (hidway_is_sealed(buf, (size_t)n)) {
            continue; /* relay has a key but we do not */
        }
        hidway_status_pkt_t s;
        if (!hidway_status_decode(pkt, len, &s) || s.session_id != g.session_id)
            continue;
        uint32_t received_at = hidway_now_us();
        uint32_t rtt = received_at - s.client_time_us;
        if (rtt >= 10000000u) {
            latency_trace("EXPIRED_STATUS_US", rtt, s.seq);
            continue; /* also rejects future echoes via unsigned subtraction */
        }
        /* A 32-bit microsecond ordering baseline is ambiguous after a long
         * outage. A reply to a recent request can establish a new baseline. */
        if (g.status_order.seen && timeGetTime() - g.last_status_received_ms >= 1800000u)
            memset(&g.status_order, 0, sizeof g.status_order);
        /* Keep the genuine age visible in diagnostics, but an older/duplicate
         * reply must not replace newer RTT or extend the health lease. */
        if (!hidway_status_order_accept(&g.status_order, s.client_time_us)) {
            latency_trace("STALE_STATUS_US", rtt, s.seq);
            continue;
        }
        latency_trace("STATUS_RTT_US", rtt, s.seq);
        g.last_status_received_ms = timeGetTime();
        g.status_seen = 1;
        g.ever_status = 1;
        g.probes_unanswered = 0;
        /* Base liveness on when this request left, not when an old reply was
         * finally processed. Queueing must not keep a dead link healthy. */
        g.last_status_us = s.client_time_us;
        g.pi_frames_ok = s.frames_ok;
        g.pi_gaps = s.seq_gaps;
        g.pi_flags = s.flags;
        if(g.armed&&(s.flags&HIDWAY_STATUS_INPUT_FAULT)) {
            set_armed(0);history_add('w',"Receiver input queue stalled - released; arm again");return;
        }
        if (rtt < 10000000u)
            g.rtt_ms = (int)(rtt / 1000u);
    }
    if (g.hwnd) PostMessageA(g.hwnd, WM_NET_READ, 0, 0);
}

static void set_armed(int on);
static void invalidate_readouts(void);

static void net_tick(void)
{
    if(!g.armed)return;
    drain_capture();
    if(!g.armed)return;
    if(InterlockedCompareExchange(&capture_fault,0,0)||
       InterlockedCompareExchange(&g.send_clock.failed,0,0)) {
        set_armed(0);history_add('w',"Input capture/sender failed - released");return;
    }
    net_send_state(HIDWAY_MSG_STATE);
    net_drain_status();

    uint32_t now = hidway_now_us();
    if (now - g.sec_start_us >= 1000000u) {
        g.pps = g.sent_in_sec;
        g.sent_in_sec = 0;
        g.sec_start_us = now;
    }
    uint32_t silence_ms = g.status_seen ? (now - g.last_status_us) / 1000u
                                       : timeGetTime() - g.armed_at_ms;
    if (g.armed && g.cfg.auto_disarm_ms > 0 && silence_ms >= (uint32_t)g.cfg.auto_disarm_ms) {
        set_armed(0);
        history_add('w', "Link lost - auto-disarmed");
    }
}

static int link_recent(void)
{
    return g.ever_status && (hidway_now_us() - g.last_status_us) < LINK_RECENT_US;
}

static void net_probe(void)
{
    if (g.net_ok) {
        net_send_state(HIDWAY_MSG_PROBE);
        g.probes_unanswered++;
    }
    net_drain_status();
}

/* The target PC as the relay last reported it (from the Pico), or -1
 * without a recent reply. */
static int target_state(void)
{
    if (!link_recent())
        return -1;
    return (g.pi_flags & HIDWAY_STATUS_TARGET_MASK) >> HIDWAY_STATUS_TARGET_SHIFT;
}

static int waking(void)
{
    return g.wake_sent_ms && timeGetTime() - g.wake_sent_ms < WAKE_WAIT_MS && target_state() != HIDWAY_TARGET_ON;
}

/* The relay can wake the target, and it is not on. */
static int wake_offered(void)
{
    return link_recent() && (g.pi_flags & HIDWAY_STATUS_WAKE) && target_state() != HIDWAY_TARGET_ON;
}

static void send_wake(void)
{
    if (!wake_offered() || g.preview)
        return;
    net_send_state(HIDWAY_MSG_WAKE);
    g.wake_sent_ms = timeGetTime();
    history_add('i', "Wake sent to the target PC");
    invalidate_readouts();
}

/* ======================================================================== */
/*  Tray                                                                    */
/* ======================================================================== */

static HICON make_dot_icon(COLORREF c)
{
    const int S2 = 32;
    HDC sdc = GetDC(NULL);
    HDC dc = CreateCompatibleDC(sdc);
    HBITMAP color = CreateCompatibleBitmap(sdc, S2, S2);
    HBITMAP mask = CreateBitmap(S2, S2, 1, 1, NULL);
    RECT full = {0, 0, S2, S2};

    HGDIOBJ ob = SelectObject(dc, color);
    FillRect(dc, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));
    HBRUSH fill = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ of = SelectObject(dc, fill), op = SelectObject(dc, pen);
    Ellipse(dc, 4, 4, S2 - 4, S2 - 4);
    SelectObject(dc, of);
    SelectObject(dc, op);
    DeleteObject(fill);
    DeleteObject(pen);

    SelectObject(dc, mask);
    FillRect(dc, &full, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HGDIOBJ obr = SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Ellipse(dc, 4, 4, S2 - 4, S2 - 4);
    SelectObject(dc, obr);

    SelectObject(dc, ob);
    DeleteDC(dc);
    ReleaseDC(NULL, sdc);

    ICONINFO ii = {TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

static void tray_update(void)
{
    if (!g.tray_shown)
        return;
    int want = g.armed ? (link_recent() ? 1 : 2) : 0;

    char tip[128];
    if (g.armed)
        snprintf(tip, sizeof tip, "HIDway - ARMED (RTT %d ms)", g.rtt_ms);
    else if (link_recent())
        snprintf(tip, sizeof tip, "HIDway - disarmed, relay reachable (%d ms)", g.rtt_ms);
    else
        snprintf(tip, sizeof tip, "HIDway - disarmed");

    g.nid.uFlags = NIF_TIP;
    strncpy(g.nid.szTip, tip, sizeof g.nid.szTip - 1);
    g.nid.szTip[sizeof g.nid.szTip - 1] = 0;
    if (want != g.cur_icon) {
        g.cur_icon = want;
        g.nid.hIcon = want == 1 ? g.ic_green : want == 2 ? g.ic_red : g.ic_grey;
        g.nid.uFlags |= NIF_ICON;
    }
    Shell_NotifyIconA(NIM_MODIFY, &g.nid);
}

static void tray_init(HWND hwnd)
{
    g.ic_grey = make_dot_icon(RGB(128, 132, 140));
    g.ic_green = make_dot_icon(C_LIVE);
    g.ic_red = make_dot_icon(C_WARN);

    memset(&g.nid, 0, sizeof g.nid);
    g.nid.cbSize = sizeof g.nid;
    g.nid.hWnd = hwnd;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAYICON;
    g.nid.hIcon = g.ic_grey;
    g.cur_icon = 0;
    strcpy(g.nid.szTip, "HIDway - disarmed");
    g.tray_shown = Shell_NotifyIconA(NIM_ADD, &g.nid);
}

static void tray_destroy(void)
{
    if (g.tray_shown)
        Shell_NotifyIconA(NIM_DELETE, &g.nid);
    if (g.ic_grey) DestroyIcon(g.ic_grey);
    if (g.ic_green) DestroyIcon(g.ic_green);
    if (g.ic_red) DestroyIcon(g.ic_red);
}

static void show_main_window(int show)
{
    if (show) {
        ShowWindow(g.hwnd, SW_SHOW);
        ShowWindow(g.hwnd, SW_RESTORE);
        SetForegroundWindow(g.hwnd);
    } else {
        ShowWindow(g.hwnd, SW_HIDE);
    }
}

static void monitor(void)
{
    if (!g.armed)
        net_probe();
    else
        net_drain_status();

    /* Note changes of the target PC's state (and the end of a wake wait). */
    int ts = target_state();
    if (ts >= 0 && ts != g.shown_target) {
        static const char *names[] = {"no report from the Pico", "off", "asleep", "on"};
        char line[64];
        snprintf(line, sizeof line, "Target PC: %s", names[ts & 3]);
        if (g.shown_target != 0 || ts != HIDWAY_TARGET_UNKNOWN)
            history_add('i', line);
        g.shown_target = ts;
        if (ts == HIDWAY_TARGET_ON)
            g.wake_sent_ms = 0;
    }

    /* Track this ARM session only. A delayed reply is not proof that the
     * relay released the target. Keep status feedback inside the app. */
    int up = g.armed && g.status_seen && link_recent();
    if (g.prev_link_up && !up && g.armed) {
        history_add('w', "Relay reply delayed");
    }
    g.prev_link_up = up;
    tray_update();
    invalidate_readouts();
}

/* ======================================================================== */
/*  Input capture                                                           */
/* ======================================================================== */

static void clear_relay_state(void)
{
    hidway_event_frame_t neutral={0};history_state(&neutral);
    g.modifiers = 0;
    memset(g.keys, 0, sizeof g.keys);
    g.buttons = 0;
    g.key_buttons = 0;
    memset(&g.events,0,sizeof g.events);
    memset(&g.inputs,0,sizeof g.inputs);
    memset(&g.held,0,sizeof g.held);
    memset(g.pulse,0,sizeof g.pulse);
    g.unicode_warned=0;
}

static uint8_t repeat_mask(unsigned origin)
{
    unsigned source=1u<<origin;
    return g.cfg.repeat_enabled&&(g.cfg.repeat_sources&(int)source)?(uint8_t)(1u<<g.cfg.repeat_button):0;
}
static uint8_t native_buttons(void)
{
    uint8_t b=0;
    for(unsigned o=0;o<2;o++)b|=g.held.source_buttons[o]&(uint8_t)~repeat_mask(o);
    return b;
}
static uint8_t paced_buttons(uint32_t now)
{
    uint8_t b=0;
    for(unsigned o=0;o<3;o++)for(unsigned i=0;i<5;i++) {
        uint8_t bit=(uint8_t)(1u<<i);
        bool repeating=(repeat_mask(o)&bit)!=0;
        if(o!=2&&!repeating)continue;
        if(hidway_event_backlog_ms(&g.events)>32&&!g.pulse[o][i].sampled)continue;
        unsigned cps=pulse_speed(o,i);
        if(hidway_pulse_sample(&g.pulse[o][i],now,cps,repeating))b|=bit;
    }
    return b;
}
static unsigned pulse_speed(unsigned origin,unsigned button)
{
    unsigned speed=(unsigned)((repeat_mask(origin)&(1u<<button))?g.cfg.repeat_cps:g.cfg.virtual_click_cps);
    if(origin==2&&speed>(unsigned)g.cfg.virtual_click_cps)speed=(unsigned)g.cfg.virtual_click_cps;
    return speed;
}
static hidway_event_frame_t input_frame(void)
{
    hidway_event_frame_t f={0};f.keyboard.mods=g.modifiers;memcpy(f.keyboard.keys,g.keys,sizeof g.keys);
    f.buttons=native_buttons()|paced_buttons(timeGetTime());
    f.x=g.cum_x;f.y=g.cum_y;f.wheel=g.cum_wheel;f.pan=g.cum_pan;return f;
}
static int publish_input(void)
{
    if(!g.armed)return 0;
    g.live_frame=input_frame();
    if(hidway_event_push(&g.events,g.live_frame,timeGetTime()))return 1;
    set_armed(0);history_add('w',"Input transition queue full - released");return 0;
}

static void merge_inputs(void)
{
    hidway_input_merge(&g.inputs,&g.held);
    g.modifiers=g.held.mods;memcpy(g.keys,g.held.keys,sizeof g.keys);
    g.buttons=g.held.buttons;g.key_buttons=g.held.aliases;
    for(unsigned o=0;o<3;o++)for(unsigned i=0;i<5;i++)
        if(o==2||(repeat_mask(o)&(1u<<i)))
            if(g.held.source_buttons[o]&(1u<<i))hidway_pulse_input(&g.pulse[o][i],true,timeGetTime());
            else hidway_pulse_release(&g.pulse[o][i],timeGetTime(),pulse_speed(o,i));
    publish_input();
}

static LONG input_source_mask(void)
{
    if(g.cfg.loopback_mode)return g.cfg.source_virtual?HIDWAY_SOURCE_VIRTUAL:0;
    return (g.cfg.source_physical ? HIDWAY_SOURCE_PHYSICAL : 0) |
           (g.cfg.source_touchpad ? HIDWAY_SOURCE_TOUCHPAD : 0) |
           (g.cfg.source_virtual ? HIDWAY_SOURCE_VIRTUAL : 0);
}

static int net_interval_ms(void)
{
    int ms = 1000 / (g.cfg.send_rate_hz > 0 ? g.cfg.send_rate_hz : 250);
    return ms < 1 ? 1 : ms;
}

/*
 * While ARMED a small always-on-top badge owns the focus and the cursor.
 * Low-level hooks route the selected keyboard and virtual-mouse sources;
 * physical/absolute mouse reports still use Raw Input. Injected events are
 * accepted only when Virtual / remote is explicitly enabled.
 *
 * Anything that takes the focus away (another window, the lock or secure
 * screen, a display change, sleep, a required hook going missing) disarms,
 * which releases everything on the target.
 */
#ifndef LLKHF_LOWER_IL_INJECTED
#define LLKHF_LOWER_IL_INJECTED 0x02
#endif
#ifndef LLMHF_LOWER_IL_INJECTED
#define LLMHF_LOWER_IL_INJECTED 0x02
#endif

/* Shared with the hook thread. */
static volatile LONG cap_active; /* armed and the badge holds the focus */
static HWND volatile cap_badge;
static volatile LONG cap_keyboard, cap_mouse, cap_sources;
static volatile LONG cap_require_badge, cap_consume_virtual;
static volatile LONG cap_route_motion;
static uint8_t cap_ours[2][768]; /* scan/E0 identity, or VK identity when scan is absent */
static uint8_t cap_key_vk[2][768]; /* retain DOWN VK across layout changes */
static uint8_t cap_mouse_ours;
static POINT cap_mouse_last, cap_warp_point;
static int cap_mouse_last_valid;
static volatile LONG cap_warp_pending;

static LRESULT CALLBACK capture_hook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
        BYTE vk = (BYTE)k->vkCode;
        BOOL up = (k->flags & LLKHF_UP) != 0;
        BOOL injected = (k->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
        unsigned source = hidway_keyboard_source(injected != FALSE);
        int slot = injected ? 1 : 0;
        unsigned identity=k->scanCode&&k->scanCode<=255?
            (unsigned)k->scanCode+((k->flags&LLKHF_EXTENDED)?256u:0u):512u+vk;
        if (cap_active && cap_keyboard && (cap_sources & source) &&
            (!cap_require_badge || GetForegroundWindow() == cap_badge) && (!up || cap_ours[slot][identity])) {
            if(!up&&!cap_ours[slot][identity])cap_key_vk[slot][identity]=vk;
            vk=cap_key_vk[slot][identity];
            cap_ours[slot][identity] = (uint8_t)!up;
            LPARAM info = (LPARAM)((k->scanCode & 0xFFFFu) | ((k->flags & LLKHF_EXTENDED) ? KEYF_E0 : 0) |
                                   (up ? KEYF_UP : 0) | (injected ? KEYF_VIRTUAL : 0));
            if(!capture_enqueue(cap_badge,WM_CAPTURE_READY,WM_CAPTURE_KEY,(WPARAM)vk,info))
                InterlockedExchange(&cap_active,0);
            return 1; /* consumed: this PC never sees the key */
        }
        /* Not ours, or the release of a key held down before arming: this PC
         * must see that, or the key would stay down here. */
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static LRESULT CALLBACK capture_mouse_hook(int code, WPARAM wp, LPARAM lp)
{
    int consume = 0;
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)lp;
        BOOL injected = (m->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) != 0;
        if (wp == WM_MOUSEMOVE && injected && InterlockedCompareExchange(&cap_warp_pending, 0, 0) &&
            m->pt.x == cap_warp_point.x && m->pt.y == cap_warp_point.y) {
            cap_mouse_last = m->pt;
            cap_mouse_last_valid = 1;
            InterlockedExchange(&cap_warp_pending, 0);
        } else if (injected && cap_active && cap_mouse && (cap_sources & HIDWAY_SOURCE_VIRTUAL) &&
                   (!cap_require_badge || GetForegroundWindow() == cap_badge)) {
            if(wp==WM_MOUSEMOVE&&!cap_route_motion) {
                /* Native placement avoids applying the host's relative-HID
                 * acceleration to an already positioned remote cursor. */
                cap_mouse_last=m->pt;cap_mouse_last_valid=1;
                return CallNextHookEx(NULL,code,wp,lp);
            }
            uint8_t bit=0;bool up=false;
            switch(wp) {
            case WM_LBUTTONUP:up=true;/* fall through */
            case WM_LBUTTONDOWN:bit=BTN_L;break;
            case WM_RBUTTONUP:up=true;/* fall through */
            case WM_RBUTTONDOWN:bit=BTN_R;break;
            case WM_MBUTTONUP:up=true;/* fall through */
            case WM_MBUTTONDOWN:bit=BTN_M;break;
            case WM_XBUTTONUP:up=true;/* fall through */
            case WM_XBUTTONDOWN:bit=HIWORD(m->mouseData)==XBUTTON1?BTN_X1:BTN_X2;break;
            }
            if(bit&&up&&!(cap_mouse_ours&bit))return CallNextHookEx(NULL,code,wp,lp);
            if(bit){if(up)cap_mouse_ours&=(uint8_t)~bit;else cap_mouse_ours|=bit;}
            LPARAM data = (LPARAM)m->mouseData;
            if (wp == WM_MOUSEMOVE) {
                POINT base = cap_mouse_last;
                int have_base = cap_mouse_last_valid;
                if (cap_consume_virtual)
                    have_base = GetCursorPos(&base) ? 1 : 0;
                if (have_base) {
                    int dx = m->pt.x - base.x, dy = m->pt.y - base.y;
                    if (dx < -32768) dx = -32768; else if (dx > 32767) dx = 32767;
                    if (dy < -32768) dy = -32768; else if (dy > 32767) dy = 32767;
                    data = MAKELPARAM((WORD)(SHORT)dx, (WORD)(SHORT)dy);
                } else {
                    data = 0;
                }
                cap_mouse_last = m->pt;
                cap_mouse_last_valid = 1;
            }
            if(!capture_enqueue(cap_badge,WM_CAPTURE_READY,WM_CAPTURE_MOUSE,wp,data))
                InterlockedExchange(&cap_active,0);
            consume = cap_consume_virtual != 0;
        }
    }
    /* The badge absorbs the legacy event. Allowing it through also lets the
     * cursor position advance, which is needed to derive relative motion. */
    return consume ? 1 : CallNextHookEx(NULL, code, wp, lp);
}

static DWORD WINAPI capture_hook_thread(LPVOID ready)
{
    MSG m;
    PeekMessageA(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE); /* create the message queue */
    HMODULE module = GetModuleHandleA(NULL);
    HHOOK keyboard = SetWindowsHookExA(WH_KEYBOARD_LL, capture_hook, module, 0);
    HHOOK mouse = SetWindowsHookExA(WH_MOUSE_LL, capture_mouse_hook, module, 0);
    InterlockedExchange(&g.hook_ok, (keyboard ? 1 : 0) | (mouse ? 2 : 0));
    SetEvent((HANDLE)ready);
    if (!keyboard && !mouse)
        return 1;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
    }
    if (keyboard) UnhookWindowsHookEx(keyboard);
    if (mouse) UnhookWindowsHookEx(mouse);
    return 0;
}

static void hook_stop(void)
{
    if (!g.hook_thread)
        return;
    PostThreadMessageA(g.hook_tid, WM_QUIT, 0, 0);
    if(WaitForSingleObject(g.hook_thread,1000)!=WAIT_OBJECT_0)return;
    CloseHandle(g.hook_thread);
    g.hook_thread = NULL;
    if(g.hook_ready){CloseHandle(g.hook_ready);g.hook_ready=NULL;}
}

static int hook_start(void)
{
    if(g.hook_thread){hook_stop();if(g.hook_thread)return 0;}
    memset(cap_ours, 0, sizeof cap_ours);
    memset(cap_key_vk,0,sizeof cap_key_vk);
    cap_mouse_ours=0;
    cap_mouse_last_valid = GetCursorPos(&cap_mouse_last) ? 1 : 0;
    InterlockedExchange(&cap_warp_pending, 0);
    HANDLE ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ready)
        return 0;
    InterlockedExchange(&g.hook_ok, 0);
    g.hook_ready=ready;
    g.hook_thread = CreateThread(NULL, 0, capture_hook_thread, ready, 0, &g.hook_tid);
    if (g.hook_thread) {
        SetThreadPriority(g.hook_thread, THREAD_PRIORITY_HIGHEST);
        if(WaitForSingleObject(ready,2000)!=WAIT_OBJECT_0){hook_stop();return 0;}
    }
    CloseHandle(ready);
    g.hook_ready=NULL;
    if ((g.cfg.relay_keyboard && (g.cfg.source_physical || g.cfg.source_virtual) && !(g.hook_ok & 1)) ||
        (g.cfg.relay_mouse && g.cfg.source_virtual && !(g.hook_ok & 2))) {
        hook_stop();
        return 0;
    }
    return 1;
}

/* Mouse Raw Input only while armed. Exclusive capture uses foreground-only
 * delivery; virtual-only mode uses INPUTSINK so the original app can keep
 * focus while injected input is translated through the Pico. */
static int raw_input_to(HWND target, int background)
{
    DWORD flags = target ? (background ? RIDEV_INPUTSINK : 0) : RIDEV_REMOVE;
    RAWINPUTDEVICE d = {0x01, 0x02, flags, target};
    return RegisterRawInputDevices(&d, 1, sizeof d) ? 1 : 0;
}

/* ---- badge ---------------------------------------------------------------- */

static int BS(int v)
{
    return MulDiv(v, (int)g.badge_dpi, 96);
}

static HFONT badge_font(int tenths_px, int weight)
{
    return CreateFontA(-MulDiv(tenths_px, (int)g.badge_dpi, 960), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       "Segoe UI Variable Text");
}

/* Main text, hint and dot colour for the current state. */
static COLORREF badge_texts(char *st, size_t st_cap, char *hint, size_t hint_cap)
{
    char hk[48];
    hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
    if (!g.armed) { /* a small "Arm" pill with a red dot; no hint, to keep it out of the way */
        hint[0] = 0;
        (void)hint_cap;
        snprintf(st, st_cap, link_recent() ? "Arm" : "Offline");
        return C_WARN;
    }
    snprintf(hint, hint_cap, "%s to release", hk);
    if (!link_recent()) {
        snprintf(st, st_cap, "ARMED  ·  no link");
        return C_AMBER;
    }
    snprintf(st, st_cap, "ARMED  ·  %d ms", g.rtt_ms);
    return C_LIVE;
}

/* Size the badge for the monitor it is on and keep it there: centred on
 * badge_x with its top at badge_y, or at the top centre of the main window's
 * monitor until the user drags it somewhere. */
static void badge_layout(void)
{
    POINT at = {g.cfg.badge_x, g.cfg.badge_y};
    HMONITOR mon = at.x != INT_MIN && at.y != INT_MIN ? MonitorFromPoint(at, MONITOR_DEFAULTTONULL) : NULL;
    MONITORINFO mi = {sizeof mi};
    int placed = mon != NULL; /* else unset, or that monitor is gone */
    if (!placed)
        mon = MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoA(mon, &mi);
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)))
        dx = 96;
    g.badge_dpi = dx;

    if (g.bf_main) DeleteObject(g.bf_main);
    if (g.bf_hint) DeleteObject(g.bf_hint);
    g.bf_main = badge_font(120, FW_SEMIBOLD);
    g.bf_hint = badge_font(105, FW_NORMAL);

    /* Disarmed: a small pill wide enough for "Offline". Armed: room for the
     * widest status and the release hint. */
    g.badge_layout_armed = g.armed;
    HDC dc = GetDC(g.badge);
    int w, h;
    if (g.armed) {
        char hk[48], hint[64];
        hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
        snprintf(hint, sizeof hint, "%s to release", hk);
        w = BS(30) + text_w(dc, g.bf_main, "ARMED  ·  9999 ms") + BS(18) + text_w(dc, g.bf_hint, hint) + BS(16);
        h = BS(32);
    } else {
        w = BS(30) + text_w(dc, g.bf_main, "Offline") + BS(16);
        h = BS(32);
    }
    ReleaseDC(g.badge, dc);

    if (!placed) {
        at.x = (mi.rcMonitor.left + mi.rcMonitor.right) / 2;
        at.y = mi.rcMonitor.top + BS(10);
    }
    int x = at.x - w / 2, y = at.y;
    if (x + w > mi.rcMonitor.right) x = mi.rcMonitor.right - w;
    if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;
    if (y + h > mi.rcMonitor.bottom) y = mi.rcMonitor.bottom - h;
    if (y < mi.rcMonitor.top) y = mi.rcMonitor.top;
    SetWindowPos(g.badge, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

/* After a drag: remember where the user put it, and resize for that monitor. */
static void badge_remember_position(void)
{
    RECT r;
    GetWindowRect(g.badge, &r);
    g.cfg.badge_x = (r.left + r.right) / 2;
    g.cfg.badge_y = r.top;
    if (!g.preview)
        hidway_config_save("hidway.ini", &g.cfg);
    badge_layout();
}

static void badge_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bm);

    fill_solid(mem, rc, g.badge_hover && !g.armed ? C_TILE : C_CARD);
    char st[48], hint[64];
    COLORREF c = badge_texts(st, sizeof st, hint, sizeof hint);
    int cy = rc.bottom / 2;
    dot(mem, BS(16), cy, BS(5), c);
    if (g.armed) {
        text_vc(mem, g.bf_main, C_TEXT, BS(30), cy, st);
        RECT hr = {0, 0, rc.right - BS(16), rc.bottom};
        text_in(mem, g.bf_hint, C_TEXT3, hr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, hint);
    } else {
        text_vc(mem, g.bf_main, g.badge_hover ? C_TEXT : C_TEXT2, BS(30), cy, st);
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void badge_set_alpha(BYTE a)
{
    if (a != g.badge_alpha) {
        g.badge_alpha = a;
        SetLayeredWindowAttributes(g.badge, 0, a, LWA_ALPHA);
    }
}

/* Visibility, text, border and fade for the current state. Cheap: called on
 * every UI tick and every state change. */
static void badge_refresh(void)
{
    if (!g.badge)
        return;
    int show = !g.shot_path[0] && (g.armed || g.cfg.badge);
    if (show != (IsWindowVisible(g.badge) != 0))
        ShowWindow(g.badge, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (!show)
        return;
    if (g.armed != g.badge_layout_armed)
        badge_layout(); /* the two states have different sizes */

    char st[48], hint[64], key[64];
    badge_texts(st, sizeof st, hint, sizeof hint);
    snprintf(key, sizeof key, "%d%d%s", g.armed, g.badge_hover, st);
    if (strcmp(key, g.badge_text) != 0) {
        strcpy(g.badge_text, key);
        InvalidateRect(g.badge, NULL, FALSE);
    }

    BYTE a;
    if (g.armed)
        a = timeGetTime() - g.armed_at_ms > BADGE_FADE_MS && link_recent() ? 120 : 240;
    else
        a = g.badge_hover || g.badge_drag ? 250 : 200;
    badge_set_alpha(a);

    COLORREF bd = g.armed ? C_LIVE_BD : C_CARD_BD;
    if (bd != g.badge_border) {
        g.badge_border = bd;
        DwmSetWindowAttribute(g.badge, HW_DWMWA_BORDER, &bd, sizeof bd);
    }
}

static void confine_cursor(void)
{
    RECT r;
    GetWindowRect(g.badge, &r);
    InflateRect(&r, -BS(4), -BS(4));
    ClipCursor(&r);
}

/* ---- arm / disarm ---------------------------------------------------------- */

static int capture_start(void)
{
    HWND fg = GetForegroundWindow();
    g.prev_fg = fg != g.badge ? fg : NULL;
    GetCursorPos(&g.cursor_home);
    badge_layout();
    g.exclusive_capture = !g.cfg.loopback_mode;
    if(!input_source_mask())return 0;

    InterlockedExchange(&cap_keyboard, g.cfg.relay_keyboard);
    InterlockedExchange(&cap_mouse, g.cfg.relay_mouse);
    InterlockedExchange(&cap_sources, input_source_mask());
    g.generation=(uint32_t)InterlockedIncrement(&capture_generation);
    capture_queue_clear();InterlockedExchange(&capture_fault,0);
    InterlockedExchange(&cap_require_badge, g.exclusive_capture);
    InterlockedExchange(&cap_consume_virtual, !g.exclusive_capture);
    InterlockedExchange(&cap_route_motion,!g.cfg.loopback_mode||g.cfg.loopback_pointer_pico);
    memset(g.absolute, 0, sizeof g.absolute);
    if (((g.cfg.relay_keyboard && (g.cfg.source_physical || g.cfg.source_virtual)) ||
         (g.cfg.relay_mouse && (g.cfg.source_touchpad || g.cfg.source_virtual))) && !hook_start())
        return 0;
    ShowWindow(g.badge, g.exclusive_capture ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (g.exclusive_capture) {
        SetForegroundWindow(g.badge);
        SetFocus(g.badge);
    }
    if ((g.exclusive_capture && GetForegroundWindow() != g.badge) ||
        !raw_input_to(g.badge, !g.exclusive_capture)) {
        hook_stop();
        return 0;
    }
    if (g.exclusive_capture)
        confine_cursor();
    InterlockedExchange(&cap_active, 1);
    /* Keys pressed while the badge was taking the focus are not the hook's;
     * drop them so they are not mistaken for a missing hook. */
    MSG m;
    while (g.exclusive_capture && PeekMessageA(&m, g.badge, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
    }
    return 1;
}

static void capture_stop(void)
{
    InterlockedExchange(&cap_active, 0);
    InterlockedIncrement(&capture_generation);
    InterlockedExchange(&cap_keyboard, 0);
    InterlockedExchange(&cap_mouse, 0);
    InterlockedExchange(&cap_sources, 0);
    InterlockedExchange(&cap_require_badge, 0);
    InterlockedExchange(&cap_consume_virtual, 0);
    InterlockedExchange(&cap_route_motion, 0);
    hook_stop();
    capture_queue_clear();
    raw_input_to(NULL, 0);
    ClipCursor(NULL);
    /* Back to where the user was, unless something else took the focus. */
    if (g.exclusive_capture && GetForegroundWindow() == g.badge && g.prev_fg && IsWindow(g.prev_fg))
        SetForegroundWindow(g.prev_fg);
    if (g.exclusive_capture)
        SetCursorPos(g.cursor_home.x, g.cursor_home.y);
    g.prev_fg = NULL;
}

/* Disarm because another window took the focus, naming its program. */
static void focus_lost(HWND to)
{
    char msg[128] = "Focus lost - auto-disarmed";
    if (!to)
        to = GetForegroundWindow();
    DWORD pid = 0;
    if (to)
        GetWindowThreadProcessId(to, &pid);
    HANDLE p = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL;
    if (p) {
        char path[MAX_PATH];
        DWORD n = sizeof path;
        if (QueryFullProcessImageNameA(p, 0, path, &n)) {
            const char *base = strrchr(path, '\\');
            snprintf(msg, sizeof msg, "Focus lost to %s - auto-disarmed", base ? base + 1 : path);
        }
        CloseHandle(p);
    } else if (!to) {
        snprintf(msg, sizeof msg, "Focus lost (lock or secure screen) - auto-disarmed");
    }
    set_armed(0);
    history_add('w', msg);
}

/* Every UI tick while armed: the focus is still ours and the cursor stays
 * confined. */
static void capture_tick(void)
{
    if(InterlockedCompareExchange(&capture_fault,0,0)||
       InterlockedCompareExchange(&g.send_clock.failed,0,0)||
       (g.send_clock.thread&&WaitForSingleObject(g.send_clock.thread,0)==WAIT_OBJECT_0)) {
        set_armed(0);history_add('w',"Input capture/sender failed - released");return;
    }
    if (!g.exclusive_capture)
        return;
    if (GetForegroundWindow() != g.badge) {
        focus_lost(NULL);
        return;
    }
    confine_cursor();
    if (g.cfg.relay_mouse && g.cfg.source_virtual) {
        RECT r;
        POINT p;
        GetWindowRect(g.badge, &r);
        GetCursorPos(&p);
        int margin = BS(7);
        if (p.x <= r.left + margin || p.x >= r.right - margin ||
            p.y <= r.top + margin || p.y >= r.bottom - margin) {
            cap_warp_point.x = (r.left + r.right) / 2;
            cap_warp_point.y = (r.top + r.bottom) / 2;
            InterlockedExchange(&cap_warp_pending, 1);
            SetCursorPos(cap_warp_point.x, cap_warp_point.y);
        }
    }
}

static void set_armed(int on)
{
    if (on == g.armed)
        return;
    if(on&&g.options){history_add('i',"Close input options before arming");return;}
    if(on) {
        uint32_t session=0;
        if(hidway_random((uint8_t*)&session,sizeof session)!=0) {
            history_add('w',"Could not create input session - not armed");return;
        }
        if(session==g.session_id)session^=0x80000000u;
        g.session_id=session;g.seq=0;memset(&g.status_order,0,sizeof g.status_order);
    }
    if (on && !g.preview && !capture_start()) {
        history_add('w', "Could not start input capture - not armed");
        badge_refresh();
        return;
    }
    if(!on) {
        g.last_limited=0;
        for(unsigned o=0;o<3;o++)for(unsigned i=0;i<5;i++)g.last_limited+=g.pulse[o][i].limited;
    }
    g.armed = on;
    g.prev_link_up = 0;
    clear_relay_state();
    g.end_as_del = 0;

    if (on) {
        g.sec_start_us = hidway_now_us();
        g.sent_in_sec = 0;
        g.status_seen = 0;
        g.armed_at_ms = timeGetTime();
        net_send_state(HIDWAY_MSG_STATE);
        if(!g.preview&&!hidway_send_clock_start(&g.send_clock,g.hwnd,WM_SEND_TICK,
                                               (unsigned)net_interval_ms(),g.generation)) {
            set_armed(0);history_add('w',"Could not start sender - released");return;
        }
        history_add('i', "Armed - input goes to the target only");
    } else {
        if(!g.preview)hidway_send_clock_stop(&g.send_clock);
        if (!g.preview)
            capture_stop();
        for (int i = 0; i < 3; i++)
            net_send_state(HIDWAY_MSG_RELEASE);
        g.pps = 0;
        history_add('i', "Disarmed - all released");
    }
    InvalidateRect(g.arm, NULL, FALSE);
    invalidate_readouts();
    tray_update();
    badge_refresh();
    if (g.armed && g.exclusive_capture && !g.preview)
        confine_cursor(); /* to the badge at its armed size */
}

static unsigned held_mods(void)
{
    unsigned m = 0;
    if (g.modifiers & 0x11) m |= MOD_CONTROL;
    if (g.modifiers & 0x22) m |= MOD_SHIFT;
    if (g.modifiers & 0x44) m |= MOD_ALT;
    if (g.modifiers & 0x88) m |= MOD_WIN;
    return m;
}

/* A key consumed by the capture hook (keyboard relay on). */
static void on_capture_key(UINT vk, LPARAM info)
{
    if (!g.armed)
        return; /* queued before a disarm: the target is already released */
    bool up = (info & KEYF_UP) != 0;
    bool e0 = (info & KEYF_E0) != 0;
    unsigned sc = (unsigned)(info & 0xFFFF);
    unsigned source=(info&KEYF_VIRTUAL)?HIDWAY_SOURCE_VIRTUAL:HIDWAY_SOURCE_PHYSICAL;
    if(!(input_source_mask()&source)||vk>255)return;
    unsigned mods = held_mods();

    if(up) {
        hidway_input_key(&g.inputs,source,vk,0,true);merge_inputs();
        return;
    }
    if(vk==VK_PACKET) {
        if(!g.unicode_warned){history_add('w',"Unicode text input is unsupported; use key input");g.unicode_warned=1;}
        return;
    }

    if (!up) {
        if (g.cfg.panic.vk && vk == g.cfg.panic.vk && mods == g.cfg.panic.mods) {
            set_armed(0);
            history_add('w', "PANIC - all released");
            return;
        }
        if (vk == g.cfg.toggle.vk && mods == g.cfg.toggle.mods) {
            set_armed(0);
            return;
        }
        /* Windows keeps Ctrl+Alt+Del and Win+L for itself; they act on this
         * PC, so never pass them on and release the target. */
        if ((vk == VK_DELETE && (mods & MOD_CONTROL) && (mods & MOD_ALT)) || (vk == 'L' && (mods & MOD_WIN))) {
            set_armed(0);
            history_add('w', "Reserved shortcut - disarmed");
            return;
        }
    }
    /* A key standing in for a mouse side button (mouse4_key / mouse5_key):
     * held while the key is held, one press for one press. */
    if ((g.cfg.mouse4.vk && vk == g.cfg.mouse4.vk) || (g.cfg.mouse5.vk && vk == g.cfg.mouse5.vk)) {
        uint8_t bit = vk == g.cfg.mouse4.vk ? BTN_X1 : BTN_X2;
        hidway_input_key(&g.inputs,source,vk,HIDWAY_BIND_MOUSE|bit,false);merge_inputs();
        return;
    }
    /* Ctrl+Alt+End is Ctrl+Alt+Del on the target, as in Remote Desktop. */
    if (vk == VK_END && (mods & MOD_CONTROL) && (mods & MOD_ALT)) {
        hidway_input_key(&g.inputs,source,vk,0x4c,false);merge_inputs();
        return;
    }
    /* Keys whose hook scan codes do not follow the Set-1 table. */
    if (vk == VK_PAUSE || vk == VK_NUMLOCK || vk == VK_SNAPSHOT) {
        hidway_input_key(&g.inputs,source,vk,(uint16_t)(vk==VK_PAUSE?0x48:vk==VK_NUMLOCK?0x53:0x46),false);
        merge_inputs();
        return;
    }
    if (vk == VK_RSHIFT)
        e0 = false; /* RShift has its own code; the hook may flag it extended */
    if(sc==0&&source==HIDWAY_SOURCE_VIRTUAL) {
        UINT mapped=MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC_EX,GetKeyboardLayout(0));
        if((mapped&0xff00u)==0xe000u)e0=true;
        else if(mapped&0xff00u)return; /* E1 sequences require deliberate mapping */
        sc=mapped&0xffu;
        /* VK navigation alone loses keypad-vs-dedicated identity. Windows
         * may return the non-E0 keypad scan even with VSC_EX. Prefer the
         * semantic navigation key only for this scan-less virtual fallback. */
        switch(vk) {
        case VK_LEFT:case VK_RIGHT:case VK_UP:case VK_DOWN:case VK_HOME:case VK_END:
        case VK_PRIOR:case VK_NEXT:case VK_INSERT:case VK_DELETE:case VK_DIVIDE:
            e0=true;break;
        }
    }
    if(sc==0||sc>0xff)return; /* do not synthesize physical layout helper keys */
    uint8_t mod=hidway_sc_to_modifier((uint8_t)sc,e0);
    uint16_t binding=mod?(uint16_t)(HIDWAY_BIND_MOD|mod):hidway_sc_to_usage((uint8_t)sc,e0);
    hidway_input_key(&g.inputs,source,vk,binding,false);merge_inputs();
}

/* While armed the hook consumes every physical key press, so a key press
 * that still reaches the badge as a window message means the hook is gone
 * (Windows drops hooks it considers unresponsive): stop instead of letting
 * keys act on this PC. A key injected by software also lands here; stopping
 * is then the safe side too. */
static void on_badge_keydown(WPARAM vk)
{
    if (g.armed && g.hook_thread && g.cfg.relay_keyboard && g.cfg.source_physical &&
        !g.cfg.loopback_mode) {
        char msg[80];
        snprintf(msg, sizeof msg, "Key capture stopped (vk %02X) - auto-disarmed", (unsigned)vk);
        set_armed(0);
        history_add('w', msg);
    }
}

static void mouse_edge(unsigned source,USHORT bf, USHORT d, USHORT u, uint8_t bit, const char *name)
{
    (void)name; /* labels are emitted from the ordered output state */
    if(!g.armed)return;
    if (bf & d) {hidway_input_button(&g.inputs,source,bit,false);merge_inputs();}
    if (bf & u) {hidway_input_button(&g.inputs,source,bit,true);merge_inputs();}
}

static int absolute_slot(HANDLE device)
{
    int empty = -1;
    for (int i = 0; i < (int)(sizeof g.absolute / sizeof g.absolute[0]); i++) {
        if (g.absolute[i].device == device)
            return i;
        if (!g.absolute[i].device && empty < 0)
            empty = i;
    }
    if (empty < 0)
        empty = 0;
    if (g.absolute[empty].device != device) {
        memset(&g.absolute[empty], 0, sizeof g.absolute[empty]);
        g.absolute[empty].device = device;
    }
    return empty;
}

static int raw_device_is_loopback(HANDLE device)
{
    if (!device || !g.cfg.loopback_device[0])
        return 0;
    char path[512];
    UINT chars = sizeof path;
    if (GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, path, &chars) == (UINT)-1)
        return 0;
    path[sizeof path - 1] = 0;
    return hidway_device_path_matches(path, g.cfg.loopback_device);
}

static void on_raw_mouse(HANDLE device, const RAWMOUSE *m, unsigned source)
{
    if (!g.armed || !g.cfg.relay_mouse || !(input_source_mask() & source))
        return;
    if (source == HIDWAY_SOURCE_TOUCHPAD && (m->usFlags & MOUSE_MOVE_ABSOLUTE)) {
        int slot = absolute_slot(device);
        int w = (m->usFlags & MOUSE_VIRTUAL_DESKTOP) ? GetSystemMetrics(SM_CXVIRTUALSCREEN) : GetSystemMetrics(SM_CXSCREEN);
        int h = (m->usFlags & MOUSE_VIRTUAL_DESKTOP) ? GetSystemMetrics(SM_CYVIRTUALSCREEN) : GetSystemMetrics(SM_CYSCREEN);
        g.cum_x += hidway_absolute_axis_delta(m->lLastX, w, &g.absolute[slot].x, &g.absolute[slot].x_valid);
        g.cum_y += hidway_absolute_axis_delta(m->lLastY, h, &g.absolute[slot].y, &g.absolute[slot].y_valid);
    } else if (!(m->usFlags & MOUSE_MOVE_ABSOLUTE)) {
        g.cum_x += m->lLastX;
        g.cum_y += m->lLastY;
    }
    USHORT bf = m->usButtonFlags;
    mouse_edge(source,bf, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, BTN_L, "Left click");
    mouse_edge(source,bf, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, BTN_R, "Right click");
    mouse_edge(source,bf, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, BTN_M, "Middle click");
    mouse_edge(source,bf, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, BTN_X1, "Mouse back");
    mouse_edge(source,bf, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, BTN_X2, "Mouse forward");
    if (bf & RI_MOUSE_WHEEL) {
        g.wheel_rem_v += (SHORT)m->usButtonData;
        g.cum_wheel = (int16_t)(g.cum_wheel + g.wheel_rem_v / WHEEL_DELTA);
        g.wheel_rem_v %= WHEEL_DELTA;
    }
    if (bf & RI_MOUSE_HWHEEL) {
        g.wheel_rem_h += (SHORT)m->usButtonData;
        g.cum_pan = (int16_t)(g.cum_pan + g.wheel_rem_h / WHEEL_DELTA);
        g.wheel_rem_h %= WHEEL_DELTA;
    }
}

static void on_capture_mouse(UINT msg, LPARAM data)
{
    if (!g.armed || !g.cfg.relay_mouse || !g.cfg.source_virtual)
        return;
    switch (msg) {
    case WM_MOUSEMOVE:
        g.cum_x += (SHORT)LOWORD(data);
        g.cum_y += (SHORT)HIWORD(data);
        break;
    case WM_LBUTTONDOWN: mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_DOWN, 0, BTN_L, "Left click"); break;
    case WM_LBUTTONUP: mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_LEFT_BUTTON_UP, 0, RI_MOUSE_LEFT_BUTTON_UP, BTN_L, "Left click"); break;
    case WM_RBUTTONDOWN: mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN, 0, BTN_R, "Right click"); break;
    case WM_RBUTTONUP: mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_RIGHT_BUTTON_UP, 0, RI_MOUSE_RIGHT_BUTTON_UP, BTN_R, "Right click"); break;
    case WM_MBUTTONDOWN: mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_DOWN, 0, BTN_M, "Middle click"); break;
    case WM_MBUTTONUP: mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_MIDDLE_BUTTON_UP, 0, RI_MOUSE_MIDDLE_BUTTON_UP, BTN_M, "Middle click"); break;
    case WM_XBUTTONDOWN:
        if (HIWORD((DWORD)data) == XBUTTON1) mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_DOWN, 0, BTN_X1, "Mouse back");
        else mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_DOWN, 0, BTN_X2, "Mouse forward");
        break;
    case WM_XBUTTONUP:
        if (HIWORD((DWORD)data) == XBUTTON1) mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_BUTTON_4_UP, 0, RI_MOUSE_BUTTON_4_UP, BTN_X1, "Mouse back");
        else mouse_edge(HIDWAY_SOURCE_VIRTUAL,RI_MOUSE_BUTTON_5_UP, 0, RI_MOUSE_BUTTON_5_UP, BTN_X2, "Mouse forward");
        break;
    case WM_MOUSEWHEEL:
        g.wheel_rem_v += (SHORT)HIWORD((DWORD)data);
        g.cum_wheel = (int16_t)(g.cum_wheel + g.wheel_rem_v / WHEEL_DELTA);
        g.wheel_rem_v %= WHEEL_DELTA;
        break;
    case WM_MOUSEHWHEEL:
        g.wheel_rem_h += (SHORT)HIWORD((DWORD)data);
        g.cum_pan = (int16_t)(g.cum_pan + g.wheel_rem_h / WHEEL_DELTA);
        g.wheel_rem_h %= WHEEL_DELTA;
        break;
    }
}

static void on_raw_input(HRAWINPUT h)
{
    UINT size = 0;
    if (GetRawInputData(h, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
        return;
    BYTE stack[256];
    BYTE *buf = size <= sizeof stack ? stack : (BYTE *)malloc(size);
    if (!buf)
        return;
    if (GetRawInputData(h, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size) {
        RAWINPUT *ri = (RAWINPUT *)buf;
        if (ri->header.dwType == RIM_TYPEMOUSE && !raw_device_is_loopback(ri->header.hDevice)) {
            int absolute = (ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
            unsigned source = hidway_mouse_source(ri->header.hDevice != NULL, absolute != 0);
            if(!ri->header.hDevice)source=HIDWAY_SOURCE_TOUCHPAD;
            if (ri->header.hDevice) {
                int slot = absolute_slot(ri->header.hDevice);
                if (absolute)
                    g.absolute[slot].touchpad = 1;
                else if (g.absolute[slot].touchpad)
                    source = HIDWAY_SOURCE_TOUCHPAD;
            }
            /* Raw Input owns local-device reports; injected virtual events
             * have one authoritative feed, the LL hook. A null handle alone
             * is not evidence of injection (Precision Touchpads use it). */
            on_raw_mouse(ri->header.hDevice, &ri->data.mouse, source);
        }
    }
    if (buf != stack)
        free(buf);
}

static void drain_capture(void)
{
    if(InterlockedCompareExchange(&capture_fault,0,0)) {
        if(g.armed){set_armed(0);history_add('w',"Input capture queue failed - released");}
        capture_queue_clear();return;
    }
    capture_event_t e;
    for(unsigned n=0;n<128;n++) {
        if(!capture_dequeue(&e))return;
        if(!g.armed||e.generation!=g.generation)continue;
        if(GetTickCount()-e.created_at>=HIDWAY_EVENT_MAX_AGE_MS) {
            set_armed(0);history_add('w',"Captured input expired - released");capture_queue_clear();return;
        }
        if(e.kind==WM_CAPTURE_KEY)on_capture_key((UINT)e.w,e.l);
        else if(e.kind==WM_CAPTURE_MOUSE)on_capture_mouse((UINT)e.w,e.l);
    }
    if(!g.preview&&!PostMessageA(g.badge,WM_CAPTURE_READY,0,0))InterlockedExchange(&capture_fault,1);
}

static LRESULT CALLBACK badge_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CAPTURE_READY:drain_capture();return 0;
    case WM_INPUT:
        on_raw_input((HRAWINPUT)lp);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && g.armed && g.exclusive_capture && !g.preview)
            focus_lost((HWND)lp);
        break;
    case WM_DISPLAYCHANGE:
        if (g.armed && !g.preview) {
            set_armed(0);
            history_add('w', "Display changed - auto-disarmed");
        }
        badge_layout();
        return 0;
    case WM_SETCURSOR: /* hidden only when the armed badge owns the cursor */
        SetCursor(g.armed && g.exclusive_capture ? NULL : LoadCursor(NULL, g.badge_drag ? IDC_SIZEALL : IDC_HAND));
        return TRUE;

    /* Disarmed: a button that never takes the focus (so it can sit over a
     * remote-desktop window). Click to arm, drag to move. */
    case WM_MOUSEACTIVATE:
        return g.armed ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        if (!g.armed) {
            SetCapture(hwnd);
            g.badge_pressed = 1;
            g.badge_drag = 0;
            GetCursorPos(&g.drag_pt);
            GetWindowRect(hwnd, &g.drag_rc);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (!g.armed) {
            if (!g.badge_hover) {
                TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
                g.badge_hover = 1;
                badge_refresh();
            }
            if (g.badge_pressed) {
                POINT p;
                GetCursorPos(&p);
                int dx = p.x - g.drag_pt.x, dy = p.y - g.drag_pt.y;
                if (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG))
                    g.badge_drag = 1;
                if (g.badge_drag)
                    SetWindowPos(hwnd, HWND_TOPMOST, g.drag_rc.left + dx, g.drag_rc.top + dy, 0, 0,
                                 SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        g.badge_hover = 0;
        badge_refresh();
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED: {
        int pressed = g.badge_pressed, dragged = g.badge_drag;
        g.badge_pressed = g.badge_drag = 0;
        if (msg == WM_LBUTTONUP && pressed)
            ReleaseCapture();
        if (dragged)
            badge_remember_position();
        else if (msg == WM_LBUTTONUP && pressed && !g.armed)
            set_armed(1);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        badge_paint(hwnd);
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        on_badge_keydown(wp);
        return 0;
    /* The badge itself never acts on input: no menus, no closing, no
     * application commands from mouse side buttons. */
    case WM_KEYUP: case WM_SYSKEYUP:
    case WM_CHAR: case WM_SYSCHAR: case WM_SYSCOMMAND: case WM_CLOSE: case WM_DPICHANGED:
        return 0;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: case WM_APPCOMMAND:
        return TRUE;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void badge_create(void)
{
    g.badge = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, "HIDwayBadge", "HIDway",
                              WS_POPUP, 0, 0, 10, 10, NULL, NULL, g.inst, NULL);
    cap_badge = g.badge;
    g.badge_dpi = 96;
    g.badge_alpha = 0;
    badge_set_alpha(150);
    int corner = HW_DWMWCP_ROUND;
    DwmSetWindowAttribute(g.badge, HW_DWMWA_CORNER, &corner, sizeof corner);
    g.badge_border = C_CARD_BD;
    DwmSetWindowAttribute(g.badge, HW_DWMWA_BORDER, &g.badge_border, sizeof g.badge_border);
    badge_layout();
}

/* ======================================================================== */
/*  Layout                                                                  */
/* ======================================================================== */

static RECT R(int l, int t, int r, int b)
{
    RECT x = {S(l), S(t), S(r), S(b)};
    return x;
}

static void compute_layout(void)
{
    layout_t *L = &g.L;
    const int W = 520, P = 18, XL = P + 14, XR = W - P - 14;

    L->W = S(W);
    L->mark = R(P, 18, P + 30, 48);
    L->title_x = S(P + 42);
    L->title_y = S(15);
    L->sub_y = S(38);
    L->pill = R(W - P - 140, 20, W - P, 46);

    /* CONNECTION: title row + four stat tiles */
    L->stats = R(P, 64, W - P, 164);
    int tile_w = (XR - XL - 3 * 8) / 4;
    for (int i = 0; i < 4; i++) {
        int x = XL + i * (tile_w + 8);
        L->tiles[i] = R(x, 100, x + tile_w, 150);
    }

    /* LIVE INPUT */
    L->live = R(P, 176, W - P, 272);

    /* RECENT INPUT */
    L->recent = R(P, 284, W - P, 424);
    L->clear = R(XR - 64, 290, XR, 314);
    L->list = R(XL - 2, 322, XR + 2, 412);

    /* SETTINGS */
    L->settings = R(P, 436, W - P, 628);
    L->save = R(XR - 64, 442, XR, 466);
    L->label_y = S(474);
    const int fw[4] = {226, 72, 66, 72};
    int x = XL - 2;
    for (int i = 0; i < 4; i++) {
        L->frame[i] = R(x, 492, x + fw[i], 522);
        x += fw[i] + 8;
    }
    L->cb[0] = R(XL - 2, 534, XL + 108, 556);
    L->cb[1] = R(XL + 116, 534, XL + 210, 556);
    L->cb[2] = R(XL + 218, 534, XL + 312, 556);
    L->src[0] = R(XL + 58, 562, XL + 154, 584);
    L->src[1] = R(XL + 160, 562, XL + 260, 584);
    L->src[2] = R(XL + 266, 562, XR, 584);
    L->hk_cy = S(606);

    /* action bar */
    L->arm = R(P, 642, W - P - 8 - 150, 688);
    L->panic = R(W - P - 150, 642, W - P, 688);

    L->H = S(706);
    L->band_bottom = S(274);
}

static void place(HWND h, RECT r)
{
    SetWindowPos(h, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void apply_layout(void)
{
    layout_t *L = &g.L;
    RECT lr = L->list;
    InflateRect(&lr, -S(4), -S(4));
    place(g.history, lr);
    SendMessageA(g.history, LB_SETITEMHEIGHT, 0, S(21));
    place(g.clear, L->clear);
    place(g.save, L->save);
    place(g.options_button,R(300,442,414,466));
    place(g.arm, L->arm);
    place(g.panic, L->panic);
    for (int i = 0; i < 3; i++)
        place(g.cb[i], L->cb[i]);
    for (int i = 0; i < 3; i++)
        place(g.src[i], L->src[i]);

    HDC dc = GetDC(g.hwnd);
    int eh = font_h(dc, g.f_body) + S(2);
    ReleaseDC(g.hwnd, dc);
    for (int i = 0; i < 4; i++) {
        RECT f = L->frame[i];
        RECT e = {f.left + S(9), f.top + ((f.bottom - f.top) - eh) / 2, f.right - S(9), 0};
        e.bottom = e.top + eh;
        place(g.edit[i], e);
        SendMessageA(g.edit[i], WM_SETFONT, (WPARAM)g.f_body, TRUE);
        SendMessageA(g.edit[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    }
}

static void invalidate_readouts(void)
{
    if (!g.hwnd)
        return;
    RECT rc = {0, 0, g.L.W, g.L.band_bottom};
    InvalidateRect(g.hwnd, &rc, FALSE);
}

static void invalidate_fields(void)
{
    RECT rc = {g.L.frame[0].left - S(2), g.L.frame[0].top - S(2),
               g.L.frame[3].right + S(2), g.L.frame[3].bottom + S(2)};
    InvalidateRect(g.hwnd, &rc, FALSE);
}

/* ======================================================================== */
/*  Fonts                                                                   */
/* ======================================================================== */

static HFONT mkfont(int tenths_px, int weight, const char *face)
{
    return CreateFontA(-MulDiv(tenths_px, (int)g.dpi, 960), 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, face);
}

static void destroy_fonts(void)
{
    HFONT *fs[] = {&g.f_title, &g.f_sub, &g.f_section, &g.f_label, &g.f_body, &g.f_pill,
                   &g.f_tile, &g.f_mono, &g.f_mono_s, &g.f_btn, &g.f_btn_big, &g.f_chip};
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++)
        if (*fs[i]) {
            DeleteObject(*fs[i]);
            *fs[i] = NULL;
        }
}

static void create_fonts(void)
{
    const char *disp = "Segoe UI Variable Display", *txt = "Segoe UI Variable Text", *mono = "Cascadia Mono";
    destroy_fonts();
    g.f_title = mkfont(195, FW_SEMIBOLD, disp);
    g.f_sub = mkfont(120, FW_NORMAL, txt);
    g.f_section = mkfont(108, FW_SEMIBOLD, txt);
    g.f_label = mkfont(120, FW_NORMAL, txt);
    g.f_body = mkfont(130, FW_NORMAL, txt);
    g.f_pill = mkfont(118, FW_SEMIBOLD, txt);
    g.f_tile = mkfont(170, FW_SEMIBOLD, mono);
    g.f_mono = mkfont(120, FW_NORMAL, mono);
    g.f_mono_s = mkfont(115, FW_NORMAL, mono);
    g.f_btn = mkfont(125, FW_SEMIBOLD, txt);
    g.f_btn_big = mkfont(145, FW_SEMIBOLD, disp);
    g.f_chip = mkfont(115, FW_SEMIBOLD, txt);
}

static void load_mark_icon(void)
{
    if (g.mark_icon)
        DestroyIcon(g.mark_icon);
    int sz = g.L.mark.right - g.L.mark.left;
    g.mark_icon = (HICON)LoadImageA(g.inst, MAKEINTRESOURCEA(1), IMAGE_ICON, sz, sz, 0);
}

/* ======================================================================== */
/*  Painting                                                                */
/* ======================================================================== */

static void card(HDC dc, RECT r, const char *title)
{
    round_rect(dc, r, S(10), C_CARD, C_CARD_BD);
    tracked(dc, g.f_section, C_TEXT3, r.left + S(14), r.top + S(12), title);
}

static int title_cy(RECT card_rect)
{
    return card_rect.top + S(12) + S(7);
}

static void paint_header(HDC dc)
{
    layout_t *L = &g.L;
    if (g.mark_icon)
        DrawIconEx(dc, L->mark.left, L->mark.top, g.mark_icon, L->mark.right - L->mark.left,
                   L->mark.bottom - L->mark.top, 0, NULL, DI_NORMAL);
    text_at(dc, g.f_title, C_TEXT, L->title_x, L->title_y, "HIDway");
    /* the version on the title's baseline */
    text_at(dc, g.f_label, C_TEXT3, L->title_x + text_w(dc, g.f_title, "HIDway") + S(8),
            L->title_y + font_ascent(dc, g.f_title) - font_ascent(dc, g.f_label), HIDWAY_VERSION);
    text_at(dc, g.f_sub, C_TEXT3, L->title_x, L->sub_y, "Remote keyboard & mouse over real USB HID");

    RECT p = L->pill;
    COLORREF fill = g.armed ? C_LIVE_F : C_FIELD;
    COLORREF bd = g.armed ? C_LIVE_BD : C_FIELD_BD;
    COLORREF dc_ = g.armed ? C_LIVE : C_TEXT3;
    COLORREF tx = g.armed ? C_LIVE_TX : C_TEXT2;
    char label[32] = "DISARMED";
    if (g.armed) { /* with how long it has been armed */
        unsigned s = (timeGetTime() - g.armed_at_ms) / 1000;
        if (s < 3600)
            snprintf(label, sizeof label, "ARMED %02u:%02u", s / 60, s % 60);
        else
            snprintf(label, sizeof label, "ARMED %u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
    }
    round_rect(dc, p, (p.bottom - p.top) / 2, fill, bd);

    SetTextCharacterExtra(dc, S(1));
    int tw = text_w(dc, g.f_pill, label);
    SetTextCharacterExtra(dc, 0);
    int dd = S(8), gap = S(8);
    int gx = p.left + ((p.right - p.left) - (dd + gap + tw)) / 2;
    int cy = (p.top + p.bottom) / 2;
    dot(dc, gx + dd / 2, cy, dd / 2, dc_);
    SetTextCharacterExtra(dc, S(1));
    text_vc(dc, g.f_pill, tx, gx + dd + gap, cy, label);
    SetTextCharacterExtra(dc, 0);
}

static void paint_stats(HDC dc)
{
    layout_t *L = &g.L;
    card(dc, L->stats, "CONNECTION");

    /* end-to-end encryption badge next to the title */
    int bx = L->stats.left + S(14) + tracked_w(dc, g.f_section, "CONNECTION") + S(10);
    if (g.key_error)
        chip(dc, bx, title_cy(L->stats), S(18), "KEY ERROR", C_DNG, C_DNG_BD, C_DNG_TX);
    else if (g.crypto_on)
        chip(dc, bx, title_cy(L->stats), S(18), "E2E", C_LIVE_F, C_LIVE_BD, C_LIVE_TX);
    else
        chip(dc, bx, title_cy(L->stats), S(18), "E2E OFF", C_FIELD, C_FIELD_BD, C_TEXT3);

    /* status on the right of the title row */
    COLORREF sc;
    const char *st;
    if (g.key_error) { sc = C_WARN; st = "invalid key in hidway.ini"; }
    else if (!g.net_ok && !g.preview) { sc = C_WARN; st = "socket error"; }
    else if (!g.armed) {
        if (link_recent()) { sc = C_LIVE; st = "relay reachable"; }
        else if (g.ever_status || g.probes_unanswered > 5) { sc = C_WARN; st = "no reply from relay"; }
        else { sc = C_TEXT3; st = "probing relay"; }
    } else if (!g.status_seen) { sc = C_AMBER; st = "waiting for relay"; }
    else if (link_recent()) { sc = C_LIVE; st = (g.pi_flags & HIDWAY_STATUS_SERIAL_OPEN) ? "linked · serial open" : "linked · no serial"; }
    else { sc = C_WARN; st = "link lost"; }

    char addr[160];
    snprintf(addr, sizeof addr, "%s:%d", g.cfg.target, g.cfg.port);
    int cy = title_cy(L->stats);
    int right = L->stats.right - S(14);
    int aw = text_w(dc, g.f_mono_s, addr);
    text_vc(dc, g.f_mono_s, C_TEXT3, right - aw, cy, addr);
    int sepw = text_w(dc, g.f_label, "  ·  ");
    text_vc(dc, g.f_label, C_TEXT3, right - aw - sepw, cy, "  ·  ");
    int sw = text_w(dc, g.f_label, st);
    int sx = right - aw - sepw - sw;
    text_vc(dc, g.f_label, sc == C_TEXT3 ? C_TEXT2 : sc, sx, cy, st);
    dot(dc, sx - S(9), cy, S(3), sc);

    /* tiles */
    char v[4][32];
    int live[4] = {0, 0, 0, 0};
    if ((g.armed && g.status_seen) || (!g.armed && link_recent())) {
        snprintf(v[0], 32, "%d ms", g.rtt_ms);
        live[0] = 1;
    } else strcpy(v[0], "—");
    if (g.armed && g.status_seen) {
        double tot = (double)g.pi_frames_ok + g.pi_gaps;
        snprintf(v[1], 32, "%.1f %%", tot > 0 ? 100.0 * g.pi_gaps / tot : 0.0);
        live[1] = 1;
    } else strcpy(v[1], "—");
    if (g.armed) { snprintf(v[2], 32, "%u/s", g.pps); live[2] = 1; } else strcpy(v[2], "—");

    /* TARGET PC: as the Pico sees it over USB, reported through the relay */
    int ts = target_state();
    COLORREF tc = C_TEXT2;
    if (ts < 0) { strcpy(v[3], "—"); tc = C_TEXT3; }
    else if (waking()) { strcpy(v[3], "Waking"); tc = C_AMBER; }
    else if (ts == HIDWAY_TARGET_ON) { strcpy(v[3], "On"); tc = C_LIVE_TX; }
    else if (ts == HIDWAY_TARGET_ASLEEP) { strcpy(v[3], "Asleep"); tc = C_AMBER; }
    else if (ts == HIDWAY_TARGET_OFF) strcpy(v[3], "Off");
    else strcpy(v[3], "Off?"); /* no report: Pico without power (PC off) or unplugged */

    static const char *names[4] = {"RTT", "LOSS", "RATE", "PC"};
    for (int i = 0; i < 4; i++) {
        RECT t = L->tiles[i];
        int hot = i == 3 && g.wake_hot && wake_offered();
        round_rect(dc, t, S(8), hot ? C_SEC_H : C_TILE, hot ? C_FOCUS_BD : C_TILE);
        tracked(dc, g.f_section, C_TEXT3, t.left + S(11), t.top + S(8), names[i]);
        text_at(dc, g.f_tile, i == 3 ? tc : live[i] ? C_TEXT : C_TEXT3, t.left + S(11), t.top + S(23), v[i]);
    }
    /* A click on the tile wakes the target; the chip says so. */
    if (wake_offered() && !waking()) {
        RECT t = L->tiles[3];
        int cw = text_w(dc, g.f_chip, "WAKE") + 2 * S(8);
        chip(dc, t.right - S(8) - cw, t.top + S(15), S(18), "WAKE", C_LIVE_F, C_LIVE_BD, C_LIVE_TX);
    }
}

static void paint_live(HDC dc)
{
    layout_t *L = &g.L;
    RECT c = L->live;
    card(dc, c, "LIVE INPUT");

    const char *mode = g.cfg.relay_keyboard && g.cfg.relay_mouse ? "keyboard + mouse"
                     : g.cfg.relay_keyboard ? "keyboard only"
                     : g.cfg.relay_mouse ? "mouse only" : "nothing relayed";
    char mode_text[80];uint32_t limited=0;
    for(unsigned o=0;o<3;o++)for(unsigned i=0;i<5;i++)limited+=g.pulse[o][i].limited;
    if(g.armed&&(limited||g.cfg.repeat_enabled)) {
        snprintf(mode_text,sizeof mode_text,"%s  |  limited %lu",g.cfg.repeat_enabled?"repeat ON":"click pacing",(unsigned long)limited);
        mode=mode_text;
    }
    int mw = text_w(dc, g.f_label, mode);
    text_vc(dc, g.f_label, C_TEXT3, c.right - S(14) - mw, title_cy(c), mode);

    if (!g.armed) {
        char hk[48], hint[96];
        hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
        snprintf(hint, sizeof hint, "Press ARM or %s to start relaying", hk);
        RECT a = {c.left, c.top + S(36), c.right, c.top + S(58)};
        RECT b = {c.left, c.top + S(58), c.right, c.top + S(80)};
        text_in(dc, g.f_body, C_TEXT2, a, DT_CENTER | DT_VCENTER | DT_SINGLELINE, "Input capture is off");
        text_in(dc, g.f_label, C_TEXT3, b, DT_CENTER | DT_VCENTER | DT_SINGLELINE, hint);
        return;
    }

    int lx = c.left + S(14), cx0 = c.left + S(70), maxx = c.right - S(14);
    int ch = S(22), gap = S(6);

    /* keys row */
    int cy = c.top + S(50);
    text_vc(dc, g.f_label, C_TEXT3, lx, cy, "Keys");
    int x = cx0, shown = 0, total = 0;
    const char *names[24];
    char tmp[24][8];
    int n = 0;
    for (int i = 0; i < 8 && n < 24; i++)
        if (g.modifiers & MODS[i].bit)
            names[n++] = MODS[i].name;
    for (unsigned u = HIDWAY_KEY_USAGE_MIN; u <= HIDWAY_KEY_USAGE_MAX && n < 24; u++)
        if (hidway_bitmap_test(g.keys, (uint8_t)u)) {
            names[n] = hidway_usage_name((uint8_t)u, tmp[n]);
            n++;
        }
    total = n;
    for (int i = 0; i < n; i++) {
        int w = text_w(dc, g.f_chip, names[i]) + 2 * S(8);
        if (w < ch) w = ch;
        if (x + w > maxx - S(40) && i < n - 1)
            break;
        x += chip(dc, x, cy, ch, names[i], C_CHIP, C_CHIP_BD, C_TEXT) + gap;
        shown++;
    }
    if (shown < total) {
        char more[8];
        snprintf(more, sizeof more, "+%d", total - shown);
        chip(dc, x, cy, ch, more, C_FIELD, C_FIELD_BD, C_TEXT2);
    }
    if (total == 0)
        text_vc(dc, g.f_label, C_TEXT3, cx0, cy, "none held");

    /* mouse row */
    cy = c.top + S(78);
    text_vc(dc, g.f_label, C_TEXT3, lx, cy, "Mouse");
    static const struct { uint8_t bit; const char *name; } BTNS[] = {
        {BTN_L, "Left"}, {BTN_R, "Right"}, {BTN_M, "Middle"}, {BTN_X1, "Back"}, {BTN_X2, "Fwd"}};
    x = cx0;
    for (int i = 0; i < 5; i++)
        if (g.buttons & BTNS[i].bit)
            x += chip(dc, x, cy, ch, BTNS[i].name, C_CHIP, C_CHIP_BD, C_TEXT) + gap;
    char mv[64];
    snprintf(mv, sizeof mv, "dx %+ld   dy %+ld   wheel %+d", (long)g.show_dx, (long)g.show_dy, g.show_dw);
    text_vc(dc, g.f_mono, C_TEXT2, x == cx0 ? cx0 : x + S(4), cy, mv);
}

static void paint_recent(HDC dc)
{
    layout_t *L = &g.L;
    card(dc, L->recent, "RECENT INPUT");
    int tw = tracked_w(dc, g.f_section, "RECENT INPUT");
    text_vc(dc, g.f_label, C_TEXT3, L->recent.left + S(14) + tw + S(10), title_cy(L->recent), "memory only");
    round_rect(dc, L->list, S(7), C_FIELD, C_FIELD_BD);
}

static void paint_settings(HDC dc)
{
    layout_t *L = &g.L;
    card(dc, L->settings, "SETTINGS");

    static const char *labels[4] = {"Relay address", "Port", "Rate (Hz)", "Disarm (ms)"};
    for (int i = 0; i < 4; i++) {
        text_at(dc, g.f_label, C_TEXT2, L->frame[i].left + S(2), L->label_y, labels[i]);
        int focused = g.focus_id == ID_TARGET + i;
        round_rect(dc, L->frame[i], S(7), C_FIELD, focused ? C_FOCUS_BD : C_FIELD_BD);
    }

    text_vc(dc, g.f_label, C_TEXT3, L->settings.left + S(14),
            (L->src[0].top + L->src[0].bottom) / 2, "Sources");

    /* hotkeys as keycaps */
    int x = L->frame[0].left + S(2), cy = L->hk_cy, ch = S(20), gap = S(4);
    if (!g.toggle_hk_ok) {
        char hk[48], msg[128];
        hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
        snprintf(msg, sizeof msg, "Toggle hotkey %s is in use — set toggle_hotkey in hidway.ini", hk);
        text_vc(dc, g.f_label, C_WARN, x, cy, msg);
        return;
    }
    const char *groups[2] = {g.cfg.toggle.text, g.cfg.panic.text};
    const char *gl[2] = {"Toggle", "Panic"};
    for (int k = 0; k < 2; k++) {
        text_vc(dc, g.f_label, C_TEXT3, x, cy, gl[k]);
        x += text_w(dc, g.f_label, gl[k]) + S(8);
        char tok[6][12];
        int n = hotkey_tokens(groups[k], tok);
        for (int i = 0; i < n; i++)
            x += chip(dc, x, cy, ch, tok[i], C_CHIP, C_CHIP_BD, C_TEXT2) + gap;
        x += S(18);
    }
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(hwnd, &ps);
    RECT cr;
    GetClientRect(hwnd, &cr);
    if (cr.right <= 0 || cr.bottom <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ obmp = SelectObject(dc, bmp);

    fill_solid(dc, cr, C_BG);
    RECT tmp;
    RECT band = {0, 0, cr.right, g.L.band_bottom};
    if (IntersectRect(&tmp, &ps.rcPaint, &band)) {
        paint_header(dc);
        paint_stats(dc);
        paint_live(dc);
    }
    if (IntersectRect(&tmp, &ps.rcPaint, &g.L.recent))
        paint_recent(dc);
    if (IntersectRect(&tmp, &ps.rcPaint, &g.L.settings))
        paint_settings(dc);

    BitBlt(wdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
           ps.rcPaint.bottom - ps.rcPaint.top, dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    SelectObject(dc, obmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

/* ---- owner-drawn controls ---------------------------------------------- */

static int is_checkbox(int id)
{
    return id == ID_CB_KBD || id == ID_CB_MOUSE || id == ID_CB_HIST ||
           id == ID_SRC_PHYSICAL || id == ID_SRC_TOUCHPAD || id == ID_SRC_VIRTUAL || id==ID_REPEAT;
}

static int cb_checked(int id)
{
    if(id==ID_REPEAT)return g.options_repeat;
    if(g.cfg.loopback_mode&&(id==ID_SRC_PHYSICAL||id==ID_SRC_TOUCHPAD))return 0;
    if (id == ID_CB_KBD) return g.cfg.relay_keyboard;
    if (id == ID_CB_MOUSE) return g.cfg.relay_mouse;
    if (id == ID_CB_HIST) return g.cfg.history_enabled;
    if (id == ID_SRC_PHYSICAL) return g.cfg.source_physical;
    if (id == ID_SRC_TOUCHPAD) return g.cfg.source_touchpad;
    return g.cfg.source_virtual;
}

static void draw_button(const DRAWITEMSTRUCT *d)
{
    RECT r = d->rcItem;
    int w = r.right - r.left, h = r.bottom - r.top;
    HDC dc = CreateCompatibleDC(d->hDC);
    HBITMAP bm = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ ob = SelectObject(dc, bm);
    RECT lr = {0, 0, w, h};
    int id = (int)d->CtlID;
    bool hot = GetPropA(d->hwndItem, "hot") != NULL;
    bool down = (d->itemState & ODS_SELECTED) != 0;
    bool focus = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);

    /* paint what is behind the control first: no default-grey corners */
    fill_solid(dc, lr, (id == ID_ARM || id == ID_PANIC) ? C_BG : C_CARD);

    char label[32];
    GetWindowTextA(d->hwndItem, label, sizeof label);

    if (is_checkbox(id)) {
        int on = cb_checked(id);
        int box = S(16);
        RECT b = {S(1), (h - box) / 2, S(1) + box, (h - box) / 2 + box};
        if (on) {
            round_rect(dc, b, S(4), hot ? C_WHITE : C_TEXT, hot ? C_WHITE : C_TEXT);
            check_mark(dc, b, C_BG);
        } else {
            round_rect(dc, b, S(4), hot ? C_SEC_H : C_FIELD, hot ? C_FOCUS_BD : C_FIELD_BD);
        }
        text_vc(dc, g.f_body, (d->itemState&ODS_DISABLED)?C_TEXT3:hot?C_WHITE:C_TEXT,
                b.right + S(8), h / 2, label);
        if (focus) {
            RECT fr = b;
            InflateRect(&fr, S(2), S(2));
            round_rect(dc, fr, S(5), C_FOCUS_BD, C_FOCUS_BD);
            InflateRect(&fr, -S(2), -S(2));
            if (on) {
                round_rect(dc, b, S(4), C_TEXT, C_TEXT);
                check_mark(dc, b, C_BG);
            } else {
                round_rect(dc, b, S(4), C_FIELD, C_FIELD_BD);
            }
        }
    } else {
        COLORREF fill, bd, tx;
        HFONT f = g.f_btn;
        int rad = S(7);
        if (id == ID_ARM) {
            fill = down ? C_BTN_P : hot ? C_BTN_H : C_BTN;
            bd = fill;
            tx = C_WHITE;
            f = g.f_btn_big;
            rad = S(10);
            strcpy(label, g.armed ? "DISARM" : "ARM");
        } else if (id == ID_PANIC) {
            fill = down ? C_DNG_P : hot ? C_DNG_H : C_DNG;
            bd = hot ? C_DNG_BD_H : C_DNG_BD;
            tx = hot ? RGB(255, 186, 178) : C_DNG_TX;
            f = g.f_btn_big;
            rad = S(10);
        } else {
            fill = down ? C_SEC : hot ? C_SEC_H : C_SEC;
            bd = hot ? C_FOCUS_BD : C_SEC_BD;
            tx = hot ? C_WHITE : C_TEXT;
            if (id == ID_SAVE && g.saved_flash)
                strcpy(label, "Saved ✓");
        }
        round_rect(dc, lr, rad, fill, bd);
        if (focus) {
            RECT fr = lr;
            InflateRect(&fr, -S(3), -S(3));
            round_rect(dc, fr, rad - S(2), C_FOCUS_BD, C_FOCUS_BD);
            InflateRect(&fr, -S(1), -S(1));
            round_rect(dc, fr, rad - S(3), fill, fill);
        }
        text_in(dc, f, tx, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE, label);
    }

    BitBlt(d->hDC, r.left, r.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bm);
    DeleteDC(dc);
}

static void draw_history_item(const DRAWITEMSTRUCT *d)
{
    if (d->itemID == (UINT)-1)
        return;
    RECT r = d->rcItem;
    fill_solid(d->hDC, r, C_FIELD);

    char buf[160];
    if (SendMessageA(d->hwndItem, LB_GETTEXT, d->itemID, (LPARAM)buf) == LB_ERR)
        return;
    char *t = buf, *lab = strchr(buf, '\x1f'), *kind = NULL;
    if (!lab)
        return;
    *lab++ = 0;
    kind = strchr(lab, '\x1f');
    if (kind)
        *kind++ = 0;
    char k = kind ? kind[0] : 'i';

    int cy = (r.top + r.bottom) / 2;
    text_vc(d->hDC, g.f_mono_s, C_TEXT3, r.left + S(10), cy, t);
    int ax = r.left + S(112), lx = r.left + S(132);
    if (k == 'd') {
        text_vc(d->hDC, g.f_body, C_TEXT, ax, cy, "↓");
        text_vc(d->hDC, g.f_body, C_TEXT, lx, cy, lab);
    } else if (k == 'u') {
        text_vc(d->hDC, g.f_body, C_TEXT3, ax, cy, "↑");
        text_vc(d->hDC, g.f_body, C_TEXT2, lx, cy, lab);
    } else if (k == 'w') {
        dot(d->hDC, ax + S(4), cy, S(3), C_WARN);
        text_vc(d->hDC, g.f_body, C_WARN, lx, cy, lab);
    } else {
        dot(d->hDC, ax + S(4), cy, S(3), C_TEXT3);
        text_vc(d->hDC, g.f_body, C_TEXT2, lx, cy, lab);
    }
}

static LRESULT CALLBACK btn_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC orig = (WNDPROC)GetPropA(h, "op");
    if (!orig)
        return DefWindowProcA(h, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; /* owner-draw paints everything; no grey flash */
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    case WM_MOUSEMOVE:
        if (!GetPropA(h, "hot")) {
            SetPropA(h, "hot", (HANDLE)1);
            TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropA(h, "hot");
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_NCDESTROY:
        RemovePropA(h, "hot");
        RemovePropA(h, "op");
        break;
    }
    return CallWindowProcA(orig, h, msg, wp, lp);
}

static LRESULT CALLBACK list_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC orig = (WNDPROC)GetPropA(h, "op");
    if (!orig)
        return DefWindowProcA(h, msg, wp, lp);
    /* Ask the original proc directly: SendMessage(LB_GETCOUNT) here would
     * re-enter this function forever. */
    int empty = (msg == WM_PAINT || msg == WM_ERASEBKGND) &&
                CallWindowProcA(orig, h, LB_GETCOUNT, 0, 0) == 0;
    if (msg == WM_ERASEBKGND && empty)
        return 1;
    if (msg == WM_PAINT && empty) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        fill_solid(dc, r, C_FIELD);
        text_in(dc, g.f_label, C_TEXT3, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                g.cfg.history_enabled ? "No input yet — arm to start relaying" : "Key history is turned off");
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemovePropA(h, "op");
    return CallWindowProcA(orig, h, msg, wp, lp);
}

static void subclass(HWND h, WNDPROC proc)
{
    WNDPROC op = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)proc);
    SetPropA(h, "op", (HANDLE)op);
}

/* ======================================================================== */
/*  Settings                                                                */
/* ======================================================================== */

static int get_int(HWND edit, int fallback)
{
    char b[32];
    GetWindowTextA(edit, b, sizeof b);
    int v = atoi(b);
    return v ? v : fallback;
}

static void apply_settings(void)
{
    if(g.armed)set_armed(0); /* release old peer before editing/opening a new peer */
    char old_target[128];
    int old_port = g.cfg.port;
    strcpy(old_target, g.cfg.target);

    GetWindowTextA(g.edit[0], g.cfg.target, sizeof g.cfg.target);
    g.cfg.port = get_int(g.edit[1], g.cfg.port);
    g.cfg.send_rate_hz = get_int(g.edit[2], g.cfg.send_rate_hz);
    g.cfg.auto_disarm_ms = get_int(g.edit[3], 0);
    if(g.cfg.port<1)g.cfg.port=1;else if(g.cfg.port>65535)g.cfg.port=65535;
    if(g.cfg.send_rate_hz<10)g.cfg.send_rate_hz=10;else if(g.cfg.send_rate_hz>1000)g.cfg.send_rate_hz=1000;
    if (g.cfg.auto_disarm_ms < 0)
        g.cfg.auto_disarm_ms = 0;

    if (!g.preview)
        hidway_config_save("hidway.ini", &g.cfg);
    if (!g.preview && (strcmp(old_target, g.cfg.target) != 0 || old_port != g.cfg.port)) {
        g.net_ok = (hidway_net_open(g.cfg.target, g.cfg.port) == 0);
        hidway_net_notify(g.hwnd, WM_NET_READ);
        g.ever_status = 0;
        memset(&g.status_order, 0, sizeof g.status_order);
        g.probes_unanswered = 0;
    }
    history_add('i', "Settings saved");
    invalidate_readouts();
}

/* ======================================================================== */
/*  Window                                                                  */
/* ======================================================================== */

static HWND mk(HWND p, const char *cls, const char *txt, DWORD style, int id)
{
    return CreateWindowExA(0, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, p,
                           (HMENU)(INT_PTR)id, g.inst, NULL);
}

static void apply_window_chrome(HWND hwnd);
static void refresh_source_controls(void)
{
    if(g.src[0])EnableWindow(g.src[0],!g.cfg.loopback_mode);
    if(g.src[1])EnableWindow(g.src[1],!g.cfg.loopback_mode);
    if(g.options_button)SetWindowTextA(g.options_button,g.cfg.loopback_mode?"Input: this PC":"Input options");
    for(int i=0;i<3;i++)if(g.src[i])InvalidateRect(g.src[i],NULL,FALSE);
}
static const int repeat_source_masks[7]={1,2,4,3,5,6,7};
static int option_speed(HWND edit)
{
    char text[32],*end;
    GetWindowTextA(edit,text,sizeof text);
    long n=strtol(text,&end,10);
    if(end==text)return 0;
    while(isspace((unsigned char)*end))end++;
    return *end||n<1||n>25?0:(int)n;
}
static HWND option_control(HWND parent,const char *cls,const char *text,DWORD style,int id,
                           int x,int y,int w,int h)
{
    HWND c=mk(parent,cls,text,style,id);
    SetWindowPos(c,NULL,S(x),S(y),S(w),S(h),SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageA(c,WM_SETFONT,(WPARAM)g.f_body,TRUE);
    return c;
}
static void option_items(HWND combo,const char *const *items,unsigned n,int selected)
{
    for(unsigned i=0;i<n;i++)SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)items[i]);
    SendMessageA(combo,CB_SETCURSEL,(WPARAM)selected,0);
    SetWindowTheme(combo,L"DarkMode_Explorer",NULL);
}
static void option_route_refresh(void)
{
    int loopback=SendMessageA(g.route,CB_GETCURSEL,0,0)==1;
    if(loopback)SendMessageA(g.repeat_source,CB_SETCURSEL,2,0);
    EnableWindow(g.repeat_source,!loopback);
    EnableWindow(g.pointer_route,loopback);
}
static LRESULT CALLBACK options_proc(HWND h,UINT msg,WPARAM w,LPARAM l)
{
    switch(msg) {
    case WM_CREATE: {
        const DWORD combo=CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL;
        option_control(h,"STATIC","Send input to",0,0,20,20,130,20);
        g.route=option_control(h,"COMBOBOX","",combo,ID_ROUTE,160,16,280,140);
        const char *routes[]={"Another PC","This PC through Pico (virtual only)"};
        option_items(g.route,routes,2,g.cfg.loopback_mode);
        option_control(h,"STATIC","This-PC pointer",0,0,20,64,136,20);
        g.pointer_route=option_control(h,"COMBOBOX","",combo,ID_POINTER,160,60,280,140);
        const char *pointers[]={"Native position (recommended)","Through Pico: relative (experimental)"};
        option_items(g.pointer_route,pointers,2,g.cfg.loopback_pointer_pico);
        g.options_repeat=g.cfg.repeat_enabled;
        g.repeat=option_control(h,"BUTTON","Hold to repeat",BS_OWNERDRAW|WS_TABSTOP,ID_REPEAT,20,110,190,26);
        subclass(g.repeat,btn_proc);
        option_control(h,"STATIC","Button",0,0,20,154,130,20);
        g.repeat_button=option_control(h,"COMBOBOX","",combo,ID_REPEAT_BUTTON,160,150,280,180);
        const char *buttons[]={"Left","Right","Middle","Back","Forward"};
        option_items(g.repeat_button,buttons,5,g.cfg.repeat_button);
        option_control(h,"STATIC","Repeat source",0,0,20,198,130,20);
        g.repeat_source=option_control(h,"COMBOBOX","",combo,ID_REPEAT_SOURCE,160,194,280,200);
        const char *sources[]={"Physical","Touchpad","Virtual / remote","Physical + touchpad",
                               "Physical + virtual","Touchpad + virtual","All selected sources"};
        int selected=0;for(int i=0;i<7;i++)if(repeat_source_masks[i]==g.cfg.repeat_sources)selected=i;
        option_items(g.repeat_source,sources,7,selected);
        option_route_refresh();
        char text[24];snprintf(text,sizeof text,"%d",g.cfg.repeat_cps);
        option_control(h,"STATIC","Repeat clicks/sec",0,0,20,242,136,20);
        g.repeat_cps=option_control(h,"EDIT",text,ES_NUMBER|WS_BORDER|WS_TABSTOP,ID_REPEAT_CPS,160,238,78,28);
        snprintf(text,sizeof text,"%d",g.cfg.virtual_click_cps);
        option_control(h,"STATIC","Virtual clicks/sec",0,0,20,286,136,20);
        g.click_cps=option_control(h,"EDIT",text,ES_NUMBER|WS_BORDER|WS_TABSTOP,ID_CLICK_CPS,160,282,78,28);
        option_control(h,"STATIC","1-25 per button",0,0,254,286,180,20);
        option_control(h,"STATIC","Repeat replaces dragging for that button and source.\r\n"
            "Virtual repeat also respects the virtual click limit.\r\n"
            "This-PC mode forwards virtual input only; local input stays local.\r\n"
            "Native pointer keeps remote positioning; clicks/keys use Pico.",0,0,20,330,420,88);
        uint32_t limited=g.last_limited;
        char info[96];snprintf(info,sizeof info,"Last ARM: %lu extra click attempts limited",(unsigned long)limited);
        option_control(h,"STATIC",info,0,0,20,426,420,24);
        HWND apply=option_control(h,"BUTTON","Apply and save",BS_OWNERDRAW|WS_TABSTOP,ID_SAVE,278,466,162,34);
        subclass(apply,btn_proc);return 0;
    }
    case WM_DRAWITEM:draw_button((const DRAWITEMSTRUCT*)l);return TRUE;
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)w,C_BG);SetTextColor((HDC)w,C_TEXT2);return (LRESULT)g.b_bg;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)w,C_FIELD);SetTextColor((HDC)w,C_TEXT);return (LRESULT)g.b_field;
    case WM_COMMAND:
        if(LOWORD(w)==ID_ROUTE&&HIWORD(w)==CBN_SELCHANGE){option_route_refresh();return 0;}
        if(LOWORD(w)==ID_REPEAT&&HIWORD(w)==BN_CLICKED) {
            g.options_repeat=!g.options_repeat;InvalidateRect(g.repeat,NULL,FALSE);return 0;
        }
        if(LOWORD(w)==ID_SAVE&&HIWORD(w)==BN_CLICKED) {
            int cps=option_speed(g.repeat_cps),limit=option_speed(g.click_cps);
            if(cps<1||cps>25||limit<1||limit>25) {
                SetWindowTextA(h,"Input options - speed must be 1 to 25");return 0;
            }
            if(g.armed)set_armed(0);
            g.cfg.loopback_mode=(int)SendMessageA(g.route,CB_GETCURSEL,0,0)==1;
            g.cfg.loopback_pointer_pico=SendMessageA(g.pointer_route,CB_GETCURSEL,0,0)==1;
            g.cfg.repeat_enabled=g.options_repeat;
            g.cfg.repeat_button=(int)SendMessageA(g.repeat_button,CB_GETCURSEL,0,0);
            int selection=(int)SendMessageA(g.repeat_source,CB_GETCURSEL,0,0);
            g.cfg.repeat_sources=repeat_source_masks[selection<0||selection>6?0:selection];
            if(g.cfg.loopback_mode)g.cfg.repeat_sources=HIDWAY_SOURCE_VIRTUAL;
            g.cfg.repeat_cps=cps;g.cfg.virtual_click_cps=limit;
            if(g.cfg.loopback_mode)g.cfg.source_virtual=1;
            if(!g.preview&&!hidway_config_save("hidway.ini",&g.cfg)) {
                SetWindowTextA(h,"Input options - could not save settings");return 0;
            }
            refresh_source_controls();
            history_add('i',"Input options saved - arm when ready");DestroyWindow(h);return 0;
        }
        break;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:
        g.options=NULL;
        if(g.hwnd){EnableWindow(g.hwnd,TRUE);SetForegroundWindow(g.hwnd);}
        return 0;
    }
    return DefWindowProcA(h,msg,w,l);
}
static void show_input_options(void)
{
    if(g.options){SetForegroundWindow(g.options);return;}
    set_armed(0);
    WNDCLASSA wc={0};wc.lpfnWndProc=options_proc;wc.hInstance=g.inst;wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=g.b_bg;wc.lpszClassName="HIDwayInputOptions";
    RegisterClassA(&wc);
    RECT parent;GetWindowRect(g.hwnd,&parent);
    RECT size={0,0,S(462),S(520)};
    AdjustWindowRectExForDpi(&size,WIN_STYLE,FALSE,0,g.dpi);
    g.options=CreateWindowExA(0,wc.lpszClassName,"Input options",WIN_STYLE,parent.left+S(24),parent.top+S(32),
        size.right-size.left,size.bottom-size.top,g.hwnd,NULL,g.inst,NULL);
    if(g.options){EnableWindow(g.hwnd,FALSE);apply_window_chrome(g.options);ShowWindow(g.options,SW_SHOW);}
}

static void create_controls(HWND h)
{
    g.history = mk(h, "LISTBOX", "",
                   LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_NOSEL | WS_VSCROLL,
                   ID_HISTORY);
    SetWindowTheme(g.history, L"DarkMode_Explorer", NULL); /* dark scrollbar */
    subclass(g.history, list_proc);

    g.clear = mk(h, "BUTTON", "Clear", BS_OWNERDRAW | WS_TABSTOP, ID_CLEAR);
    g.save = mk(h, "BUTTON", "Save", BS_OWNERDRAW | WS_TABSTOP, ID_SAVE);
    g.options_button=mk(h,"BUTTON","Input options",BS_OWNERDRAW|WS_TABSTOP,ID_OPTIONS);
    g.arm = mk(h, "BUTTON", "ARM", BS_OWNERDRAW | WS_TABSTOP, ID_ARM);
    g.panic = mk(h, "BUTTON", "PANIC", BS_OWNERDRAW | WS_TABSTOP, ID_PANIC);
    g.cb[0] = mk(h, "BUTTON", "Keyboard", BS_OWNERDRAW | WS_TABSTOP, ID_CB_KBD);
    g.cb[1] = mk(h, "BUTTON", "Mouse", BS_OWNERDRAW | WS_TABSTOP, ID_CB_MOUSE);
    g.cb[2] = mk(h, "BUTTON", "History", BS_OWNERDRAW | WS_TABSTOP, ID_CB_HIST);
    g.src[0] = mk(h, "BUTTON", "Physical", BS_OWNERDRAW | WS_TABSTOP, ID_SRC_PHYSICAL);
    g.src[1] = mk(h, "BUTTON", "Touchpad", BS_OWNERDRAW | WS_TABSTOP, ID_SRC_TOUCHPAD);
    g.src[2] = mk(h, "BUTTON", "Virtual / remote", BS_OWNERDRAW | WS_TABSTOP, ID_SRC_VIRTUAL);

    char tmp[32];
    g.edit[0] = mk(h, "EDIT", g.cfg.target, ES_AUTOHSCROLL | WS_TABSTOP, ID_TARGET);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.port);
    g.edit[1] = mk(h, "EDIT", tmp, ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_PORT);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.send_rate_hz);
    g.edit[2] = mk(h, "EDIT", tmp, ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_RATE);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.auto_disarm_ms);
    g.edit[3] = mk(h, "EDIT", tmp, ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_AUTO);

    HWND btns[] = {g.clear, g.save, g.arm, g.panic, g.cb[0], g.cb[1], g.cb[2],
                   g.src[0], g.src[1], g.src[2],g.options_button};
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++)
        subclass(btns[i], btn_proc);

    apply_layout();
    refresh_source_controls();
}

static void apply_window_chrome(HWND hwnd)
{
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    int corner = HW_DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, HW_DWMWA_CORNER, &corner, sizeof corner);
    COLORREF cap = C_BG, txt = C_TEXT2, bd = C_CARD_BD;
    DwmSetWindowAttribute(hwnd, HW_DWMWA_CAPTION, &cap, sizeof cap); /* seamless titlebar */
    DwmSetWindowAttribute(hwnd, HW_DWMWA_TEXT, &txt, sizeof txt);
    DwmSetWindowAttribute(hwnd, HW_DWMWA_BORDER, &bd, sizeof bd);
}

static void enable_dark_menus(void)
{
    HMODULE ux = LoadLibraryExA("uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux)
        return;
    typedef int(WINAPI * set_mode_t)(int);
    typedef void(WINAPI * flush_t)(void);
    set_mode_t set_mode = (set_mode_t)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    flush_t flush = (flush_t)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (set_mode)
        set_mode(2); /* force dark context menus */
    if (flush)
        flush();
}

static void resize_to_layout(HWND hwnd, const RECT *pos)
{
    RECT wr = {0, 0, g.L.W, g.L.H};
    AdjustWindowRectExForDpi(&wr, WIN_STYLE, FALSE, 0, g.dpi);
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | (pos ? 0 : SWP_NOMOVE);
    SetWindowPos(hwnd, NULL, pos ? pos->left : 0, pos ? pos->top : 0,
                 wr.right - wr.left, wr.bottom - wr.top, flags);
}

/* ---- preview / screenshot (dev aid) ------------------------------------ */

static void preview_fill(void)
{
    g.toggle_hk_ok = 1;
    g.crypto_on = 1;
    g.key_error = 0;
    g.last_status_us = hidway_now_us();
    if (g.preview == 1) { /* disarmed, target off: offers wake */
        g.ever_status = 1;
        g.rtt_ms = 91;
        g.pi_flags = HIDWAY_STATUS_SERIAL_OPEN | HIDWAY_STATUS_WAKE |
                     (HIDWAY_TARGET_OFF << HIDWAY_STATUS_TARGET_SHIFT);
    } else {
        g.armed = 1;
        g.status_seen = g.ever_status = 1;
        g.rtt_ms = 92;
        g.pps = 250;
        g.pi_frames_ok = 48213;
        g.pi_gaps = 2;
        g.pi_flags = HIDWAY_STATUS_SERIAL_OPEN | HIDWAY_STATUS_WAKE |
                     (HIDWAY_TARGET_ON << HIDWAY_STATUS_TARGET_SHIFT);
        g.armed_at_ms = timeGetTime() - 83000;
        g.modifiers = 0x02;
        hidway_bitmap_set(g.keys, 0x1A); /* W */
        hidway_bitmap_set(g.keys, 0x2C); /* Space */
        g.buttons = BTN_L;
        g.show_dx = 14;
        g.show_dy = -3;
        static const char *rows[][3] = {
            {"21:04:11.902", "Armed", "i"},
            {"21:04:12.481", "W", "d"},
            {"21:04:12.483", "Shift", "d"},
            {"21:04:12.611", "Left click", "d"},
            {"21:04:12.702", "Left click", "u"},
            {"21:04:13.020", "Space", "d"},
            {"21:04:13.388", "E", "d"},
            {"21:04:13.452", "E", "u"},
        };
        for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            char line[96];
            snprintf(line, sizeof line, "%s" "\x1f" "%s" "\x1f" "%s", rows[i][0], rows[i][1], rows[i][2]);
            SendMessageA(g.history, LB_INSERTSTRING, 0, (LPARAM)line);
        }
    }
    struct { const char *name; HWND h; } hot[] = {
        {"arm", g.arm}, {"panic", g.panic}, {"save", g.save}, {"clear", g.clear},
        {"kbd", g.cb[0]}, {"mouse", g.cb[1]}, {"hist", g.cb[2]},
    };
    for (size_t i = 0; i < sizeof hot / sizeof hot[0]; i++)
        if (strstr(g.hot_list, hot[i].name))
            SetPropA(hot[i].h, "hot", (HANDLE)1);
    if (strstr(g.hot_list, "focus"))
        g.focus_id = ID_TARGET;
}

static void save_screenshot(HWND hwnd)
{
    RECT wr, fr;
    GetWindowRect(hwnd, &wr);
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof fr)))
        fr = wr;
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    HDC sdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, w, h);
    HGDIOBJ ob = SelectObject(mdc, bm);
    PrintWindow(hwnd, mdc, 2 /* PW_RENDERFULLCONTENT */);
    SelectObject(mdc, ob);

    int cx = fr.left - wr.left, cy = fr.top - wr.top;
    int cw = fr.right - fr.left, chh = fr.bottom - fr.top;
    BITMAPINFOHEADER bi = {sizeof bi, w, -h, 1, 32, BI_RGB};
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (px && GetDIBits(sdc, bm, 0, (UINT)h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS)) {
        FILE *f = fopen(g.shot_path, "wb");
        if (f) {
            BITMAPINFOHEADER oi = {sizeof oi, cw, -chh, 1, 32, BI_RGB};
            BITMAPFILEHEADER fh = {0x4D42, (DWORD)(sizeof fh + sizeof oi + (size_t)cw * chh * 4), 0, 0,
                                   (DWORD)(sizeof fh + sizeof oi)};
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&oi, sizeof oi, 1, f);
            for (int y = 0; y < chh; y++)
                fwrite(px + ((size_t)(y + cy) * w + cx) * 4, 4, (size_t)cw, f);
            fclose(f);
        }
    }
    free(px);
    DeleteObject(bm);
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        g.dpi = GetDpiForWindow(hwnd);
        create_fonts();
        compute_layout();
        load_mark_icon();
        create_controls(hwnd);
        apply_window_chrome(hwnd);
        resize_to_layout(hwnd, NULL);
        badge_create();
        if (!g.preview) {
            WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
            g.net_ok = (hidway_net_open(g.cfg.target, g.cfg.port) == 0);
            hidway_net_notify(hwnd, WM_NET_READ);
            g.toggle_hk_ok = RegisterHotKey(hwnd, HK_TOGGLE, g.cfg.toggle.mods | MOD_NOREPEAT, g.cfg.toggle.vk) ? 1 : 0;
            if (g.cfg.panic.vk)
                RegisterHotKey(hwnd, HK_PANIC, g.cfg.panic.mods | MOD_NOREPEAT, g.cfg.panic.vk);
            tray_init(hwnd);
            SetTimer(hwnd, TIMER_PROBE, PROBE_MS, NULL);
        } else {
            preview_fill();
            if (g.shot_path[0])
                SetTimer(hwnd, TIMER_SHOT, 900, NULL);
        }
        badge_refresh(); /* preview: shown too, without taking anything over */
        SetTimer(hwnd, TIMER_UI, UI_REFRESH_MS, NULL);
        return 0;

    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        create_fonts();
        compute_layout();
        load_mark_icon();
        resize_to_layout(hwnd, (const RECT *)lp);
        apply_layout();
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_LISTBOX)
            mi->itemHeight = (UINT)S(21);
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_LISTBOX)
            draw_history_item(d);
        else
            draw_button(d);
        return TRUE;
    }

    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp, C_FIELD);
        SetTextColor((HDC)wp, C_TEXT2);
        return (LRESULT)g.b_field;
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wp, C_FIELD);
        SetTextColor((HDC)wp, C_TEXT);
        return (LRESULT)g.b_field;

    case WM_LBUTTONDOWN: {
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        for (int i = 0; i < 4; i++)
            if (PtInRect(&g.L.frame[i], pt)) {
                SetFocus(g.edit[i]);
                SendMessageA(g.edit[i], EM_SETSEL, 0, -1);
            }
        if (PtInRect(&g.L.tiles[3], pt))
            send_wake(); /* only when the tile offers it */
        return 0;
    }

    /* The PC tile is a button while it offers wake: hand cursor and hover. */
    case WM_MOUSEMOVE: {
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        int hot = PtInRect(&g.L.tiles[3], pt) && wake_offered();
        if (hot != g.wake_hot) {
            g.wake_hot = hot;
            if (hot) {
                TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&t);
            }
            invalidate_readouts();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g.wake_hot) {
            g.wake_hot = 0;
            invalidate_readouts();
        }
        return 0;
    case WM_SETCURSOR:
        if ((HWND)wp == hwnd && LOWORD(lp) == HTCLIENT && g.wake_hot && wake_offered()) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_NET_READ:
        net_drain_status();
        return 0;

    case WM_SEND_TICK:
        if((DWORD)wp==g.generation) {
            InterlockedExchange(&g.send_clock.posted,0);
            if(g.armed&&GetTickCount()-(DWORD)lp>=HIDWAY_EVENT_MAX_AGE_MS) {
                set_armed(0);history_add('w',"Sender paused - released instead of replaying old input");return 0;
            }
            net_tick();
        }
        return 0;

    case WM_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK) {
            show_main_window(1);
        } else if (lp == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            AppendMenuA(m, MF_STRING, IDM_ARM, g.armed ? "Disarm" : "Arm");
            AppendMenuA(m, MF_STRING, IDM_PANIC, "Panic - release all");
            AppendMenuA(m, MF_STRING | (wake_offered() ? 0 : MF_GRAYED), IDM_WAKE, "Wake target PC");
            AppendMenuA(m, MF_SEPARATOR, 0, NULL);
            AppendMenuA(m, MF_STRING, IDM_SHOW, IsWindowVisible(hwnd) ? "Hide window" : "Show window");
            AppendMenuA(m, MF_STRING | (g.cfg.badge ? MF_CHECKED : 0), IDM_BADGE, "Show badge (click to arm)");
            AppendMenuA(m, MF_SEPARATOR, 0, NULL);
            AppendMenuA(m, MF_STRING, IDM_QUIT, "Quit HIDway");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(m);
        }
        return 0;

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED && g.tray_shown)
            show_main_window(0); /* minimize to tray */
        return 0;

    /* Safety: anything that changes the screen or the session disarms. */
    case WM_DISPLAYCHANGE:
        if (g.armed && !g.preview) {
            set_armed(0);
            history_add('w', "Display changed - auto-disarmed");
        }
        badge_layout();
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (g.armed && (wp == WTS_SESSION_LOCK || wp == WTS_CONSOLE_DISCONNECT ||
                        wp == WTS_REMOTE_DISCONNECT || wp == WTS_SESSION_LOGOFF)) {
            set_armed(0);
            history_add('w', "Session locked - auto-disarmed");
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND && g.armed) {
            set_armed(0);
            history_add('w', "Sleep - auto-disarmed");
        }
        return TRUE;
    case WM_QUERYENDSESSION:
        set_armed(0);
        return TRUE;

    case WM_HOTKEY:
        if (wp == HK_TOGGLE)
            set_armed(!g.armed);
        else if (wp == HK_PANIC) {
            set_armed(0);
            history_add('w', "PANIC - all released");
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_ARM: set_armed(!g.armed); return 0;
        case IDM_PANIC: set_armed(0); history_add('w', "PANIC - all released"); return 0;
        case IDM_WAKE: send_wake(); return 0;
        case IDM_SHOW: show_main_window(!IsWindowVisible(hwnd)); return 0;
        case IDM_BADGE:
            g.cfg.badge = !g.cfg.badge;
            if (!g.preview)
                hidway_config_save("hidway.ini", &g.cfg);
            badge_refresh();
            return 0;
        case IDM_QUIT: DestroyWindow(hwnd); return 0;
        }
        if (LOWORD(wp) >= ID_TARGET && LOWORD(wp) <= ID_AUTO) {
            if (HIWORD(wp) == EN_SETFOCUS) {
                g.focus_id = LOWORD(wp);
                invalidate_fields();
            } else if (HIWORD(wp) == EN_KILLFOCUS) {
                g.focus_id = 0;
                invalidate_fields();
            }
            return 0;
        }
        if (HIWORD(wp) == BN_CLICKED) {
            if (g.armed && (LOWORD(wp) == ID_CB_KBD || LOWORD(wp) == ID_CB_MOUSE ||
                            LOWORD(wp) == ID_SRC_PHYSICAL || LOWORD(wp) == ID_SRC_TOUCHPAD ||
                            LOWORD(wp) == ID_SRC_VIRTUAL))
                set_armed(0); /* a capture-policy change always starts from a released state */
            switch (LOWORD(wp)) {
            case ID_OPTIONS:show_input_options();break;
            case ID_ARM: set_armed(!g.armed); break;
            case ID_PANIC: set_armed(0); history_add('w', "PANIC - all released"); break;
            case ID_CLEAR: SendMessageA(g.history, LB_RESETCONTENT, 0, 0); break;
            case ID_SAVE:
                apply_settings();
                g.saved_flash = 1;
                InvalidateRect(g.save, NULL, FALSE);
                SetTimer(hwnd, TIMER_SAVED, 1400, NULL);
                break;
            case ID_CB_KBD: g.cfg.relay_keyboard = !g.cfg.relay_keyboard; break;
            case ID_CB_MOUSE: g.cfg.relay_mouse = !g.cfg.relay_mouse; break;
            case ID_CB_HIST:
                g.cfg.history_enabled = !g.cfg.history_enabled;
                InvalidateRect(g.history, NULL, FALSE);
                break;
            case ID_SRC_PHYSICAL: g.cfg.source_physical = !g.cfg.source_physical; break;
            case ID_SRC_TOUCHPAD: g.cfg.source_touchpad = !g.cfg.source_touchpad; break;
            case ID_SRC_VIRTUAL: g.cfg.source_virtual = !g.cfg.source_virtual; break;
            }
            if (is_checkbox(LOWORD(wp))) {
                InvalidateRect((HWND)lp, NULL, FALSE);
                invalidate_readouts();
            }
        }
        return 0;

    case WM_TIMER:
        if (wp == TIMER_UI) {
            static DWORD previous_ui_tick;
            DWORD ui_now = timeGetTime();
            if (previous_ui_tick && ui_now - previous_ui_tick >= 250u)
                latency_trace("UI_GAP_MS", ui_now - previous_ui_tick, g.events.count);
            previous_ui_tick = ui_now;
            if (!g.preview) {
                g.show_dx = g.cum_x - g.disp_x;
                g.show_dy = g.cum_y - g.disp_y;
                g.show_dw = (int16_t)(g.cum_wheel - g.disp_wheel);
                g.show_dp = (int16_t)(g.cum_pan - g.disp_pan);
                g.disp_x = g.cum_x;
                g.disp_y = g.cum_y;
                g.disp_wheel = g.cum_wheel;
                g.disp_pan = g.cum_pan;
                if (g.armed)
                    capture_tick();
            } else {
                g.last_status_us = hidway_now_us(); /* preview: link always fresh */
            }
            badge_refresh();
            invalidate_readouts();
        } else if (wp == TIMER_PROBE) {
            monitor();
        } else if (wp == TIMER_SAVED) {
            KillTimer(hwnd, TIMER_SAVED);
            g.saved_flash = 0;
            InvalidateRect(g.save, NULL, FALSE);
        } else if (wp == TIMER_SHOT) {
            KillTimer(hwnd, TIMER_SHOT);
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            save_screenshot(hwnd);
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CLOSE:
        set_armed(0);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        if (!g.preview) {
            WINDOWPLACEMENT wpl = {sizeof wpl};
            if (GetWindowPlacement(hwnd, &wpl)) {
                g.cfg.window_x = wpl.rcNormalPosition.left;
                g.cfg.window_y = wpl.rcNormalPosition.top;
                hidway_config_save("hidway.ini", &g.cfg);
            }
        }
        tray_destroy();
        if (!g.preview)
            WTSUnRegisterSessionNotification(hwnd);
        if (g.badge)
            DestroyWindow(g.badge);
        if (g.bf_main) DeleteObject(g.bf_main);
        if (g.bf_hint) DeleteObject(g.bf_hint);
        KillTimer(hwnd, TIMER_UI);
        KillTimer(hwnd, TIMER_PROBE);
        UnregisterHotKey(hwnd, HK_TOGGLE);
        UnregisterHotKey(hwnd, HK_PANIC);
        hidway_net_close();
        destroy_fonts();
        if (g.mark_icon)
            DestroyIcon(g.mark_icon);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void parse_args(const char *cmd)
{
    if (strstr(cmd, "--preview-armed"))
        g.preview = 2;
    else if (strstr(cmd, "--preview"))
        g.preview = 1;
    const char *s = strstr(cmd, "--shot=");
    if (s) {
        s += 7;
        size_t n = strcspn(s, " ");
        if (n >= sizeof g.shot_path)
            n = sizeof g.shot_path - 1;
        memcpy(g.shot_path, s, n);
        g.shot_path[n] = 0;
    }
    const char *hot = strstr(cmd, "--hot=");
    if (hot) {
        hot += 6;
        size_t n = strcspn(hot, " ");
        if (n >= sizeof g.hot_list)
            n = sizeof g.hot_list - 1;
        memcpy(g.hot_list, hot, n);
        g.hot_list[n] = 0;
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g.inst = inst;
    parse_args(cmd ? cmd : "");
    HANDLE instance_guard=NULL;
    if(!g.preview) {
        instance_guard=CreateMutexW(NULL,TRUE,L"Local\\HIDwayClientInputOwner");
        if(!instance_guard)return 1;
        if(GetLastError()==ERROR_ALREADY_EXISTS) {
            HWND existing=FindWindowA("HIDwayClient",NULL);
            if(existing)ShowWindowAsync(existing,SW_RESTORE);
            CloseHandle(instance_guard);return 0;
        }
    }
    if (cmd && strstr(cmd, "--trace-latency")) {
        latency_trace_file = fopen("hidway-latency.log", "w");
        latency_trace("START", 0, 0);
    }

    hidway_config_load("hidway.ini", &g.cfg);
    if (g.preview) /* previews never show a real address */
        strcpy(g.cfg.target, "100.100.100.100");
    if (g.cfg.key_hex[0]) {
        /* A key was asked for: encrypt, or (if it is malformed) send nothing.
         * Never fall back to plaintext. */
        g.crypto_on = hidway_key_parse(g.cfg.key_hex, g.key);
        g.key_error = !g.crypto_on;
    }
    g.rtt_ms = -1;
    if (hidway_random((uint8_t *)&g.session_id, sizeof g.session_id) != 0) {
        srand(GetTickCount() ^ GetCurrentProcessId());
        g.session_id = ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 3) ^ GetTickCount();
    }

    HW_GdiplusStartupInput gsi = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&g.gdip, &gsi, NULL);
    enable_dark_menus();
    timeBeginPeriod(1);
    g.b_bg = CreateSolidBrush(C_BG);
    g.b_field = CreateSolidBrush(C_FIELD);

    HICON appicon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g.b_bg;
    wc.hIcon = appicon;
    wc.lpszClassName = "HIDwayClient";
    if (!RegisterClassA(&wc))
        return 1;
    WNDCLASSA bc = {0};
    bc.lpfnWndProc = badge_proc;
    bc.hInstance = inst;
    bc.lpszClassName = "HIDwayBadge";
    if (!RegisterClassA(&bc))
        return 1;

    g.dpi = GetDpiForSystem();
    compute_layout();
    RECT wr = {0, 0, g.L.W, g.L.H};
    AdjustWindowRectExForDpi(&wr, WIN_STYLE, FALSE, 0, g.dpi);
    int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT;
    if (g.preview && g.shot_path[0]) {
        wx = wy = -20000; /* screenshot runs render off-screen */
    } else if (!g.preview && g.cfg.window_x != INT_MIN && g.cfg.window_y != INT_MIN) {
        wx = g.cfg.window_x;
        wy = g.cfg.window_y;
    }
    HWND hwnd = CreateWindowExA(0, "HIDwayClient", "HIDway", WIN_STYLE, wx, wy,
                                wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, inst, NULL);
    if (!hwnd)
        return 1;
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)appicon);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadImageA(inst, MAKEINTRESOURCEA(1), IMAGE_ICON,
                                    GetSystemMetricsForDpi(SM_CXSMICON, g.dpi),
                                    GetSystemMetricsForDpi(SM_CYSMICON, g.dpi), 0));

    ShowWindow(hwnd, g.shot_path[0] ? SW_SHOWNOACTIVATE : show);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0) > 0) {
        if (!(g.options&&IsDialogMessageA(g.options,&m))&&!IsDialogMessageA(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    timeEndPeriod(1);
    GdiplusShutdown(g.gdip);
    hidway_wipe(g.key, sizeof g.key);
    hidway_wipe(g.cfg.key_hex, sizeof g.cfg.key_hex);
    if (latency_trace_file) fclose(latency_trace_file);
    if(instance_guard){ReleaseMutex(instance_guard);CloseHandle(instance_guard);}
    return 0;
}
