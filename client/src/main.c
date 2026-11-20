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
#include <commdlg.h>
#include <commctrl.h>

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
#include "local_pico.h"
#include "hidway_companion_win.h"
#include "protocol.h"
#include "capture_queue.h"
#include "send_clock.h"
#include "replay.h"
static struct {
    int active;hw_recording file;hidway_replay_state state;unsigned next;
    uint32_t started,last_tick;wchar_t path[2048];
} replay;
static void replay_tick(void);
static void replay_choose(void);
static void open_monitor(void);
#define RECORDING_IPC 0x48575231u

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "comctl32.lib") /* tooltips only */

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
#define TIMER_PICO  6 /* Pico-on-this-PC indication only; never on the 50 ms UI timer */
#define PICO_REFRESH_MS 2000
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
    IDM_ARM, IDM_PANIC, IDM_WAKE, IDM_SHOW, IDM_BADGE, IDM_QUIT, ID_MONITOR, ID_REPLAY, ID_LOCAL_PICO, ID_UPDATES,
    ID_COMPACT, /* new IDs go last: tests post commands by number */
    ID_TABS, ID_TAB_CONTROL, ID_TAB_CONNECTION, ID_TAB_INPUT, ID_TAB_TOOLS,
    ID_PROFILE_REMOTE, ID_PROFILE_GAMING, ID_INPUT_APPLY
};
/* Retired with the tabbed window, numbers kept: ID_OPTIONS (the Input options
 * window; a posted command now opens the Input tab), ID_ROUTE (its route combo,
 * now ID_PROFILE_REMOTE / ID_PROFILE_GAMING) and ID_COMPACT (Less / More). */

enum { TAB_CONTROL, TAB_CONNECTION, TAB_INPUT, TAB_TOOLS, TAB_COUNT };
static const char *const TAB_NAMES[TAB_COUNT] = {"Control", "Connection", "Input", "Tools"};

/* Logical (96 DPI) window size; the same on every tab. */
#define LAYOUT_W 520
#define LAYOUT_H 520

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
/* ARM while disarmed: the primary action, a restrained tint of C_LIVE. */
#define C_ARM       RGB(36, 72, 46)
#define C_ARM_H     RGB(43, 86, 55)
#define C_ARM_P     RGB(31, 60, 39)
#define C_ARM_BD    RGB(52, 112, 67)
#define C_ARM_BD_H  RGB(68, 146, 87)
#define C_ARM_TX    RGB(214, 246, 218)

typedef struct {
    int W, H;
    /* every tab: header, tab strip, action row */
    RECT mark, pill, tabs, page;
    int title_x, title_y, sub_y;
    RECT arm, panic, local_pico;
    /* Control */
    RECT stats, tiles[4], live;
    int info_cy;
    /* Connection */
    RECT settings, save, frame[4], conn_hint;
    int label_y;
    /* Input */
    RECT input, apply, profile[2], cb[2], src[3], pointer, repeat, repeat_button, repeat_source;
    RECT repeat_cps, click_cps, status; /* field frames and the status text */
    int row_route, row_relay, row_sources, row_pointer, row_button, row_source, row_click, row_limited;
    /* Tools */
    RECT tools, monitor_btn, replay_btn, updates, recent, history_cb, clear, list;
    int hk_cy;
} layout_t;

static struct {
    hidway_config_t cfg;
    int preview; /* 0 normal, 1 preview disarmed, 2 preview armed */
    char shot_path[MAX_PATH];
    char hot_list[96];
    /* preview-only: --tab=NAME (or --options for the Input tab), --dpi=N */
    int preview_tab;
    UINT preview_dpi;

    int tab;         /* TAB_*; Control at every start */
    int in_sizemove; /* the user is dragging the window */

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
    /* g.options stays NULL: the Input options window became the Input tab.
     * Kept so the message loop and set_armed() are unchanged. */
    HWND options,pointer_route,repeat,repeat_button,repeat_cps,repeat_source,click_cps;
    HWND tabs,profile_button[2],input_apply;
    HWND monitor_button,replay_button,updates_button,local_pico_button,tooltip;
    char local_pico_notice[192];
    /* Input tab: edits not applied yet (reset from g.cfg when the tab is
     * entered), and its status / error line. */
    hidway_config_t pending;
    int input_dirty, input_error;
    char input_status[160];
    /* Connection tab: why the last Save was refused or failed (empty: the hint) */
    int conn_error;
    char conn_status[160];
    /* Pico on this PC indication (read-only, GUI-thread timer) */
    unsigned pico_state;
    int pico_known, pico_pending;
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
    if(replay.active)replay_tick();else drain_capture();
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
    history_add('i', "Wake sent to the Gaming PC");
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
        snprintf(line, sizeof line, "Gaming PC: %s", names[ts & 3]);
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
    if(replay.active)return replay.state.frame;
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
    if (!g.armed) { /* a small "Arm" pill; no hint, to keep it out of the way */
        hint[0] = 0;
        (void)hint_cap;
        /* Red stays for a relay that does not answer; plain "disarmed,
         * nothing wrong" is a neutral grey (red reads as live/recording). */
        int up = link_recent();
        snprintf(st, st_cap, up ? "Arm" : "Offline");
        return up ? C_TEXT3 : C_WARN;
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
    if (on && !g.preview && !replay.active && !capture_start()) {
        history_add('w', "Could not start input capture - not armed");
        badge_refresh();
        return;
    }
    if(!on) {
        g.last_limited=0;
        for(unsigned o=0;o<3;o++)for(unsigned i=0;i<5;i++)g.last_limited+=g.pulse[o][i].limited;
    }
    g.armed = on;
    if(g.local_pico_button)EnableWindow(g.local_pico_button,!on);
    if(!on&&replay.active){replay.active=0;hw_rec_free(&replay.file);memset(&replay.state,0,sizeof replay.state);if(g.replay_button)SetWindowTextA(g.replay_button,"Replay...");}
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
    if (!g.armed||replay.active)
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
    if (!g.armed || replay.active || !g.cfg.relay_mouse || !(input_source_mask() & source))
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
    if (!g.armed || replay.active || !g.cfg.relay_mouse || !g.cfg.source_virtual)
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

/* One window size for every tab. Header, tab strip and the ARM / Pico on
 * this PC / PANIC row are the same on every tab; the page between them
 * belongs to the selected tab. */
static void compute_layout(void)
{
    layout_t *L = &g.L;
    const int W = LAYOUT_W, P = 18, XL = P + 14, XR = W - P - 14, CX = 166;

    L->W = S(W);
    L->H = S(LAYOUT_H);
    L->mark = R(P, 14, P + 28, 42);
    L->title_x = S(P + 40);
    L->title_y = S(9);
    L->sub_y = S(33);
    L->pill = R(W - P - 140, 12, W - P, 38);
    L->tabs = R(P, 56, W - P, 88);
    L->page = R(0, 96, W, 456);

    L->arm = R(P, 462, 194, 508);
    L->local_pico = R(202, 462, 344, 508);
    L->panic = R(W - P - 150, 462, W - P, 508);

    /* Control: CONNECTION (tiles, Pico, route) and LIVE INPUT. The GAMING PC
     * tile gets the remaining width so its WAKE chip sits clear of the label
     * and the value. */
    L->stats = R(P, 100, W - P, 226);
    int x = XL;
    for (int i = 0; i < 4; i++) {
        int w = i < 3 ? 96 : XR - x;
        L->tiles[i] = R(x, 132, x + w, 182);
        x += w + 8;
    }
    L->info_cy = S(205);
    L->live = R(P, 238, W - P, 334);

    /* Connection */
    L->settings = R(P, 100, W - P, 240);
    L->save = R(XR - 64, 106, XR, 130);
    L->label_y = S(140);
    const int fw[4] = {226, 72, 66, 72};
    x = XL - 2;
    for (int i = 0; i < 4; i++) {
        L->frame[i] = R(x, 158, x + fw[i], 188);
        x += fw[i] + 8;
    }
    L->conn_hint = R(XL, 198, XR, 236);

    /* Input: the former Input options window plus Keyboard/Mouse/Sources.
     * Combo rectangles include their drop-down list height. */
    L->input = R(P, 100, W - P, 450);
    L->apply = R(XR - 120, 106, XR, 130);
    L->row_route = S(152);
    L->profile[0] = R(CX, 138, 325, 166);
    L->profile[1] = R(329, 138, XR, 166);
    L->row_relay = S(183);
    L->cb[0] = R(CX, 172, CX + 96, 194);
    L->cb[1] = R(CX + 104, 172, CX + 190, 194);
    L->row_sources = S(209);
    L->src[0] = R(CX, 198, CX + 90, 220);
    L->src[1] = R(CX + 96, 198, CX + 190, 220);
    L->src[2] = R(CX + 196, 198, XR, 220);
    L->row_pointer = S(240);
    L->pointer = R(CX, 226, XR, 226 + 160);
    L->repeat = R(XL - 2, 260, XL + 190, 282);
    L->row_button = S(302);
    L->repeat_button = R(CX, 288, 300, 288 + 180);
    L->repeat_cps = R(428, 288, XR, 316);
    L->row_source = S(336);
    L->repeat_source = R(CX, 322, XR, 322 + 200);
    L->row_click = S(370);
    L->click_cps = R(CX, 356, CX + 60, 384);
    L->row_limited = S(400);
    L->status = R(XL, 412, XR, 448);

    /* Tools */
    L->tools = R(P, 100, W - P, 174);
    L->monitor_btn = R(XR - 222, 106, XR - 152, 130);
    L->replay_btn = R(XR - 146, 106, XR - 76, 130);
    L->updates = R(XR - 70, 106, XR, 130);
    L->hk_cy = S(152);
    L->recent = R(P, 186, W - P, 450);
    L->history_cb = R(XR - 160, 194, XR - 80, 216);
    L->clear = R(XR - 64, 193, XR, 217);
    L->list = R(XL - 2, 226, XR + 2, 438);
}

/* Keep the window, and above all its action row, inside the work area. */
static void keep_on_work_area(HWND hwnd)
{
    if (!hwnd || g.shot_path[0]) /* screenshot runs render off-screen on purpose */
        return;
    RECT wr;
    MONITORINFO mi = {sizeof mi};
    if (!GetWindowRect(hwnd, &wr) ||
        !GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    RECT a = mi.rcWork;
    int w = wr.right - wr.left, h = wr.bottom - wr.top, x = wr.left, y = wr.top;
    if (x + w > a.right) x = a.right - w;
    if (x < a.left) x = a.left;
    if (y + h > a.bottom) y = a.bottom - h; /* too tall: the bottom row wins */
    if (y < a.top && h <= a.bottom - a.top) y = a.top;
    if (x != wr.left || y != wr.top)
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void place(HWND h, RECT r)
{
    if (h)
        SetWindowPos(h, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

/* A borderless edit vertically centred in its painted field frame. */
static void place_field(HWND edit, RECT f, int eh)
{
    if (!edit)
        return;
    RECT e = {f.left + S(9), f.top + ((f.bottom - f.top) - eh) / 2, f.right - S(9), 0};
    e.bottom = e.top + eh;
    place(edit, e);
    SendMessageA(edit, WM_SETFONT, (WPARAM)g.f_body, TRUE);
    SendMessageA(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}

/* Controls of the other tabs are hidden: they keep their values, and hidden
 * windows are skipped by the dialog keyboard navigation. The action row and
 * the tab strip are never hidden. */
static void show_tab_controls(void)
{
    struct { HWND h; int tab; } c[] = {
        {g.edit[0], TAB_CONNECTION}, {g.edit[1], TAB_CONNECTION}, {g.edit[2], TAB_CONNECTION},
        {g.edit[3], TAB_CONNECTION}, {g.save, TAB_CONNECTION},
        {g.profile_button[0], TAB_INPUT}, {g.profile_button[1], TAB_INPUT}, {g.cb[0], TAB_INPUT},
        {g.cb[1], TAB_INPUT}, {g.src[0], TAB_INPUT}, {g.src[1], TAB_INPUT}, {g.src[2], TAB_INPUT},
        {g.pointer_route, TAB_INPUT}, {g.repeat, TAB_INPUT}, {g.repeat_button, TAB_INPUT},
        {g.repeat_cps, TAB_INPUT}, {g.repeat_source, TAB_INPUT}, {g.click_cps, TAB_INPUT},
        {g.input_apply, TAB_INPUT},
        {g.monitor_button, TAB_TOOLS}, {g.replay_button, TAB_TOOLS}, {g.updates_button, TAB_TOOLS},
        {g.cb[2], TAB_TOOLS}, {g.clear, TAB_TOOLS}, {g.history, TAB_TOOLS},
    };
    HWND focus = GetFocus();
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        if (!c[i].h)
            continue;
        int on = c[i].tab == g.tab;
        if (!on && focus && (focus == c[i].h || IsChild(c[i].h, focus))) {
            SetFocus(g.tabs ? g.tabs : g.arm); /* never leave the keyboard on a hidden control */
            focus = NULL;
        }
        ShowWindow(c[i].h, on ? SW_SHOWNA : SW_HIDE);
    }
}

static void apply_layout(void)
{
    layout_t *L = &g.L;
    HDC dc = GetDC(g.hwnd);
    int eh = font_h(dc, g.f_body) + S(2);
    ReleaseDC(g.hwnd, dc);

    place(g.tabs, L->tabs);
    place(g.arm, L->arm);
    place(g.local_pico_button, L->local_pico);
    place(g.panic, L->panic);

    place(g.save, L->save);
    for (int i = 0; i < 4; i++)
        place_field(g.edit[i], L->frame[i], eh);

    HWND combos[] = {g.pointer_route, g.repeat_button, g.repeat_source};
    for (size_t i = 0; i < sizeof combos / sizeof combos[0]; i++) {
        if (!combos[i])
            continue;
        SendMessageA(combos[i], WM_SETFONT, (WPARAM)g.f_body, FALSE);
        SendMessageA(combos[i], CB_SETITEMHEIGHT, (WPARAM)-1, S(22)); /* the closed box */
        SendMessageA(combos[i], CB_SETITEMHEIGHT, 0, S(24));          /* list items */
    }
    place(g.input_apply, L->apply);
    for (int i = 0; i < 2; i++)
        place(g.profile_button[i], L->profile[i]);
    place(g.cb[0], L->cb[0]);
    place(g.cb[1], L->cb[1]);
    for (int i = 0; i < 3; i++)
        place(g.src[i], L->src[i]);
    place(g.pointer_route, L->pointer);
    place(g.repeat, L->repeat);
    place(g.repeat_button, L->repeat_button);
    place(g.repeat_source, L->repeat_source);
    place_field(g.repeat_cps, L->repeat_cps, eh);
    place_field(g.click_cps, L->click_cps, eh);

    place(g.monitor_button, L->monitor_btn);
    place(g.replay_button, L->replay_btn);
    place(g.updates_button, L->updates);
    place(g.cb[2], L->history_cb);
    place(g.clear, L->clear);
    RECT lr = L->list;
    InflateRect(&lr, -S(4), -S(4));
    place(g.history, lr);
    if (g.history)
        SendMessageA(g.history, LB_SETITEMHEIGHT, 0, S(21));

    show_tab_controls();
}

/* The header (state pill) and the page of the selected tab; the timer calls
 * this every 50 ms. Child controls are clipped and repaint themselves. */
static void invalidate_readouts(void)
{
    if (!g.hwnd)
        return;
    RECT rc = {0, 0, g.L.W, g.L.page.bottom};
    InvalidateRect(g.hwnd, &rc, FALSE);
}

static void invalidate_fields(void)
{
    if (g.hwnd)
        InvalidateRect(g.hwnd, &g.L.page, FALSE);
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
    /* the version on the title's baseline, with room up to the state pill */
    int version_top = L->title_y + font_ascent(dc, g.f_title) - font_ascent(dc, g.f_label);
    RECT version={L->title_x + text_w(dc,g.f_title,"HIDway")+S(8),
        version_top,L->pill.left-S(10),version_top+font_h(dc,g.f_label)};
    text_in(dc,g.f_label,C_TEXT3,version,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS,HIDWAY_VERSION);
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

/* Pico on this PC, from the last read-only Raw Input inventory. */
static const char *pico_text(COLORREF *color)
{
    if (!g.pico_known) { *color = C_TEXT3; return "Checking..."; }
    if ((g.pico_state & HIDWAY_PICO_READY) == HIDWAY_PICO_READY) { *color = C_LIVE; return "On this PC"; }
    if (g.pico_state & HIDWAY_PICO_READY) { *color = C_AMBER; return "Partly detected"; }
    *color = C_TEXT3;
    return "Not on this PC";
}

static const char *route_name(int loopback)
{
    return loopback ? "Gaming PC" : "Remote PC";
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

    static const char *names[4] = {"RTT", "LOSS", "RATE", "GAMING PC"};
    for (int i = 0; i < 4; i++) {
        RECT t = L->tiles[i];
        int hot = i == 3 && g.wake_hot && wake_offered();
        round_rect(dc, t, S(8), hot ? C_SEC_H : C_TILE, hot ? C_FOCUS_BD : C_TILE);
        tracked(dc, g.f_section, C_TEXT3, t.left + S(11), t.top + S(8), names[i]);
        text_at(dc, g.f_tile, i == 3 ? tc : live[i] ? C_TEXT : C_TEXT3, t.left + S(11), t.top + S(23), v[i]);
    }
    /* A click on the tile wakes the target; the chip says so. It sits on the
     * value line at the right, below the label, clear of the longest value. */
    if (wake_offered() && !waking()) {
        RECT t = L->tiles[3];
        int cw = text_w(dc, g.f_chip, "WAKE") + 2 * S(8);
        chip(dc, t.right - S(8) - cw, t.top + S(34), S(18), "WAKE", C_LIVE_F, C_LIVE_BD, C_LIVE_TX);
    }

    /* Pico on this PC (indication only) and the route in use */
    cy = L->info_cy;
    int x = L->stats.left + S(14), fh = font_h(dc, g.f_section);
    tracked(dc, g.f_section, C_TEXT3, x, cy - fh / 2, "PICO");
    x += tracked_w(dc, g.f_section, "PICO") + S(10);
    COLORREF pc;
    const char *pt = pico_text(&pc);
    dot(dc, x + S(3), cy, S(3), pc);
    text_vc(dc, g.f_label, pc == C_TEXT3 ? C_TEXT2 : pc, x + S(12), cy, pt);
    x = L->tiles[2].left;
    tracked(dc, g.f_section, C_TEXT3, x, cy - fh / 2, "ROUTE");
    x += tracked_w(dc, g.f_section, "ROUTE") + S(10);
    text_vc(dc, g.f_label, C_TEXT, x, cy, route_name(g.cfg.loopback_mode));
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
        if(g.local_pico_notice[0]) {
            RECT note={c.left+S(14),c.top+S(35),c.right-S(14),c.bottom-S(6)};
            text_in(dc,g.f_label,C_TEXT2,note,DT_CENTER|DT_WORDBREAK,g.local_pico_notice);
            return;
        }
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

/* A painted field frame around a borderless edit. */
static void field_frame(HDC dc, RECT f, int id, int enabled)
{
    round_rect(dc, f, S(7), C_FIELD, enabled && g.focus_id == id ? C_FOCUS_BD : C_FIELD_BD);
}

static void paint_connection(HDC dc)
{
    layout_t *L = &g.L;
    card(dc, L->settings, "RELAY");
    char route[64];
    snprintf(route, sizeof route, "%s route", route_name(g.cfg.loopback_mode));
    int tx = L->settings.left + S(14) + tracked_w(dc, g.f_section, "RELAY") + S(10);
    chip(dc, tx, title_cy(L->settings), S(18), route, C_FIELD, C_FIELD_BD, C_TEXT2);

    static const char *labels[4] = {"Relay address", "Port", "Rate (Hz)", "Disarm (ms)"};
    for (int i = 0; i < 4; i++) {
        text_at(dc, g.f_label, C_TEXT2, L->frame[i].left + S(2), L->label_y, labels[i]);
        field_frame(dc, L->frame[i], ID_TARGET + i, 1);
    }
    if (g.conn_status[0]) { /* a refused or failed Save, in place of the hint */
        text_in(dc, g.f_label, g.conn_error ? C_WARN : C_TEXT2, L->conn_hint, DT_LEFT | DT_WORDBREAK,
                g.conn_status);
        return;
    }
    char hint[200];
    snprintf(hint, sizeof hint,
             "Relay address and port belong to the %s route; rate and disarm are shared "
             "by both routes. Save disarms first.", route_name(g.cfg.loopback_mode));
    text_in(dc, g.f_label, C_TEXT3, L->conn_hint, DT_LEFT | DT_WORDBREAK, hint);
}

static void paint_input(HDC dc)
{
    layout_t *L = &g.L;
    const int XL = L->input.left + S(14), sub = XL + S(16);
    int loop = g.cfg.loopback_mode != 0, rep = g.pending.repeat_enabled != 0;
    card(dc, L->input, "INPUT");
    if (g.input_dirty) {
        int tx = XL + tracked_w(dc, g.f_section, "INPUT") + S(10);
        text_vc(dc, g.f_label, C_AMBER, tx, title_cy(L->input), "not applied");
    }

    struct { const char *text; int x, cy, on; } labels[] = {
        {"Send input to", XL, L->row_route, 1},
        {"Relay", XL, L->row_relay, 1},
        {"Sources", XL, L->row_sources, 1},
        {"Gaming PC pointer", XL, L->row_pointer, loop},
        {"Button", sub, L->row_button, rep},
        {"Repeat clicks/sec", L->repeat_button.right + S(16), L->row_button, rep},
        {"Repeat source", sub, L->row_source, rep && !loop},
        {"Virtual clicks/sec", XL, L->row_click, 1},
    };
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; i++)
        text_vc(dc, g.f_label, labels[i].on ? C_TEXT2 : C_TEXT3, labels[i].x, labels[i].cy, labels[i].text);
    text_vc(dc, g.f_label, C_TEXT3, L->click_cps.right + S(10), L->row_click, "1-25 per button");
    field_frame(dc, L->repeat_cps, ID_REPEAT_CPS, rep);
    field_frame(dc, L->click_cps, ID_CLICK_CPS, 1);

    char limited[96];
    snprintf(limited, sizeof limited, "Last ARM: %lu extra click attempts limited", (unsigned long)g.last_limited);
    text_vc(dc, g.f_label, C_TEXT3, XL, L->row_limited, limited);

    const char *status = g.input_status;
    if (!status[0])
        status = g.input_dirty ? "Changes not applied yet. Apply and save disarms first, then saves."
               : loop ? "Gaming PC: virtual input only; Physical and Touchpad stay local. Apply and "
                        "save disarms first; changes not applied are dropped when you leave this tab."
                      : "Apply and save disarms first; changes not applied are dropped when you leave "
                        "this tab. Hold to repeat replaces dragging for its button.";
    text_in(dc, g.f_label, g.input_error ? C_WARN : C_TEXT3, L->status, DT_LEFT | DT_WORDBREAK, status);
}

static void paint_tools(HDC dc)
{
    layout_t *L = &g.L;
    card(dc, L->tools, "TOOLS");

    /* hotkeys as keycaps */
    int x = L->tools.left + S(14), cy = L->hk_cy, ch = S(20), gap = S(4);
    if (!g.toggle_hk_ok) {
        char hk[48], msg[128];
        hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
        snprintf(msg, sizeof msg, "Toggle hotkey %s is in use — set toggle_hotkey in hidway.ini", hk);
        text_vc(dc, g.f_label, C_WARN, x, cy, msg);
    } else {
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

    card(dc, L->recent, "RECENT INPUT");
    int tw = tracked_w(dc, g.f_section, "RECENT INPUT");
    text_vc(dc, g.f_label, C_TEXT3, L->recent.left + S(14) + tw + S(10), title_cy(L->recent), "memory only");
    round_rect(dc, L->list, S(7), C_FIELD, C_FIELD_BD);
}

/* The whole client area except the child controls, into `dc`. */
static void paint_content(HDC dc, RECT cr)
{
    fill_solid(dc, cr, C_BG);
    paint_header(dc);
    switch (g.tab) {
    case TAB_CONTROL: paint_stats(dc); paint_live(dc); break;
    case TAB_CONNECTION: paint_connection(dc); break;
    case TAB_INPUT: paint_input(dc); break;
    default: paint_tools(dc); break;
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
    paint_content(dc, cr);
    BitBlt(wdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
           ps.rcPaint.bottom - ps.rcPaint.top, dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    SelectObject(dc, obmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

/* ---- tab strip ----------------------------------------------------------- */

/* A custom child control: one tab stop; Left/Right/Home/End (and Ctrl+Tab
 * while it has the focus) pick a tab. It reports a choice to the main window
 * as WM_COMMAND ID_TAB_CONTROL + index, the same command the tests post. */
static int tab_hot = -1;

static void tabs_pick(HWND h, int t)
{
    if (t >= 0 && t < TAB_COUNT)
        SendMessageA(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_TAB_CONTROL + t, BN_CLICKED), (LPARAM)h);
}

static int tabs_hit(HWND h, int x)
{
    RECT r;
    GetClientRect(h, &r);
    if (r.right <= 0)
        return -1;
    int t = x * TAB_COUNT / r.right;
    return t < 0 ? 0 : t >= TAB_COUNT ? TAB_COUNT - 1 : t;
}

static void paint_tabs(HWND h, HDC wdc)
{
    RECT r;
    GetClientRect(h, &r);
    if (r.right <= 0 || r.bottom <= 0)
        return;
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, r.right, r.bottom);
    HGDIOBJ ob = SelectObject(dc, bm);
    fill_solid(dc, r, C_BG);
    round_rect(dc, r, S(9), C_CARD, C_CARD_BD);
    int focus = GetFocus() == h && !(SendMessageA(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    for (int i = 0; i < TAB_COUNT; i++) {
        RECT t = {r.right * i / TAB_COUNT + S(3), S(3), r.right * (i + 1) / TAB_COUNT - S(3), r.bottom - S(3)};
        if (i == g.tab)
            round_rect(dc, t, S(7), C_SEC_H, focus ? C_FOCUS_BD : C_SEC_BD);
        else if (i == tab_hot)
            round_rect(dc, t, S(7), C_SEC, C_SEC);
        text_in(dc, g.f_btn, i == g.tab ? C_WHITE : i == tab_hot ? C_TEXT : C_TEXT2, t,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE, TAB_NAMES[i]);
    }
    BitBlt(wdc, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bm);
    DeleteDC(dc);
}

static LRESULT CALLBACK tabs_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_GETDLGCODE: {
        LRESULT code = DLGC_WANTARROWS;
        const MSG *m = (const MSG *)lp;
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
            code |= DLGC_WANTTAB; /* Ctrl+Tab picks the next tab; plain Tab moves on */
        return code;
    }
    case WM_KEYDOWN: {
        int t = g.tab, n = TAB_COUNT;
        if (wp == VK_LEFT) t = (t + n - 1) % n;
        else if (wp == VK_RIGHT) t = (t + 1) % n;
        else if (wp == VK_HOME) t = 0;
        else if (wp == VK_END) t = n - 1;
        else if (wp == VK_TAB && GetKeyState(VK_CONTROL) < 0) t = GetKeyState(VK_SHIFT) < 0 ? (t + n - 1) % n : (t + 1) % n;
        else break;
        tabs_pick(h, t);
        return 0;
    }
    case WM_LBUTTONDOWN:
        tabs_pick(h, tabs_hit(h, (short)LOWORD(lp)));
        return 0;
    case WM_MOUSEMOVE: {
        int t = tabs_hit(h, (short)LOWORD(lp));
        if (t != tab_hot) {
            tab_hot = t;
            TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tab_hot = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_UPDATEUISTATE:
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint_tabs(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint_tabs(h, (HDC)wp);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ---- owner-drawn controls ---------------------------------------------- */

static int is_checkbox(int id)
{
    return id == ID_CB_KBD || id == ID_CB_MOUSE || id == ID_CB_HIST ||
           id == ID_SRC_PHYSICAL || id == ID_SRC_TOUCHPAD || id == ID_SRC_VIRTUAL || id==ID_REPEAT;
}

/* Input tab boxes show the edits not applied yet (g.pending); History, on
 * the Tools tab, applies at once as before. In the Gaming PC route the
 * Physical / Touchpad boxes show off and Virtual shows on: that is what the
 * route captures, whatever the boxes remember for the Remote PC route. */
static int cb_checked(int id)
{
    if (id == ID_CB_HIST) return g.cfg.history_enabled;
    if (id == ID_REPEAT) return g.pending.repeat_enabled;
    if (g.cfg.loopback_mode && (id == ID_SRC_PHYSICAL || id == ID_SRC_TOUCHPAD)) return 0;
    if (g.cfg.loopback_mode && id == ID_SRC_VIRTUAL) return 1;
    if (id == ID_CB_KBD) return g.pending.relay_keyboard;
    if (id == ID_CB_MOUSE) return g.pending.relay_mouse;
    if (id == ID_SRC_PHYSICAL) return g.pending.source_physical;
    if (id == ID_SRC_TOUCHPAD) return g.pending.source_touchpad;
    return g.pending.source_virtual;
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

    /* paint what is behind the control first: no default-grey corners. Only
     * the action row sits on the window background; the rest is on cards. */
    fill_solid(dc, lr, (id == ID_ARM || id == ID_PANIC || id == ID_LOCAL_PICO) ? C_BG : C_CARD);

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
            if (d->itemState & ODS_DISABLED) {
                fill = C_SEC;
                bd = C_SEC_BD;
                tx = C_TEXT3;
            } else if (!g.armed) { /* the primary action while disarmed */
                fill = down ? C_ARM_P : hot ? C_ARM_H : C_ARM;
                bd = hot ? C_ARM_BD_H : C_ARM_BD;
                tx = hot ? C_WHITE : C_ARM_TX;
            } else {
                fill = down ? C_BTN_P : hot ? C_BTN_H : C_BTN;
                bd = fill;
                tx = C_WHITE;
            }
            f = g.f_btn_big;
            rad = S(10);
            strcpy(label, g.armed ? "DISARM" : "ARM");
        } else if (id == ID_PANIC) {
            fill = down ? C_DNG_P : hot ? C_DNG_H : C_DNG;
            bd = hot ? C_DNG_BD_H : C_DNG_BD;
            tx = hot ? RGB(255, 186, 178) : C_DNG_TX;
            f = g.f_btn_big;
            rad = S(10);
        } else if (id == ID_PROFILE_REMOTE || id == ID_PROFILE_GAMING) {
            /* two-way route selector: the route in use is lit, with a dot */
            int on = (id == ID_PROFILE_GAMING) == (g.cfg.loopback_mode != 0);
            fill = on ? C_CHIP : down ? C_SEC : hot ? C_SEC_H : C_FIELD;
            bd = on || hot ? C_FOCUS_BD : C_FIELD_BD;
            tx = on || hot ? C_WHITE : C_TEXT2;
            round_rect(dc, lr, rad, fill, bd);
            if (focus) {
                RECT fr = lr;
                InflateRect(&fr, -S(3), -S(3));
                round_rect(dc, fr, rad - S(2), C_FOCUS_BD, C_FOCUS_BD);
                InflateRect(&fr, -S(1), -S(1));
                round_rect(dc, fr, rad - S(3), fill, fill);
            }
            int tw = text_w(dc, f, label), dd = on ? S(8) + S(8) : 0;
            int x = (w - tw - dd) / 2;
            if (on)
                dot(dc, x + S(4), h / 2, S(4), C_LIVE);
            text_vc(dc, f, tx, x + dd, h / 2, label);
            BitBlt(d->hDC, r.left, r.top, w, h, dc, 0, 0, SRCCOPY);
            SelectObject(dc, ob);
            DeleteObject(bm);
            DeleteDC(dc);
            return;
        } else {
            fill = down ? C_SEC : hot ? C_SEC_H : C_SEC;
            bd = hot ? C_FOCUS_BD : C_SEC_BD;
            tx = hot ? C_WHITE : C_TEXT;
            if (id == ID_SAVE && g.saved_flash)
                strcpy(label, "Saved ✓");
            if(id==ID_LOCAL_PICO&&g.cfg.loopback_mode)bd=C_LIVE_BD;
            if(d->itemState&ODS_DISABLED)tx=C_TEXT3;
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
    int empty = (msg == WM_PAINT || msg == WM_ERASEBKGND || msg == WM_PRINTCLIENT) &&
                CallWindowProcA(orig, h, LB_GETCOUNT, 0, 0) == 0;
    if (msg == WM_ERASEBKGND && empty)
        return 1;
    if ((msg == WM_PAINT || msg == WM_PRINTCLIENT) && empty) {
        PAINTSTRUCT ps;
        HDC dc = msg == WM_PAINT ? BeginPaint(h, &ps) : (HDC)wp;
        RECT r;
        GetClientRect(h, &r);
        fill_solid(dc, r, C_FIELD);
        text_in(dc, g.f_label, C_TEXT3, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                g.cfg.history_enabled ? "No input yet — arm to start relaying" : "Key history is turned off");
        if (msg == WM_PAINT)
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

/* A whole number lo..hi in `edit` (spaces around it allowed). Returns 1 and
 * sets *out; 0 for an empty field, other text or a value out of range (an
 * overflow gives LONG_MIN / LONG_MAX, which no range here includes). */
static int field_int(HWND edit, int lo, int hi, int *out)
{
    char b[32], *end;
    GetWindowTextA(edit, b, sizeof b);
    const char *s = b;
    while (isspace((unsigned char)*s))
        s++;
    long n = strtol(s, &end, 10);
    if (end == s)
        return 0;
    while (isspace((unsigned char)*end))
        end++;
    if (*end || n < lo || n > hi)
        return 0;
    *out = (int)n;
    return 1;
}

/* The Connection tab's status line (shown in place of its hint). */
static void connection_status(const char *text, int error)
{
    snprintf(g.conn_status, sizeof g.conn_status, "%s", text ? text : "");
    g.conn_error = error;
    invalidate_fields();
}

/* Reopen the relay socket when the address in use changed (Save on the
 * Connection tab, or a route switch to a route with another address). */
static void relay_reopen(const char *old_target, int old_port)
{
    if (!g.preview && (strcmp(old_target, g.cfg.target) != 0 || old_port != g.cfg.port)) {
        g.net_ok = (hidway_net_open(g.cfg.target, g.cfg.port) == 0);
        hidway_net_notify(g.hwnd, WM_NET_READ);
        g.ever_status = 0;
        memset(&g.status_order, 0, sizeof g.status_order);
        g.probes_unanswered = 0;
    }
}

/* Connection tab Save. An invalid field is refused with a message on the
 * tab: nothing changes and nothing is disarmed. Otherwise this disarms
 * first, then saves; a failed save also changes nothing and says so.
 * Ranges are those hidway.ini is read with. Returns 1 when saved. */
static int apply_settings(void)
{
    char target[sizeof g.cfg.target] = {0};
    int port, rate, disarm;
    GetWindowTextA(g.edit[0], target, sizeof target);
    const char *t = target;
    while (isspace((unsigned char)*t))
        t++;
    if (!*t) {
        connection_status("Settings not saved: enter the relay address.", 1);
        return 0;
    }
    if (!field_int(g.edit[1], 1, 65535, &port)) {
        connection_status("Settings not saved: port must be 1 to 65535.", 1);
        return 0;
    }
    if (!field_int(g.edit[2], 10, 1000, &rate)) {
        connection_status("Settings not saved: rate must be 10 to 1000 Hz.", 1);
        return 0;
    }
    if (!field_int(g.edit[3], 0, 60000, &disarm)) {
        connection_status("Settings not saved: disarm must be 0 (off) to 60000 ms.", 1);
        return 0;
    }

    if(g.armed)set_armed(0); /* release old peer before editing/opening a new peer */
    char old_target[sizeof g.cfg.target];
    int old_port = g.cfg.port;
    strcpy(old_target, g.cfg.target);

    hidway_config_t next = g.cfg;
    memcpy(next.target, target, sizeof next.target);
    next.port = port;
    next.send_rate_hz = rate;
    next.auto_disarm_ms = disarm;
    hidway_profile_store(&next); /* address and port belong to the route in use */
    int saved = g.preview || hidway_config_save("hidway.ini", &next);
    if (saved)
        g.cfg = next;
    SecureZeroMemory(next.key_hex, sizeof next.key_hex);
    if (!saved) {
        connection_status("Could not save hidway.ini. Settings have not changed.", 1);
        return 0;
    }
    connection_status(NULL, 0);
    relay_reopen(old_target, old_port);
    history_add('i', "Settings saved");
    invalidate_readouts();
    return 1;
}

/* The Connection fields show the saved values (after a route switch). */
static void connection_fields_load(void)
{
    char tmp[32];
    if (g.edit[0]) SetWindowTextA(g.edit[0], g.cfg.target);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.port);
    if (g.edit[1]) SetWindowTextA(g.edit[1], tmp);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.send_rate_hz);
    if (g.edit[2]) SetWindowTextA(g.edit[2], tmp);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.auto_disarm_ms);
    if (g.edit[3]) SetWindowTextA(g.edit[3], tmp);
    connection_status(NULL, 0);
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
static void option_items(HWND combo,const char *const *items,unsigned n,int selected)
{
    for(unsigned i=0;i<n;i++)SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)items[i]);
    SendMessageA(combo,CB_SETCURSEL,(WPARAM)selected,0);
    SetWindowTheme(combo,L"DarkMode_Explorer",NULL);
}
/* Input tab combo boxes. The client has no common-controls v6 manifest,
 * so visual styles (and DarkMode themes) never apply to them: like the Input
 * Monitor's dropdowns the items are owner-drawn, and the closed box with its
 * drop arrow is painted here in the field colours. */
static void chevron(HDC dc, int cx, int cy, COLORREF c)
{
    void *gr = gp_begin(dc);
    void *pen = NULL;
    float u = (float)g.dpi / 96.0f;
    GdipCreatePen1(argb(c), u * 1.6f, 2 /* UnitPixel */, &pen);
    GdipSetPenStartCap(pen, 2);
    GdipSetPenEndCap(pen, 2);
    GdipSetPenLineJoin(pen, 2);
    HW_PointF pts[3] = {{cx - 4.0f * u, cy - 2.0f * u}, {(float)cx, cy + 2.0f * u}, {cx + 4.0f * u, cy - 2.0f * u}};
    GdipDrawLines(gr, pen, pts, 3);
    GdipDeletePen(pen);
    GdipDeleteGraphics(gr);
}
static void combo_text(HWND combo, LRESULT item, char *text, size_t cap)
{
    text[0] = 0;
    if (item >= 0 && SendMessageA(combo, CB_GETLBTEXTLEN, (WPARAM)item, 0) < (LRESULT)cap)
        SendMessageA(combo, CB_GETLBTEXT, (WPARAM)item, (LPARAM)text);
}
static void draw_combo_item(const DRAWITEMSTRUCT *d)
{
    RECT r = d->rcItem;
    int edit = (d->itemState & ODS_COMBOBOXEDIT) != 0, off = (d->itemState & ODS_DISABLED) != 0;
    fill_solid(d->hDC, r, !edit && (d->itemState & ODS_SELECTED) ? C_CHIP : C_FIELD);
    char text[128];
    combo_text(d->hwndItem, d->itemID == (UINT)-1 ? -1 : (LRESULT)d->itemID, text, sizeof text);
    r.left = edit ? S(10) : r.left + S(10); /* the closed box: same x as paint_combo */
    text_in(d->hDC, g.f_body, off ? C_TEXT3 : C_TEXT, r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, text);
}
static void paint_combo(HWND h, HDC wdc)
{
    RECT r;
    GetClientRect(h, &r);
    if (r.right <= 0 || r.bottom <= 0)
        return;
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, r.right, r.bottom);
    HGDIOBJ ob = SelectObject(dc, bm);
    int on = IsWindowEnabled(h) != 0;
    fill_solid(dc, r, C_CARD); /* the Input tab card behind the rounded corners */
    round_rect(dc, r, S(6), C_FIELD, on && GetFocus() == h ? C_FOCUS_BD : C_FIELD_BD);
    char text[128];
    combo_text(h, SendMessageA(h, CB_GETCURSEL, 0, 0), text, sizeof text);
    RECT tr = {S(10), 0, r.right - S(30), r.bottom};
    text_in(dc, g.f_body, on ? C_TEXT : C_TEXT3, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, text);
    chevron(dc, r.right - S(16), r.bottom / 2, on ? C_TEXT2 : C_TEXT3);
    BitBlt(wdc, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob);
    DeleteObject(bm);
    DeleteDC(dc);
}
static LRESULT CALLBACK combo_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC orig = (WNDPROC)GetPropA(h, "op");
    if (!orig)
        return DefWindowProcA(h, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint_combo(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint_combo(h, (HDC)wp);
        return 0;
    /* The classic combo draws its button/selection straight to the screen
     * on these; paint the dark box again right after. */
    case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_CAPTURECHANGED: case WM_KEYDOWN: case WM_CHAR: case CB_SETCURSEL: {
        LRESULT res = CallWindowProcA(orig, h, msg, wp, lp);
        InvalidateRect(h, NULL, FALSE);
        return res;
    }
    case WM_NCDESTROY:
        RemovePropA(h, "op");
        break;
    }
    return CallWindowProcA(orig, h, msg, wp, lp);
}

/* ---- routes, Input tab --------------------------------------------------- */

/* What the Gaming PC route always means: virtual input only, so virtual on
 * and repeat on virtual input. Physical / Touchpad keep their values and are
 * ignored by input_source_mask() there. */
static void route_constraints(hidway_config_t *c)
{
    if (c->loopback_mode) {
        c->source_virtual = 1;
        c->repeat_sources = HIDWAY_SOURCE_VIRTUAL;
    }
}

static void input_status(const char *text, int error)
{
    snprintf(g.input_status, sizeof g.input_status, "%s", text ? text : "");
    g.input_error = error;
    invalidate_fields();
}

/* Enable rules of the Input tab, from the route in use and the pending
 * Hold to repeat choice. Disabled controls keep their values. */
static void input_tab_refresh(void)
{
    int loop = g.cfg.loopback_mode != 0, rep = g.pending.repeat_enabled != 0;
    for (int i = 0; i < 3; i++)
        if (g.src[i]) {
            EnableWindow(g.src[i], !loop);
            InvalidateRect(g.src[i], NULL, FALSE);
        }
    if (g.pointer_route) EnableWindow(g.pointer_route, loop);
    if (loop && g.repeat_source) SendMessageA(g.repeat_source, CB_SETCURSEL, 2, 0);
    if (g.repeat_source) EnableWindow(g.repeat_source, !loop && rep);
    if (g.repeat_button) EnableWindow(g.repeat_button, rep);
    if (g.repeat_cps) EnableWindow(g.repeat_cps, rep);
    HWND redraw[] = {g.cb[0], g.cb[1], g.repeat, g.profile_button[0], g.profile_button[1], g.local_pico_button};
    for (size_t i = 0; i < sizeof redraw / sizeof redraw[0]; i++)
        if (redraw[i])
            InvalidateRect(redraw[i], NULL, FALSE);
    invalidate_fields();
}

/* Unapplied edits are dropped: the controls show the saved values again.
 * Called when the Input tab is entered and after every save. */
static void input_tab_load(void)
{
    g.pending = g.cfg;
    char text[24];
    if (g.pointer_route) SendMessageA(g.pointer_route, CB_SETCURSEL, (WPARAM)(g.cfg.loopback_pointer_pico ? 1 : 0), 0);
    if (g.repeat_button) SendMessageA(g.repeat_button, CB_SETCURSEL, (WPARAM)g.cfg.repeat_button, 0);
    int selected = 0;
    for (int i = 0; i < 7; i++)
        if (repeat_source_masks[i] == g.cfg.repeat_sources)
            selected = i;
    if (g.repeat_source) SendMessageA(g.repeat_source, CB_SETCURSEL, (WPARAM)selected, 0);
    snprintf(text, sizeof text, "%d", g.cfg.repeat_cps);
    if (g.repeat_cps) SetWindowTextA(g.repeat_cps, text);
    snprintf(text, sizeof text, "%d", g.cfg.virtual_click_cps);
    if (g.click_cps) SetWindowTextA(g.click_cps, text);
    g.input_dirty = 0; /* after the texts: EN_CHANGE marks edits */
    g.input_error = 0;
    g.input_status[0] = 0;
    input_tab_refresh();
}

/* Save `next` as the configuration in use. A capture-policy change always
 * starts from a released state, so this disarms first. On a save failure
 * nothing changes and the reason is shown on the Input tab or in LIVE INPUT. */
static int config_commit(const hidway_config_t *next, const char *saved, int on_input_tab)
{
    if(g.armed)set_armed(0);
    hidway_config_t copy = *next;
    hidway_profile_store(&copy);
    if(!g.preview&&!hidway_config_save("hidway.ini",&copy)) {
        const char *message="Could not save hidway.ini. Settings have not changed.";
        if(on_input_tab)input_status(message,1);
        else{snprintf(g.local_pico_notice,sizeof g.local_pico_notice,"%s",message);invalidate_readouts();}
        return 0;
    }
    g.cfg=copy;
    g.local_pico_notice[0]=0;
    input_tab_load();
    history_add('i',saved);
    invalidate_readouts();
    return 1;
}

/* Input tab: Apply and save. Same validation and error texts as the former
 * Input options window, plus Keyboard / Mouse / Sources. */
static int input_apply(void)
{
    int cps=option_speed(g.repeat_cps),limit=option_speed(g.click_cps);
    if(cps<1||cps>25||limit<1||limit>25) {
        input_status("Settings not saved: clicks/sec must be 1 to 25.",1);return 0;
    }
    hidway_config_t next=g.cfg;
    next.relay_keyboard=g.pending.relay_keyboard;
    next.relay_mouse=g.pending.relay_mouse;
    next.source_physical=g.pending.source_physical;
    next.source_touchpad=g.pending.source_touchpad;
    next.source_virtual=g.pending.source_virtual;
    next.loopback_pointer_pico=SendMessageA(g.pointer_route,CB_GETCURSEL,0,0)==1;
    next.repeat_enabled=g.pending.repeat_enabled;
    int button=(int)SendMessageA(g.repeat_button,CB_GETCURSEL,0,0);
    next.repeat_button=button<0||button>4?0:button;
    int selection=(int)SendMessageA(g.repeat_source,CB_GETCURSEL,0,0);
    next.repeat_sources=repeat_source_masks[selection<0||selection>6?0:selection];
    next.repeat_cps=cps;next.virtual_click_cps=limit;
    /* Remote PC: Hold to repeat may only name ticked sources ("All selected
     * sources" always fits). Gaming PC always repeats virtual input. */
    int ticked=(next.source_physical?HIDWAY_SOURCE_PHYSICAL:0)|(next.source_touchpad?HIDWAY_SOURCE_TOUCHPAD:0)|
               (next.source_virtual?HIDWAY_SOURCE_VIRTUAL:0);
    if(!next.loopback_mode&&next.repeat_enabled&&next.repeat_sources!=7&&(next.repeat_sources&~ticked)) {
        input_status("Settings not saved: the repeat source includes a source that is not ticked under Sources.",1);
        return 0;
    }
    route_constraints(&next);
    if(!config_commit(&next,"Input settings saved - arm when ready",1))return 0;
    input_status("Saved. Press ARM when ready.",0);
    return 1;
}

/* Switch route: disarm, keep the values of the route being left, load the
 * other route's own values, apply and save. Unapplied Input tab edits are
 * dropped. */
static int route_select(int mode, const char *saved, int on_input_tab)
{
    char old_target[128];
    int old_port = g.cfg.port;
    strcpy(old_target, g.cfg.target);
    hidway_config_t next = g.cfg;
    hidway_profile_select(&next, mode);
    route_constraints(&next);
    if (!config_commit(&next, saved, on_input_tab))
        return 0;
    relay_reopen(old_target, old_port);
    connection_fields_load();
    return 1;
}

static void select_tab(int t)
{
    if (t < 0 || t >= TAB_COUNT)
        return;
    if (t == TAB_INPUT && g.tab != TAB_INPUT)
        input_tab_load(); /* unapplied edits from an earlier visit are gone */
    g.tab = t;
    show_tab_controls();
    if (g.tabs)
        InvalidateRect(g.tabs, NULL, FALSE);
    if (g.hwnd)
        InvalidateRect(g.hwnd, NULL, FALSE);
}

static void local_pico_notice(const char *message)
{
    snprintf(g.local_pico_notice, sizeof g.local_pico_notice, "%s", message);
    invalidate_readouts();
}

/* Pico on this PC: the existing checks, then the Gaming PC route with its
 * own saved values. Never ARMs. The result is shown in LIVE INPUT on the
 * Control tab, which it selects. */
static void use_local_pico(void)
{
    select_tab(TAB_CONTROL);
    /* A main-window click must not end or reconfigure an active capture/replay. */
    if(g.armed||replay.active) {
        local_pico_notice("DISARM before selecting Pico on this PC.");return;
    }
    char target[128],port[24],expected_port[24];
    if(g.edit[0]) {
        GetWindowTextA(g.edit[0],target,sizeof target);
        GetWindowTextA(g.edit[1],port,sizeof port);
        snprintf(expected_port,sizeof expected_port,"%d",g.cfg.port);
        if(strcmp(target,g.cfg.target)||strcmp(port,expected_port)) {
            local_pico_notice("Save the Pi relay address and port on the Connection tab first.");return;
        }
    }
    if(!g.net_ok||hidway_net_peer_is_loopback()!=0) {
        local_pico_notice("Save the Pi relay address on the Connection tab first.\r\n"
            "127.0.0.1 / localhost cannot forward USB HID through the Pi.");return;
    }
    if(hidway_local_pico_interfaces(g.cfg.loopback_device)!=HIDWAY_PICO_READY) {
        local_pico_notice("Pico keyboard and mouse USB devices were not both found here.\r\n"
            "Use this button on the Gaming PC with Pico USB connected.");return;
    }
    /* The checked address must be the one the Gaming PC route will use. */
    hidway_config_t next=g.cfg;
    hidway_profile_select(&next,HIDWAY_PROFILE_GAMING);
    if(strcmp(next.target,g.cfg.target)||next.port!=g.cfg.port) {
        char message[192];
        snprintf(message,sizeof message,"The Gaming PC route uses relay %s:%d. Select Gaming PC on the "
                 "Input tab and check its address on the Connection tab.",next.target,next.port);
        local_pico_notice(message);return;
    }
    if(route_select(HIDWAY_PROFILE_GAMING,"Pico on this PC: Gaming PC route saved - arm when ready",0)) {
        snprintf(g.local_pico_notice,sizeof g.local_pico_notice,
            "Pico on this PC: virtual keys, clicks and wheels -> USB HID.\r\n"
            "Pointer %s. Press ARM when ready.",
            g.cfg.loopback_pointer_pico?"moves through Pico (experimental)":"stays native");
        invalidate_readouts();
    }
}

static void register_tabs_class(void)
{
    WNDCLASSA wc;
    if (GetClassInfoA(g.inst, "HIDwayTabs", &wc))
        return;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = tabs_proc;
    wc.hInstance = g.inst;
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    wc.lpszClassName = "HIDwayTabs";
    RegisterClassA(&wc);
}

/* Creation order is the keyboard order: tab strip, page controls in reading
 * order, then ARM / Pico on this PC / PANIC. */
static void create_controls(HWND h)
{
    register_tabs_class();
    g.tabs = CreateWindowExA(0, "HIDwayTabs", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, h,
                             (HMENU)(INT_PTR)ID_TABS, g.inst, NULL);

    /* Connection */
    char tmp[32];
    g.edit[0] = mk(h, "EDIT", g.cfg.target, ES_AUTOHSCROLL | WS_TABSTOP, ID_TARGET);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.port);
    g.edit[1] = mk(h, "EDIT", tmp, ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_PORT);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.send_rate_hz);
    g.edit[2] = mk(h, "EDIT", tmp, ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_RATE);
    snprintf(tmp, sizeof tmp, "%d", g.cfg.auto_disarm_ms);
    g.edit[3] = mk(h, "EDIT", tmp, ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_AUTO);
    g.save = mk(h, "BUTTON", "Save", BS_OWNERDRAW | WS_TABSTOP, ID_SAVE);

    /* Input */
    const DWORD combo = CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL;
    g.profile_button[0] = mk(h, "BUTTON", "Remote PC", BS_OWNERDRAW | WS_TABSTOP, ID_PROFILE_REMOTE);
    g.profile_button[1] = mk(h, "BUTTON", "Gaming PC", BS_OWNERDRAW | WS_TABSTOP, ID_PROFILE_GAMING);
    g.cb[0] = mk(h, "BUTTON", "Keyboard", BS_OWNERDRAW | WS_TABSTOP, ID_CB_KBD);
    g.cb[1] = mk(h, "BUTTON", "Mouse", BS_OWNERDRAW | WS_TABSTOP, ID_CB_MOUSE);
    g.src[0] = mk(h, "BUTTON", "Physical", BS_OWNERDRAW | WS_TABSTOP, ID_SRC_PHYSICAL);
    g.src[1] = mk(h, "BUTTON", "Touchpad", BS_OWNERDRAW | WS_TABSTOP, ID_SRC_TOUCHPAD);
    g.src[2] = mk(h, "BUTTON", "Virtual / remote", BS_OWNERDRAW | WS_TABSTOP, ID_SRC_VIRTUAL);
    g.pointer_route = mk(h, "COMBOBOX", "", combo, ID_POINTER);
    const char *pointers[] = {"Native position (recommended)", "Through Pico: relative (experimental)"};
    option_items(g.pointer_route, pointers, 2, g.cfg.loopback_pointer_pico);
    g.repeat = mk(h, "BUTTON", "Hold to repeat", BS_OWNERDRAW | WS_TABSTOP, ID_REPEAT);
    g.repeat_button = mk(h, "COMBOBOX", "", combo, ID_REPEAT_BUTTON);
    const char *buttons[] = {"Left", "Right", "Middle", "Back", "Forward"};
    option_items(g.repeat_button, buttons, 5, g.cfg.repeat_button);
    g.repeat_cps = mk(h, "EDIT", "", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_REPEAT_CPS);
    g.repeat_source = mk(h, "COMBOBOX", "", combo, ID_REPEAT_SOURCE);
    const char *sources[] = {"Physical", "Touchpad", "Virtual / remote", "Physical + touchpad",
                             "Physical + virtual", "Touchpad + virtual", "All selected sources"};
    option_items(g.repeat_source, sources, 7, 0);
    g.click_cps = mk(h, "EDIT", "", ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, ID_CLICK_CPS);
    g.input_apply = mk(h, "BUTTON", "Apply and save", BS_OWNERDRAW | WS_TABSTOP, ID_INPUT_APPLY);

    /* Tools */
    g.monitor_button = mk(h, "BUTTON", "Monitor", BS_OWNERDRAW | WS_TABSTOP, ID_MONITOR);
    g.replay_button = mk(h, "BUTTON", replay.active ? "Stop" : "Replay...", BS_OWNERDRAW | WS_TABSTOP, ID_REPLAY);
    g.updates_button = mk(h, "BUTTON", "Updates", BS_OWNERDRAW | WS_TABSTOP, ID_UPDATES);
    g.cb[2] = mk(h, "BUTTON", "History", BS_OWNERDRAW | WS_TABSTOP, ID_CB_HIST);
    g.clear = mk(h, "BUTTON", "Clear", BS_OWNERDRAW | WS_TABSTOP, ID_CLEAR);
    g.history = mk(h, "LISTBOX", "",
                   LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_NOSEL | WS_VSCROLL,
                   ID_HISTORY);
    SetWindowTheme(g.history, L"DarkMode_Explorer", NULL); /* dark scrollbar */
    subclass(g.history, list_proc);

    /* every tab */
    g.arm = mk(h, "BUTTON", "ARM", BS_OWNERDRAW | WS_TABSTOP, ID_ARM);
    g.local_pico_button = mk(h, "BUTTON", "Pico on this PC", BS_OWNERDRAW | WS_TABSTOP, ID_LOCAL_PICO);
    g.panic = mk(h, "BUTTON", "PANIC", BS_OWNERDRAW | WS_TABSTOP, ID_PANIC);
    if (g.armed && g.local_pico_button)
        EnableWindow(g.local_pico_button, FALSE); /* same rule as set_armed() */

    HWND btns[] = {g.clear, g.save, g.arm, g.panic, g.cb[0], g.cb[1], g.cb[2], g.src[0], g.src[1], g.src[2],
                   g.monitor_button, g.replay_button, g.updates_button, g.local_pico_button, g.repeat,
                   g.profile_button[0], g.profile_button[1], g.input_apply};
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++)
        if (btns[i])
            subclass(btns[i], btn_proc);
    HWND combos[] = {g.pointer_route, g.repeat_button, g.repeat_source};
    for (size_t i = 0; i < sizeof combos / sizeof combos[0]; i++)
        if (combos[i])
            subclass(combos[i], combo_proc);

    apply_layout();
    input_tab_load();
}

/* Short hover help for the main window's buttons. A tooltip never takes
 * the focus or the foreground (shown no-activate), and it appears only while
 * the main window itself is active, so the badge and capture are unaffected. */
static void add_tooltips(HWND hwnd)
{
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    g.tooltip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL,
                                g.inst, NULL);
    if (!g.tooltip)
        return;
    SendMessageA(g.tooltip, TTM_SETMAXTIPWIDTH, 0, S(300));
    SendMessageA(g.tooltip, TTM_SETTIPBKCOLOR, C_CARD, 0); /* no visual styles: colours apply */
    SendMessageA(g.tooltip, TTM_SETTIPTEXTCOLOR, C_TEXT, 0);
    struct { HWND h; const char *text; } tips[] = {
        {g.arm, "ARM: send this PC's keyboard and mouse to the Gaming PC. Press again (or the toggle hotkey) to DISARM."},
        {g.local_pico_button, "Check the local Pico USB devices and the relay address, then select and save the Gaming PC route. Does not ARM."},
        {g.panic, "PANIC: release every key and button immediately and disarm."},
        {g.tabs, "Control, Connection, Input, Tools. Left / Right arrows switch tabs; switching never disarms."},
        {g.profile_button[0], "Remote PC route: this PC controls the Gaming PC. Switching disarms, keeps each route's own settings and saves."},
        {g.profile_button[1], "Gaming PC route: the Pico is connected to this PC; virtual input only. Switching disarms, keeps each route's own settings and saves."},
        {g.input_apply, "Disarm, then apply and save the input settings of the route in use."},
        {g.monitor_button, "Open the Input Monitor: devices, live events and recordings."},
        {g.replay_button, "Load a recording and replay it through the Pi and Pico after confirmation."},
        {g.save, "Save the relay address and port of the route in use, and the shared rate and disarm settings."},
        {g.clear, "Clear the recent input list (kept in memory only)."},
        {g.updates_button, "Check for HIDway updates."},
    };
    for (size_t i = 0; i < sizeof tips / sizeof tips[0]; i++) {
        if (!tips[i].h)
            continue;
        TTTOOLINFOA ti = {0};
        ti.cbSize = TTTOOLINFOA_V2_SIZE; /* comctl32 v5: the larger v6 size is rejected */
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = hwnd;
        ti.uId = (UINT_PTR)tips[i].h;
        ti.lpszText = (LPSTR)tips[i].text;
        SendMessageA(g.tooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
    }
}

/* Read-only Pico inventory from the GUI thread's own timer; a refresh never
 * starts while one is running. Indication only: it gates nothing. */
static void pico_refresh(void)
{
    if (g.pico_pending)
        return;
    g.pico_pending = 1;
    unsigned state = hidway_local_pico_interfaces(g.cfg.loopback_device);
    g.pico_pending = 0;
    if (!g.pico_known || state != g.pico_state) {
        g.pico_state = state;
        g.pico_known = 1;
        invalidate_readouts();
    }
}

static void resize_to_layout(HWND hwnd, const RECT *pos);

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
    /* a --dpi preview scales the content only; the frame keeps the real DPI */
    AdjustWindowRectExForDpi(&wr, WIN_STYLE, FALSE, 0, g.preview_dpi ? GetDpiForWindow(hwnd) : g.dpi);
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
        if (g.local_pico_button)
            EnableWindow(g.local_pico_button, FALSE); /* as set_armed(1) would */
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

/* --shot: render the client area straight into a bitmap (the painted page,
 * then every visible child through WM_PRINT at its own position). Unlike a
 * capture of the window, this does not depend on DWM having composed an
 * off-screen, resized window or child controls shown after creation. */
static void save_screenshot(HWND hwnd)
{
    RECT cr;
    GetClientRect(hwnd, &cr);
    int w = cr.right, h = cr.bottom;
    if (w <= 0 || h <= 0)
        return;
    HDC sdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, w, h);
    HGDIOBJ ob = SelectObject(mdc, bm);
    paint_content(mdc, cr);
    for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!(GetWindowLongPtrA(c, GWL_STYLE) & WS_VISIBLE))
            continue;
        RECT r;
        GetWindowRect(c, &r);
        MapWindowPoints(NULL, hwnd, (POINT *)&r, 2);
        int saved = SaveDC(mdc);
        SetViewportOrgEx(mdc, r.left, r.top, NULL);
        IntersectClipRect(mdc, 0, 0, r.right - r.left, r.bottom - r.top);
        SendMessageA(c, WM_PRINT, (WPARAM)mdc, PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
        RestoreDC(mdc, saved);
    }
    SelectObject(mdc, ob);

    BITMAPINFOHEADER bi = {sizeof bi, w, -h, 1, 32, BI_RGB};
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4);
    if (px && GetDIBits(sdc, bm, 0, (UINT)h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS)) {
        FILE *f = fopen(g.shot_path, "wb");
        if (f) {
            BITMAPFILEHEADER fh = {0x4D42, (DWORD)(sizeof fh + sizeof bi + (size_t)w * h * 4), 0, 0,
                                   (DWORD)(sizeof fh + sizeof bi)};
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&bi, sizeof bi, 1, f);
            fwrite(px, 4, (size_t)w * h, f);
            fclose(f);
        }
    }
    free(px);
    DeleteObject(bm);
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
}

static int window_executable_matches(HWND window,const wchar_t *expected)
{
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);HANDLE process=pid?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid):NULL;
    if(!process)return 0;wchar_t path[2048];DWORD length=2048;
    int ok=QueryFullProcessImageNameW(process,0,path,&length)&&!_wcsicmp(path,expected);CloseHandle(process);return ok;
}
static void open_monitor(void)
{
    int result=hw_companion_open(g.hwnd,HW_COMPANION_MONITOR);
    if(result!=HW_COMPANION_OK)MessageBoxW(g.hwnd,hw_companion_error(result),L"Monitor unavailable",MB_OK|MB_ICONINFORMATION);
}
static void replay_choose(void)
{
    if(replay.active){set_armed(0);history_add('i',"Replay stopped - all released");return;}
    if(g.armed||g.preview||g.options){history_add('i',"DISARM before loading a replay");return;}
    wchar_t path[2048];wcscpy(path,replay.path);
    if(!path[0]) {
        OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=g.hwnd;dialog.lpstrFilter=L"HIDway recording (*.hidrec)\0*.hidrec\0\0";
        dialog.lpstrFile=path;dialog.nMaxFile=2048;dialog.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&dialog))return;
    }
    replay.path[0]=0;hw_rec_free(&replay.file);
    FILE *f=_wfopen(path,L"rb");char error[160];
    if(!f){MessageBoxW(g.hwnd,L"Cannot open recording.",L"Replay",MB_OK|MB_ICONERROR);return;}
    int ok=hw_rec_load(f,&replay.file,error,sizeof error);fclose(f);
    if(!ok){MessageBoxA(g.hwnd,error,"Replay rejected",MB_OK|MB_ICONERROR);return;}
    for(unsigned i=0;i<replay.file.count;i++) {
        const hw_rec_event *e=&replay.file.events[i];uint16_t binding;
        if(!hidway_replay_binding(e,&binding)||(!g.cfg.relay_keyboard&&e->type<=HW_REC_KEY_UP)||(!g.cfg.relay_mouse&&e->type>HW_REC_KEY_UP&&e->type!=HW_REC_RELEASE_DEVICE)) {
            hw_rec_free(&replay.file);MessageBoxW(g.hwnd,L"This recording contains an unsupported key or an input type disabled in HIDway. Enable Keyboard/Mouse as appropriate. Unicode text/IME packets cannot be replayed as HID keys.",L"Replay rejected",MB_OK|MB_ICONINFORMATION);return;
        }
    }
    wchar_t target[128],message[1024];to_wide(g.cfg.target,target,128);
    swprintf(message,1024,L"Replay %u events over %.2f seconds to %ls:%d?\n\nLive capture is disabled during playback. Choose a blank test window on the target. Playback begins after 3 seconds.\n\nStop: Replay button, ARM toggle or PANIC hotkey. Playback runs once and releases everything when finished.",replay.file.count,replay.file.duration/1000.0,target,g.cfg.port);
    if(MessageBoxW(g.hwnd,message,L"Start HIDway replay",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK){hw_rec_free(&replay.file);return;}
    memset(&replay.state,0,sizeof replay.state);replay.next=0;replay.active=1;
    replay.started=timeGetTime()+3000u;replay.last_tick=timeGetTime();g.generation++;g.exclusive_capture=0;
    InterlockedExchange(&capture_fault,0);capture_queue_clear();
    set_armed(1);
    if(!g.armed){replay.active=0;hw_rec_free(&replay.file);return;}
    SetWindowTextA(g.replay_button,"Stop");history_add('i',"REPLAY: starts in 3 seconds; live capture disabled");
}
static void replay_tick(void)
{
    uint32_t now=timeGetTime();if((int32_t)(now-replay.started)<0){replay.last_tick=now;return;}
    /* Preserve ordered edges without a late catch-up burst. The output journal
     * supplies minimum HID dwell; pause the recording clock under pressure. */
    if(hidway_event_backlog_ms(&g.events)>24u){replay.started+=now-replay.last_tick;replay.last_tick=now;return;}
    replay.last_tick=now;uint32_t elapsed=now-replay.started;
    for(unsigned batch=0;batch<4&&replay.next<replay.file.count;batch++) {
        hw_rec_event *e=&replay.file.events[replay.next];if(e->ms>elapsed)break;
        if(!hidway_replay_apply(&replay.state,&replay.file.screen,e)){set_armed(0);history_add('w',"Replay mapping failed - released");return;}
        replay.next++;
        if(!publish_input())return;
    }
    g.cum_x=replay.state.frame.x;g.cum_y=replay.state.frame.y;g.cum_wheel=replay.state.frame.wheel;g.cum_pan=replay.state.frame.pan;
    if(replay.next==replay.file.count&&elapsed>=replay.file.duration&&!g.events.count){set_armed(0);history_add('i',"Replay complete - all released");}
}
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COPYDATA: {
        const COPYDATASTRUCT *data=(const COPYDATASTRUCT *)lp;
        if(!data||data->dwData!=RECORDING_IPC||!data->lpData||data->cbData<sizeof(wchar_t)||data->cbData>sizeof replay.path||data->cbData%sizeof(wchar_t))return FALSE;
        const wchar_t *path=data->lpData;unsigned length=data->cbData/sizeof(wchar_t);
        if(path[length-1]||wcsnlen(path,length)!=length-1||g.armed)return FALSE;
        memcpy(replay.path,path,data->cbData);history_add('i',"Recording ready: click Replay to review and start");return TRUE;
    }
    case WM_CREATE:
        g.hwnd = hwnd;
        g.dpi = g.preview_dpi ? g.preview_dpi : GetDpiForWindow(hwnd);
        create_fonts();
        g.tab = TAB_CONTROL; /* every start opens on Control */
        compute_layout();
        load_mark_icon();
        create_controls(hwnd);
        apply_window_chrome(hwnd);
        resize_to_layout(hwnd, NULL);
        keep_on_work_area(hwnd);
        if (!g.shot_path[0])
            add_tooltips(hwnd);
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
            select_tab(g.preview_tab); /* --tab=NAME */
            if (g.shot_path[0])
                SetTimer(hwnd, TIMER_SHOT, 900, NULL);
        }
        badge_refresh(); /* preview: shown too, without taking anything over */
        SetTimer(hwnd, TIMER_UI, UI_REFRESH_MS, NULL);
        pico_refresh();
        SetTimer(hwnd, TIMER_PICO, PICO_REFRESH_MS, NULL);
        return 0;

    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        create_fonts();
        compute_layout();
        load_mark_icon();
        resize_to_layout(hwnd, (const RECT *)lp);
        apply_layout();
        if (!g.in_sizemove) /* never fight a drag in progress */
            keep_on_work_area(hwnd);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        g.in_sizemove = 1;
        break;
    case WM_EXITSIZEMOVE:
        g.in_sizemove = 0;
        break;

    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_LISTBOX)
            mi->itemHeight = (UINT)S(21);
        else if (mi->CtlType == ODT_COMBOBOX) /* closed box (itemID -1) and list items */
            mi->itemHeight = (UINT)S(mi->itemID == (UINT)-1 ? 22 : 24);
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_LISTBOX)
            draw_history_item(d);
        else if (d->CtlType == ODT_COMBOBOX)
            draw_combo_item(d);
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
    case WM_CTLCOLORSTATIC: /* a greyed-out field stays a field */
        if ((HWND)lp == g.repeat_cps || (HWND)lp == g.click_cps) {
            SetBkColor((HDC)wp, C_FIELD);
            SetTextColor((HDC)wp, C_TEXT3);
            return (LRESULT)g.b_field;
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        /* a click on a field frame focuses its edit; only on the visible tab */
        struct { const RECT *frame; HWND edit; int tab; } fields[] = {
            {&g.L.frame[0], g.edit[0], TAB_CONNECTION}, {&g.L.frame[1], g.edit[1], TAB_CONNECTION},
            {&g.L.frame[2], g.edit[2], TAB_CONNECTION}, {&g.L.frame[3], g.edit[3], TAB_CONNECTION},
            {&g.L.repeat_cps, g.repeat_cps, TAB_INPUT}, {&g.L.click_cps, g.click_cps, TAB_INPUT},
        };
        for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
            if (fields[i].tab == g.tab && fields[i].edit && IsWindowEnabled(fields[i].edit) &&
                PtInRect(fields[i].frame, pt)) {
                SetFocus(fields[i].edit);
                SendMessageA(fields[i].edit, EM_SETSEL, 0, -1);
            }
        if (g.tab == TAB_CONTROL && PtInRect(&g.L.tiles[3], pt))
            send_wake(); /* only when the tile offers it */
        return 0;
    }

    /* The PC tile is a button while it offers wake: hand cursor and hover. */
    case WM_MOUSEMOVE: {
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        int hot = g.tab == TAB_CONTROL && PtInRect(&g.L.tiles[3], pt) && wake_offered();
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
            AppendMenuA(m, MF_STRING | (wake_offered() ? 0 : MF_GRAYED), IDM_WAKE, "Wake Gaming PC");
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
        if ((LOWORD(wp) >= ID_TARGET && LOWORD(wp) <= ID_AUTO) || LOWORD(wp) == ID_REPEAT_CPS ||
            LOWORD(wp) == ID_CLICK_CPS) {
            if (HIWORD(wp) == EN_SETFOCUS) {
                g.focus_id = LOWORD(wp);
                invalidate_fields();
            } else if (HIWORD(wp) == EN_KILLFOCUS) {
                g.focus_id = 0;
                invalidate_fields();
            } else if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) > ID_AUTO && !g.input_dirty) {
                g.input_dirty = 1; /* Input tab speed edited, not applied yet */
                invalidate_fields();
            }
            return 0;
        }
        /* Input tab combo boxes: repaint the dark closed box; a new choice is
         * an edit that waits for Apply and save. */
        if (LOWORD(wp) == ID_POINTER || LOWORD(wp) == ID_REPEAT_BUTTON || LOWORD(wp) == ID_REPEAT_SOURCE) {
            if (lp && (HIWORD(wp) == CBN_DROPDOWN || HIWORD(wp) == CBN_CLOSEUP || HIWORD(wp) == CBN_SELCHANGE))
                InvalidateRect((HWND)lp, NULL, FALSE);
            if (HIWORD(wp) == CBN_SELCHANGE) {
                g.input_dirty = 1;
                invalidate_fields();
            }
            return 0;
        }
        if (HIWORD(wp) == BN_CLICKED) {
            /* Tabs, the route selector and the Input tab boxes never disarm by
             * themselves: only applying a change (or switching route) does. */
            switch (LOWORD(wp)) {
            case ID_TAB_CONTROL: case ID_TAB_CONNECTION: case ID_TAB_INPUT: case ID_TAB_TOOLS:
                select_tab(LOWORD(wp) - ID_TAB_CONTROL);
                break;
            case ID_OPTIONS: select_tab(TAB_INPUT); break; /* the former Input options window */
            case ID_COMPACT: break;                        /* Less / More no longer exists */
            case ID_PROFILE_REMOTE: case ID_PROFILE_GAMING: {
                int mode = LOWORD(wp) == ID_PROFILE_GAMING;
                if (mode != g.cfg.loopback_mode &&
                    route_select(mode, mode ? "Route: Gaming PC - arm when ready" : "Route: Remote PC - arm when ready", 1))
                    input_status(mode ? "Gaming PC route selected and saved. Press ARM when ready."
                                      : "Remote PC route selected and saved. Press ARM when ready.", 0);
                break;
            }
            case ID_INPUT_APPLY: input_apply(); break;
            case ID_REPEAT:
                g.pending.repeat_enabled = !g.pending.repeat_enabled;
                g.input_dirty = 1;
                input_tab_refresh();
                break;
            case ID_MONITOR:open_monitor();break;
            case ID_REPLAY:replay_choose();break;
            case ID_LOCAL_PICO:use_local_pico();break;
            case ID_UPDATES:
                if(!g.preview){int result=hw_companion_open(hwnd,HW_COMPANION_UPDATES);
                    if(result!=HW_COMPANION_OK)MessageBoxW(hwnd,hw_companion_error(result),L"Updates unavailable",MB_OK|MB_ICONINFORMATION);}
                break;
            case ID_ARM: set_armed(!g.armed); break;
            case ID_PANIC: set_armed(0); history_add('w', "PANIC - all released"); break;
            case ID_CLEAR: SendMessageA(g.history, LB_RESETCONTENT, 0, 0); break;
            case ID_SAVE:
                if (!apply_settings())
                    break; /* the reason is on the Connection tab; no "Saved" flash */
                g.saved_flash = 1;
                InvalidateRect(g.save, NULL, FALSE);
                SetTimer(hwnd, TIMER_SAVED, 1400, NULL);
                break;
            case ID_CB_HIST: /* display only: applies at once, as before */
                g.cfg.history_enabled = !g.cfg.history_enabled;
                InvalidateRect(g.history, NULL, FALSE);
                break;
            case ID_CB_KBD: g.pending.relay_keyboard = !g.pending.relay_keyboard; g.input_dirty = 1; break;
            case ID_CB_MOUSE: g.pending.relay_mouse = !g.pending.relay_mouse; g.input_dirty = 1; break;
            /* Gaming PC: Physical / Touchpad are not selectable and Virtual is
             * forced on (the boxes are disabled; a queued click changes nothing) */
            case ID_SRC_PHYSICAL:
                if (!g.cfg.loopback_mode) { g.pending.source_physical = !g.pending.source_physical; g.input_dirty = 1; }
                break;
            case ID_SRC_TOUCHPAD:
                if (!g.cfg.loopback_mode) { g.pending.source_touchpad = !g.pending.source_touchpad; g.input_dirty = 1; }
                break;
            case ID_SRC_VIRTUAL:
                if (!g.cfg.loopback_mode) { g.pending.source_virtual = !g.pending.source_virtual; g.input_dirty = 1; }
                break;
            }
            if (is_checkbox(LOWORD(wp))) {
                if (lp)
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
        } else if (wp == TIMER_PICO) {
            pico_refresh();
        } else if (wp == TIMER_SHOT) {
            KillTimer(hwnd, TIMER_SHOT);
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
        KillTimer(hwnd, TIMER_PICO);
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
    /* Preview-only layout aids (ignored without --preview / --preview-armed):
     * --tab=control|connection|input|tools opens that tab (--options: Input). */
    if (g.preview) {
        const char *tab = strstr(cmd, "--tab=");
        if (tab)
            for (int i = 0; i < TAB_COUNT; i++)
                if (!_strnicmp(tab + 6, TAB_NAMES[i], strlen(TAB_NAMES[i])))
                    g.preview_tab = i;
        if (strstr(cmd, "--options"))
            g.preview_tab = TAB_INPUT;
        const char *dpi = strstr(cmd, "--dpi=");
        if (dpi) {
            int v = atoi(dpi + 6);
            g.preview_dpi = v >= 96 && v <= 384 ? (UINT)v : 0;
        }
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
    /* Configuration always belongs to the executable, including Explorer and
     * monitor-initiated launches without a shortcut working directory. */
    wchar_t executable_folder[2048];DWORD executable_length=GetModuleFileNameW(NULL,executable_folder,2048);
    if(!executable_length||executable_length>=2048)return 1;
    wchar_t *executable_slash=wcsrchr(executable_folder,L'\\');if(!executable_slash)return 1;*executable_slash=0;
    if(!SetCurrentDirectoryW(executable_folder))return 1;
    parse_args(cmd ? cmd : "");
    int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv){for(int i=1;i+1<argc;i++)if(!wcscmp(argv[i],L"--load-recording")&&wcslen(argv[i+1])<2048)wcscpy(replay.path,argv[i+1]);LocalFree(argv);}
    HANDLE instance_guard=NULL;
    if(!g.preview) {
        instance_guard=CreateMutexW(NULL,TRUE,L"Local\\HIDwayClientInputOwner");
        if(!instance_guard)return 1;
        if(GetLastError()==ERROR_ALREADY_EXISTS) {
            HWND existing=FindWindowA("HIDwayClient",NULL);
            if(existing){
                wchar_t current_exe[2048];GetModuleFileNameW(NULL,current_exe,2048);
                if(!window_executable_matches(existing,current_exe)){MessageBoxW(NULL,L"HIDway is already running from another folder. Close that copy before opening this version. The active input session was left untouched.",L"Close the previous HIDway",MB_OK|MB_ICONINFORMATION);CloseHandle(instance_guard);return 1;}
                ShowWindowAsync(existing,SW_RESTORE);
                if(replay.path[0]){COPYDATASTRUCT data={RECORDING_IPC,(DWORD)((wcslen(replay.path)+1)*sizeof(wchar_t)),replay.path};DWORD_PTR result=0;
                    if(!SendMessageTimeoutW(existing,WM_COPYDATA,0,(LPARAM)&data,SMTO_ABORTIFHUNG,2000,&result)||!result)
                        MessageBoxW(NULL,L"DISARM HIDway before loading a recording, then try again.",L"Replay not loaded",MB_OK|MB_ICONINFORMATION);
                }
            }
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
    wc.lpszClassName = g.preview?"HIDwayClientPreview":"HIDwayClient";
    if (!RegisterClassA(&wc))
        return 1;
    WNDCLASSA bc = {0};
    bc.lpfnWndProc = badge_proc;
    bc.hInstance = inst;
    bc.lpszClassName = "HIDwayBadge";
    if (!RegisterClassA(&bc))
        return 1;

    g.dpi = g.preview_dpi ? g.preview_dpi : GetDpiForSystem();
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
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "HIDway", WIN_STYLE, wx, wy,
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
    if(replay.path[0])history_add('i',"Recording ready: click Replay to review and start");

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
