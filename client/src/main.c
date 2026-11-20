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
#include "hidway_echo.h"
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
#include "update_check.h"
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
int WINAPI GdipDrawEllipse(void *graphics, void *pen, float x, float y, float w, float h);
int WINAPI GdipFillPolygon(void *graphics, void *brush, const HW_PointF *points, int count, int fill_mode);
int WINAPI GdipFillPie(void *graphics, void *brush, float x, float y, float w, float h, float start, float sweep);
int WINAPI GdipFillRectangle(void *graphics, void *brush, float x, float y, float w, float h);

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
#define WM_CAPTURE_PMOUSE (WM_APP + 7) /* this PC's mouse through the Pico: wp WM_*, lp mouseData */
#define WM_UPDATE_RESULT (WM_APP + 8)  /* the start update check ended: wp HW_UPDATE_* */
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
    ID_PROFILE_REMOTE, ID_PROFILE_GAMING, ID_INPUT_APPLY,
    ID_UPDATE_NOW, ID_UPDATE_LATER /* the update banner */
};
/* Retired with the tabbed window, numbers kept: ID_OPTIONS (the Input options
 * window; a posted command now opens the Input tab), ID_ROUTE (its route combo,
 * now ID_PROFILE_REMOTE / ID_PROFILE_GAMING) and ID_COMPACT (Less / More). */

enum { TAB_CONTROL, TAB_CONNECTION, TAB_INPUT, TAB_TOOLS, TAB_COUNT };
static const char *const TAB_NAMES[TAB_COUNT] = {"Control", "Connection", "Input", "Tools"};

/* Logical (96 DPI) window size; the same on every tab: a 208 px rail on the
 * left (state, facts, ARM, Pico on this PC, PANIC) and the tab pane on the
 * right (tab strip, body, one message strip). */
#define LAYOUT_W 700
#define LAYOUT_H 432

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

/* ---- window palette (the Rail design). The badge keeps the C_ colours above. */
#define W_RAIL        RGB(17, 18, 20)
#define W_BG          RGB(23, 24, 27)
#define W_SURF        RGB(31, 33, 37)
#define W_SURF2       RGB(39, 42, 47)
#define W_FIELD       RGB(27, 29, 33)
#define W_LINE        RGB(46, 49, 55)
#define W_LINE2       RGB(59, 63, 70)
#define W_FOCUS       RGB(146, 152, 162)
#define W_TEXT        RGB(236, 237, 239)
#define W_TEXT2       RGB(174, 177, 184)
#define W_TEXT3       RGB(130, 134, 142)
#define W_CHECK       RGB(217, 219, 223)
#define W_GREEN       RGB(63, 185, 80)
#define W_GREEN_F     RGB(24, 51, 31)
#define W_GREEN_BD    RGB(62, 154, 85)
#define W_GREEN_TX    RGB(142, 229, 156)
#define W_ARM         RGB(44, 134, 68)
#define W_ARM_H       RGB(52, 152, 78)
#define W_ARM_P       RGB(36, 112, 57)
#define W_ARM_BD      RGB(69, 167, 94)
#define W_ARM_TX      RGB(244, 255, 246)
#define W_ARM_SUB     RGB(205, 239, 212)
#define W_ARMED_TX    RGB(216, 247, 221)
#define W_ARMED_SUB   RGB(156, 203, 166)
#define W_DIS         RGB(74, 78, 86)
#define W_DIS_H       RGB(88, 93, 102)
#define W_DIS_P       RGB(63, 67, 74)
#define W_DIS_BD      RGB(104, 109, 118)
#define W_DIS_SUB     RGB(201, 204, 210)
#define W_RED         RGB(229, 83, 75)
#define W_RED_F       RGB(59, 23, 25)
#define W_RED_FH      RGB(80, 31, 34)
#define W_RED_FP      RGB(48, 19, 21)
#define W_RED_BD      RGB(180, 66, 61)
#define W_RED_BDH     RGB(214, 84, 78)
#define W_RED_TX      RGB(255, 158, 150)
#define W_AMBER       RGB(217, 162, 58)
#define W_SEG_ON      RGB(58, 62, 70)
#define W_KEY         RGB(46, 49, 55)
#define W_KEY_BD      RGB(69, 73, 81)
#define W_BX_BD       RGB(90, 94, 102)
#define W_BX_DIS_BD   RGB(63, 67, 74)
#define W_BX_DIS_ON   RGB(94, 98, 106)
#define W_MSG_ERR_BG  RGB(34, 24, 26)
#define W_MSG_ERR_TX  RGB(243, 201, 197)
#define W_MSG_WARN_BG RGB(34, 30, 21)
#define W_MSG_WARN_TX RGB(234, 214, 168)
#define W_MSG_OK_TX   RGB(191, 233, 198)
#define W_WARN_LINE   RGB(227, 194, 122)
#define W_UPD         RGB(22, 32, 46)
#define W_UPD_BD      RGB(52, 80, 122)
#define W_UPD_TX      RGB(200, 216, 240)
#define W_UPD_ICO     RGB(127, 168, 230)
#define W_UPD_BTN     RGB(34, 50, 74)
#define W_UPD_BTN_BD  RGB(70, 103, 154)

typedef struct {
    int W, H;
    /* rail: brand, state block, four fact rows, ARM / Pico on this PC / PANIC */
    RECT rail, mark, state, arm, local_pico, panic;
    int title_x, title_cy, fact_x, fact_vx, fact_cy[4], div_y;
    /* pane: tab strip (a child), page, body, the one message strip */
    RECT tabs, page, body, msg;
    /* update banner: the blue bar under the tab strip, and its two buttons */
    RECT banner, banner_now, banner_later;
    /* Control */
    RECT tiles[4], live;
    int head_cy, live_head_cy;
    /* Connection: field frames (address, port, rate, disarm), Save, row positions */
    RECT frame[4], save;
    int conn_head_cy[3], conn_lbl_cy[2], conn_hint_cy, link_cy;
    /* Input: two columns. note is the Physical experimental note (Gaming PC route). */
    RECT seg, profile[2], cb[2], src[3], note, pointer, vr, repeat, repeat_button, repeat_source;
    RECT repeat_cps, click_cps, limited, apply; /* field frames, the last-ARM note, the button */
    int in_route_cy, in_desc_cy, in_relay_cy, in_src_cy, in_ptr_cy;
    int in_cap_cy, in_btn_cy, in_rsrc_cy, in_click_cy, in_cps_right;
    int note_shown;
    /* Tools */
    RECT monitor_btn, replay_btn, updates, history_cb, clear, list;
    int hk_cy, rh_cy;
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
    /* Armed on the Gaming PC route with loopback_physical: this PC's own
     * keys, buttons and wheel go through the Pico (echo credits). Latched
     * at ARM, cleared at DISARM; every code path of that mode checks it. */
    int loop_phys;

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
    int pico_sev;                   /* SEV_* of local_pico_notice */
    unsigned msg_stamp[3], msg_seq; /* which message site spoke last (MSRC_*) */
    int msg_lines;                  /* lines the message strip needs now: 1 or 2 */
    int layout_banner;              /* banner_visible() when the layout was computed */
    int tab_dirty_drawn;            /* g.input_dirty as the tab strip last drew it */
    /* Input tab: edits not applied yet (reset from g.cfg when the tab is
     * entered), and its status / error line. */
    hidway_config_t pending;
    int input_dirty, input_error;
    char input_status[160];
    /* Connection tab: why the last Save was refused or failed (empty: the hint) */
    int conn_error;
    int conn_bad; /* 1-4: the field the last refused Save named (drawn with a red frame), 0 none */
    char conn_status[160];
    /* Pico on this PC indication (read-only, GUI-thread timer) */
    unsigned pico_state;
    int pico_known, pico_pending;
    /* Update banner from the one check per start: update_offer 0 none, 1 offered
     * (shown while disarmed), 2 dismissed with Later or handed to the updater. */
    int update_offer, update_check_started;
    char update_tag[HW_UPDATE_TAG_MAX];
    char update_notice[64]; /* why Update now did not start, in place of the banner text */
    HWND update_now, update_later;
    HFONT f_title, f_sub, f_section, f_label, f_body, f_pill, f_tile, f_mono, f_mono_s,
          f_btn, f_btn_big, f_chip, f_tab, f_timer, f_arm_sub, f_panic, f_fact, f_semi;
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

/* The one message strip: how serious a message is, and which of the three
 * message buffers (Connection status, Input status, Pico notice) spoke. */
enum { SEV_INFO, SEV_OK, SEV_WARN, SEV_ERR };
enum { MSRC_CONN, MSRC_INPUT, MSRC_PICO, MSRC_COUNT };
#define ID_MSG_TIP 0x7101 /* the strip's tooltip: the whole text of a long message */

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
static void round_rect_w(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border, int bw)
{
    void *gr = gp_begin(dc);
    float x = (float)r.left, y = (float)r.top;
    float w = (float)(r.right - r.left), h = (float)(r.bottom - r.top);
    void *outer = path_round(x, y, w, h, (float)rad);
    fill_path(gr, outer, border);
    GdipDeletePath(outer);
    if (border != fill) {
        float t = (float)(bw > 0 ? bw : 1);
        void *inner = path_round(x + t, y + t, w - 2 * t, h - 2 * t, (float)rad - t);
        fill_path(gr, inner, fill);
        GdipDeletePath(inner);
    }
    GdipDeleteGraphics(gr);
}

static void round_rect(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border)
{
    round_rect_w(dc, r, rad, fill, border, S(1) > 0 ? S(1) : 1);
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

/* Echo credits of the physical-through-Pico mode: granted here (GUI thread,
 * from each state actually sent), taken by the hook thread. */
static hidway_echo_t cap_echo;
static int echo_cas(volatile uint32_t *w, uint32_t expected, uint32_t desired)
{
    return (uint32_t)InterlockedCompareExchange((volatile LONG *)w, (LONG)desired, (LONG)expected) == expected;
}
static const char LOOP_PHYS_NO_PICO[] = "Physical through the Pico: the Pico is not on this PC - not armed";

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
        if (g.loop_phys) { /* one credit per edge the Pico will now perform */
            hidway_echo_frame_t sent;
            memset(&sent, 0, sizeof sent);
            sent.mods = p.mods;
            memcpy(sent.keys, p.keys, sizeof sent.keys);
            sent.buttons = p.buttons;
            sent.wheel = p.wheel;
            hidway_echo_grant(&cap_echo, &sent, timeGetTime());
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

/* A Pico notice (the strip shows it with this severity until it is cleared). */
static void pico_notice(const char *message, int sev)
{
    snprintf(g.local_pico_notice, sizeof g.local_pico_notice, "%s", message);
    g.pico_sev = sev;
    g.msg_stamp[MSRC_PICO] = ++g.msg_seq;
    invalidate_readouts();
}

/* Disarm from the physical-through-Pico mode with the reason in the window:
 * the recent input list and LIVE INPUT on the Control tab. */
static void loop_phys_stop(const char *message)
{
    set_armed(0);
    history_add('w', message);
    pico_notice(message, SEV_WARN);
}

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
    if (g.loop_phys && g.armed) {
        int fault = hidway_echo_expire(&cap_echo, timeGetTime());
        if (fault) {
            loop_phys_stop(fault == HIDWAY_ECHO_FAULT_SILENT
                               ? "The Pico did not return the input - auto-disarmed"
                               : "Input loop through the Pico detected - auto-disarmed");
            return;
        }
    }
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
static volatile LONG cap_loop_phys; /* g.loop_phys for the hook thread */
static uint8_t cap_ours[2][768]; /* scan/E0 identity, or VK identity when scan is absent */
static uint8_t cap_key_vk[2][768]; /* retain DOWN VK across layout changes */
static uint8_t cap_mouse_ours;
static POINT cap_mouse_last, cap_warp_point;
static int cap_mouse_last_valid;
static volatile LONG cap_warp_pending;

/* The echo slot of a hardware key: the HID usage (modifiers 0xE0..0xE7)
 * that on_capture_key() sends for it, the same way the Pico's returning key
 * maps; 0 when it sends none. */
static unsigned loop_key_slot(unsigned vk, unsigned sc, int e0)
{
    if (vk == VK_PAUSE) return 0x48;
    if (vk == VK_NUMLOCK) return 0x53;
    if (vk == VK_SNAPSHOT) return 0x46;
    if (vk == VK_RSHIFT) e0 = 0;
    if (!sc || sc > 0xff) return 0;
    uint8_t mod = hidway_sc_to_modifier((uint8_t)sc, e0 != 0);
    if (mod) {
        unsigned bit = 0;
        while (!(mod & (1u << bit))) bit++;
        return 0xE0u + bit;
    }
    return hidway_sc_to_usage((uint8_t)sc, e0 != 0);
}

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
        if (cap_loop_phys && !injected && cap_active && cap_keyboard) {
            /* Gaming PC, this PC's keys through the Pico: a matching echo
             * credit means the Pico's returning key (pass), otherwise it is
             * this PC's own key (the physical path below). */
            unsigned echo_slot = loop_key_slot(vk, k->scanCode, (k->flags & LLKHF_EXTENDED) != 0);
            int verdict = echo_slot ? hidway_echo_input(&cap_echo, echo_slot, up != FALSE, timeGetTime())
                                    : HIDWAY_ECHO_PASS;
            if (verdict == HIDWAY_ECHO_SWALLOW)
                return 1;
            if (verdict == HIDWAY_ECHO_OWN) {
                if (!up) cap_key_vk[0][identity] = vk;
                if (cap_key_vk[0][identity]) vk = cap_key_vk[0][identity];
                cap_ours[0][identity] = (uint8_t)!up;
                LPARAM info = (LPARAM)((k->scanCode & 0xFFFFu) | ((k->flags & LLKHF_EXTENDED) ? KEYF_E0 : 0) |
                                       (up ? KEYF_UP : 0));
                if(!capture_enqueue(cap_badge,WM_CAPTURE_READY,WM_CAPTURE_KEY,(WPARAM)vk,info))
                    InterlockedExchange(&cap_active,0);
                return 1;
            }
            return CallNextHookEx(NULL, code, wp, lp);
        }
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
        } else if (!injected && cap_loop_phys && cap_active && cap_mouse) {
            /* Gaming PC, this PC's buttons and wheel through the Pico. The
             * pointer, the horizontal wheel and every echo stay native. */
            unsigned echo_slot = 0;
            bool up = false;
            int verdict = HIDWAY_ECHO_PASS;
            switch (wp) {
            case WM_LBUTTONUP: up = true; /* fall through */
            case WM_LBUTTONDOWN: echo_slot = HIDWAY_ECHO_BUTTON0; break;
            case WM_RBUTTONUP: up = true; /* fall through */
            case WM_RBUTTONDOWN: echo_slot = HIDWAY_ECHO_BUTTON0 + 1; break;
            case WM_MBUTTONUP: up = true; /* fall through */
            case WM_MBUTTONDOWN: echo_slot = HIDWAY_ECHO_BUTTON0 + 2; break;
            case WM_XBUTTONUP: up = true; /* fall through */
            case WM_XBUTTONDOWN:
                echo_slot = HIDWAY_ECHO_BUTTON0 + (HIWORD(m->mouseData) == XBUTTON1 ? 3 : 4);
                break;
            case WM_MOUSEWHEEL:
                verdict = hidway_echo_wheel(&cap_echo, (SHORT)HIWORD(m->mouseData), timeGetTime());
                break;
            }
            if (echo_slot)
                verdict = hidway_echo_input(&cap_echo, echo_slot, up, timeGetTime());
            if (verdict == HIDWAY_ECHO_SWALLOW)
                return 1;
            if (verdict == HIDWAY_ECHO_OWN) {
                if(!capture_enqueue(cap_badge,WM_CAPTURE_READY,WM_CAPTURE_PMOUSE,wp,(LPARAM)m->mouseData))
                    InterlockedExchange(&cap_active,0);
                return 1;
            }
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
    InterlockedExchange(&cap_loop_phys, g.loop_phys);
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
    InterlockedExchange(&cap_loop_phys, 0);
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
    /* Physical keys through the Pico swallow this PC's own keyboard and get
     * it back from the Pico: refuse unless the Pico is really on this PC. */
    int loop_phys = on && !replay.active && g.cfg.loopback_mode && g.cfg.loopback_physical;
    if(loop_phys&&hidway_local_pico_interfaces(g.cfg.loopback_device)!=HIDWAY_PICO_READY) {
        history_add('w',LOOP_PHYS_NO_PICO);
        pico_notice(LOOP_PHYS_NO_PICO,SEV_WARN);
        badge_refresh();return;
    }
    if(on) {
        uint32_t session=0;
        if(hidway_random((uint8_t*)&session,sizeof session)!=0) {
            history_add('w',"Could not create input session - not armed");return;
        }
        if(session==g.session_id)session^=0x80000000u;
        g.session_id=session;g.seq=0;memset(&g.status_order,0,sizeof g.status_order);
        hidway_echo_reset(&cap_echo,echo_cas);
        g.loop_phys=loop_phys;
        if(!strcmp(g.local_pico_notice,LOOP_PHYS_NO_PICO))g.local_pico_notice[0]=0; /* no longer true */
    }
    if (on && !g.preview && !replay.active && !capture_start()) {
        g.loop_phys=0;InterlockedExchange(&cap_loop_phys,0);
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
        g.loop_phys = 0; /* every credit and ownership goes with the capture */
        hidway_echo_reset(&cap_echo, echo_cas);
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
    int accepted=(input_source_mask()&source)||(g.loop_phys&&source==HIDWAY_SOURCE_PHYSICAL);
    if(!accepted||vk>255)return;
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

/* A mouse event from the hook: injected (Virtual), or this PC's own buttons
 * and wheel in the physical-through-Pico mode (never pointer movement). */
static void capture_mouse_event(unsigned source, UINT msg, LPARAM data)
{
    switch (msg) {
    case WM_MOUSEMOVE:
        g.cum_x += (SHORT)LOWORD(data);
        g.cum_y += (SHORT)HIWORD(data);
        break;
    case WM_LBUTTONDOWN: mouse_edge(source,RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_DOWN, 0, BTN_L, "Left click"); break;
    case WM_LBUTTONUP: mouse_edge(source,RI_MOUSE_LEFT_BUTTON_UP, 0, RI_MOUSE_LEFT_BUTTON_UP, BTN_L, "Left click"); break;
    case WM_RBUTTONDOWN: mouse_edge(source,RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN, 0, BTN_R, "Right click"); break;
    case WM_RBUTTONUP: mouse_edge(source,RI_MOUSE_RIGHT_BUTTON_UP, 0, RI_MOUSE_RIGHT_BUTTON_UP, BTN_R, "Right click"); break;
    case WM_MBUTTONDOWN: mouse_edge(source,RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_DOWN, 0, BTN_M, "Middle click"); break;
    case WM_MBUTTONUP: mouse_edge(source,RI_MOUSE_MIDDLE_BUTTON_UP, 0, RI_MOUSE_MIDDLE_BUTTON_UP, BTN_M, "Middle click"); break;
    case WM_XBUTTONDOWN:
        if (HIWORD((DWORD)data) == XBUTTON1) mouse_edge(source,RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_DOWN, 0, BTN_X1, "Mouse back");
        else mouse_edge(source,RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_DOWN, 0, BTN_X2, "Mouse forward");
        break;
    case WM_XBUTTONUP:
        if (HIWORD((DWORD)data) == XBUTTON1) mouse_edge(source,RI_MOUSE_BUTTON_4_UP, 0, RI_MOUSE_BUTTON_4_UP, BTN_X1, "Mouse back");
        else mouse_edge(source,RI_MOUSE_BUTTON_5_UP, 0, RI_MOUSE_BUTTON_5_UP, BTN_X2, "Mouse forward");
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

static void on_capture_mouse(UINT msg, LPARAM data)
{
    if (!g.armed || replay.active || !g.cfg.relay_mouse || !g.cfg.source_virtual)
        return;
    capture_mouse_event(HIDWAY_SOURCE_VIRTUAL, msg, data);
}

/* This PC's own buttons and wheel, consumed by the hook (Gaming PC route,
 * physical through the Pico). Raw Input never adds them a second time. */
static void on_capture_physical_mouse(UINT msg, LPARAM data)
{
    if (!g.armed || replay.active || !g.cfg.relay_mouse || !g.loop_phys || msg == WM_MOUSEMOVE)
        return;
    capture_mouse_event(HIDWAY_SOURCE_PHYSICAL, msg, data);
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
        else if(e.kind==WM_CAPTURE_PMOUSE)on_capture_physical_mouse((UINT)e.w,e.l);
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

/* The update banner shows only while disarmed and not replaying. */
static int banner_visible(void)
{
    return g.update_offer == 1 && !g.armed && !replay.active;
}

/* One window size for every tab (logical px at 96 DPI):
 *   rail   x 0-208   brand, state block, four facts, ARM, Pico on this PC,
 *                    a divider and PANIC alone at the bottom
 *   pane   x 208-700 tab strip (y 0-44), update banner (y 52-82, only while
 *                    shown; the body then starts 38 px lower), body, and the
 *                    one message strip at the foot (34 high, 46 for two
 *                    lines, which takes the 12 px from the body).
 * Everything is derived from these numbers; apply_layout() moves the child
 * controls to what is computed here. */
static void compute_layout(void)
{
    layout_t *L = &g.L;
    const int W = LAYOUT_W, H = LAYOUT_H, RAIL = 208, X0 = 228, X1 = 680, X2 = 523;
    L->W = S(W);
    L->H = S(H);

    L->rail = R(0, 0, RAIL, H);
    L->mark = R(16, 15, 42, 41);
    L->title_x = S(51);
    L->title_cy = S(28);
    L->state = R(16, 56, 192, 120);
    L->fact_x = S(16);
    L->fact_vx = S(78);
    for (int i = 0; i < 4; i++)
        L->fact_cy[i] = S(144 + 28 * i);
    L->arm = R(16, 261, 192, 319);
    L->local_pico = R(16, 327, 192, 361);
    L->div_y = S(373);
    L->panic = R(16, 384, 192, 420);

    L->tabs = R(RAIL, 0, W, 44);
    L->page = R(RAIL, 44, W, H);

    int banner = banner_visible();
    g.layout_banner = banner;
    int top = g.msg_lines == 2 ? 374 : 386;
    L->msg = R(X0, top, X1, 420);
    const int Y = 58 + (banner ? 38 : 0), yb = top - 10;
    L->body = R(X0, Y, X1, yb);

    /* Update banner: the blue bar under the tab strip; its buttons follow its
     * right edge (change the bar and they move with it). */
    L->banner = R(X0, 52, X1, 82);
    L->banner_later = R(X1 - 6 - 52, 55, X1 - 6, 79);
    L->banner_now = R(X1 - 6 - 52 - 9 - 88, 55, X1 - 6 - 52 - 9, 79);

    /* Control: section head, four tiles (the last gets the rest, so WAKE
     * clears the label and the value), LIVE INPUT head and panel. Where the
     * banner and a second message line take height, the gaps shrink. */
    int tight = (yb - Y) < 290;
    L->head_cy = S(Y + 10);
    int ty = Y + (tight ? 26 : 28), x = X0;
    for (int i = 0; i < 4; i++) {
        int w = i < 3 ? 96 : X1 - x;
        L->tiles[i] = R(x, ty, x + w, ty + 58);
        x += w + 8;
    }
    int ht = ty + 58 + (tight ? 10 : 14), hh = tight ? 18 : 20;
    L->live_head_cy = S(ht + hh / 2);
    L->live = R(X0, ht + hh + (tight ? 6 : 8), X1, yb);

    /* Connection */
    L->conn_head_cy[0] = S(Y + 10);
    L->conn_lbl_cy[0] = S(Y + 31);
    L->frame[0] = R(X0, Y + 40, X0 + 250, Y + 68);
    L->frame[1] = R(X0 + 260, Y + 40, X0 + 344, Y + 68);
    L->conn_head_cy[1] = S(Y + 88);
    L->conn_lbl_cy[1] = S(Y + 109);
    L->frame[2] = R(X0, Y + 118, X0 + 120, Y + 146);
    L->frame[3] = R(X0 + 130, Y + 118, X0 + 250, Y + 146);
    L->save = R(X1 - 72, Y + 118, X1, Y + 146);
    L->conn_hint_cy = S(Y + 159);
    L->conn_head_cy[2] = S(Y + 186);
    L->link_cy = S(Y + 212); /* first of three rows, 20 apart */

    /* Input: left column 254 wide, a vertical rule at x 502, right column
     * x 523-680. Combo rectangles include their drop-down list height. The
     * Physical note (Gaming PC route, Physical ticked) takes two lines under
     * the Physical box and moves the rows below it down. */
    L->in_route_cy = S(Y + 7);
    L->seg = R(X0, Y + 16, X0 + 254, Y + 44);
    L->profile[0] = R(X0 + 2, Y + 18, X0 + 126, Y + 42);
    L->profile[1] = R(X0 + 128, Y + 18, X0 + 252, Y + 42);
    L->in_desc_cy = S(Y + 55);
    L->in_relay_cy = S(Y + 75);
    L->cb[0] = R(X0, Y + 84, X0 + 96, Y + 108);
    L->cb[1] = R(X0 + 108, Y + 84, X0 + 198, Y + 108);
    L->in_src_cy = S(Y + 121);
    L->src[0] = R(X0, Y + 130, X0 + 170, Y + 154);
    int note = g.cfg.loopback_mode && g.pending.loopback_physical;
    L->note_shown = note;
    memset(&L->note, 0, sizeof L->note);
    if (note)
        L->note = R(X0 + 24, Y + 156, X0 + 254, Y + 190);
    int sy = Y + (note ? 192 : 156);
    L->src[1] = R(X0, sy, X0 + 104, sy + 24);
    L->src[2] = R(X0 + 116, sy, X0 + 254, sy + 24);
    L->in_ptr_cy = S(sy + 35);
    L->pointer = R(X0, sy + 44, X0 + 254, sy + 44 + 28 + 2 * 24 + 6);
    L->vr = R(502, Y, 503, Y + 264);
    L->in_cap_cy = S(Y + 7);
    L->repeat = R(X2, Y + 16, X2 + 157, Y + 40);
    L->in_btn_cy = S(Y + 53);
    L->in_cps_right = S(X1);
    L->repeat_button = R(X2, Y + 62, X2 + 85, Y + 62 + 28 + 5 * 24 + 6);
    L->repeat_cps = R(X2 + 93, Y + 62, X2 + 157, Y + 90);
    L->in_rsrc_cy = S(Y + 103);
    L->repeat_source = R(X2, Y + 112, X2 + 157, Y + 112 + 28 + 7 * 24 + 6);
    L->in_click_cy = S(Y + 153);
    L->click_cps = R(X2, Y + 162, X2 + 56, Y + 190);
    L->limited = R(X2, Y + 194, X1, Y + 228);
    L->apply = R(X1 - 120, Y + 236, X1, Y + 264);

    /* Tools: three equal tool buttons, the hotkey keycaps, the Recent input
     * head with History and Clear, and the list down to the message strip. */
    L->monitor_btn = R(X0, Y, 373, Y + 54);
    L->replay_btn = R(381, Y, 527, Y + 54);
    L->updates = R(535, Y, X1, Y + 54);
    L->hk_cy = S(Y + 78);
    L->rh_cy = S(Y + 113);
    L->clear = R(X1 - 56, Y + 101, X1, Y + 125);
    L->history_cb = R(X1 - 56 - 6 - 84, Y + 101, X1 - 56 - 6, Y + 125);
    L->list = R(X0, Y + 132, X1, yb);
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
 * windows are skipped by the dialog keyboard navigation. The rail's buttons
 * and the tab strip are never hidden. */
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

    place(g.arm, L->arm);
    place(g.local_pico_button, L->local_pico);
    place(g.panic, L->panic);
    place(g.tabs, L->tabs);
    place(g.update_now, L->banner_now);
    place(g.update_later, L->banner_later);

    place(g.save, L->save);
    for (int i = 0; i < 4; i++)
        place_field(g.edit[i], L->frame[i], eh);

    HWND combos[] = {g.pointer_route, g.repeat_button, g.repeat_source};
    for (size_t i = 0; i < sizeof combos / sizeof combos[0]; i++) {
        if (!combos[i])
            continue;
        SendMessageA(combos[i], WM_SETFONT, (WPARAM)g.f_body, FALSE);
        SendMessageA(combos[i], CB_SETITEMHEIGHT, (WPARAM)-1, S(24)); /* the closed box: 28 with its edge */
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
    if (g.tooltip) { /* the strip's tooltip follows the strip */
        TTTOOLINFOA ti = {0};
        ti.cbSize = TTTOOLINFOA_V2_SIZE;
        ti.hwnd = g.hwnd;
        ti.uId = ID_MSG_TIP;
        ti.rect = L->msg;
        SendMessageA(g.tooltip, TTM_NEWTOOLRECTA, 0, (LPARAM)&ti);
    }
}

/* Compute again and move every control: after something that changes the
 * body (the banner, a second message line, the Physical note). */
static void relayout(void)
{
    compute_layout();
    apply_layout();
    if (g.hwnd)
        RedrawWindow(g.hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

/* Everything except the child controls repaints; the timer calls this every
 * 50 ms. Child controls are clipped and repaint themselves. */
static void invalidate_readouts(void)
{
    if (!g.hwnd)
        return;
    RECT rc = {0, 0, g.L.W, g.L.H};
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

/* Cascadia Mono where installed, Consolas otherwise (GDI would substitute
 * something proportional for a missing face). */
static HFONT mkmono(int tenths_px, int weight)
{
    HFONT f = mkfont(tenths_px, weight, "Cascadia Mono");
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, f);
    char face[LF_FACESIZE] = {0};
    GetTextFaceA(dc, LF_FACESIZE, face);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    if (lstrcmpiA(face, "Cascadia Mono") != 0) {
        DeleteObject(f);
        f = mkfont(tenths_px, weight, "Consolas");
    }
    return f;
}

static void destroy_fonts(void)
{
    HFONT *fs[] = {&g.f_title, &g.f_sub, &g.f_section, &g.f_label, &g.f_body, &g.f_pill,
                   &g.f_tile, &g.f_mono, &g.f_mono_s, &g.f_btn, &g.f_btn_big, &g.f_chip,
                   &g.f_tab, &g.f_timer, &g.f_arm_sub, &g.f_panic, &g.f_fact, &g.f_semi};
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++)
        if (*fs[i]) {
            DeleteObject(*fs[i]);
            *fs[i] = NULL;
        }
}

/* Sizes in tenths of a px: body 130, labels 120, captions 115, caps labels
 * 110 semibold; tabs 130 semibold; brand 150; state word 170; timer 160 mono;
 * ARM 190 bold with a 110 hotkey line; PANIC 140 bold; tile values 180 mono. */
static void create_fonts(void)
{
    const char *txt = "Segoe UI";
    destroy_fonts();
    g.f_title = mkfont(150, FW_SEMIBOLD, txt);
    g.f_sub = mkfont(115, FW_NORMAL, txt);
    g.f_section = mkfont(110, FW_SEMIBOLD, txt);
    g.f_label = mkfont(120, FW_NORMAL, txt);
    g.f_body = mkfont(130, FW_NORMAL, txt);
    g.f_pill = mkfont(170, FW_SEMIBOLD, txt);
    g.f_tile = mkmono(180, FW_SEMIBOLD);
    g.f_mono = mkmono(120, FW_NORMAL);
    g.f_mono_s = mkmono(115, FW_NORMAL);
    g.f_btn = mkfont(125, FW_SEMIBOLD, txt);
    g.f_btn_big = mkfont(190, FW_BOLD, txt);
    g.f_chip = mkfont(110, FW_SEMIBOLD, txt);
    g.f_tab = mkfont(130, FW_SEMIBOLD, txt);
    g.f_timer = mkmono(160, FW_SEMIBOLD);
    g.f_arm_sub = mkfont(110, FW_NORMAL, txt);
    g.f_panic = mkfont(140, FW_BOLD, txt);
    g.f_fact = mkfont(125, FW_NORMAL, txt);
    g.f_semi = mkfont(130, FW_SEMIBOLD, txt);
}

static void load_mark_icon(void)
{
    if (g.mark_icon)
        DestroyIcon(g.mark_icon);
    int sz = g.L.mark.right - g.L.mark.left;
    g.mark_icon = (HICON)LoadImageA(g.inst, MAKEINTRESOURCEA(1), IMAGE_ICON, sz, sz, 0);
}

/* ======================================================================== */
/*  Glyphs (anti-aliased with GDI+). Colour never carries a state alone:     */
/*  hollow circle = off / idle, filled dot = good, ring = armed, triangle =  */
/*  problem, half circle = partial, octagon = PANIC.                         */
/* ======================================================================== */

enum { G_HOLLOW, G_HOLLOW_AMBER, G_FILL, G_RING, G_TRI, G_TRI_AMBER, G_HALF, G_OCT, G_CHECK,
       G_INFO, G_UPG };

static void gp_pen(void **pen, COLORREF c, float w)
{
    GdipCreatePen1(argb(c), w, 2 /* UnitPixel */, pen);
    GdipSetPenStartCap(*pen, 2);
    GdipSetPenEndCap(*pen, 2);
    GdipSetPenLineJoin(*pen, 2);
}

static void gp_fill_ellipse(void *gr, COLORREF c, float x, float y, float w, float h)
{
    void *b = NULL;
    GdipCreateSolidFill(argb(c), &b);
    GdipFillEllipse(gr, b, x, y, w, h);
    GdipDeleteBrush(b);
}

static void gp_fill_rect(void *gr, COLORREF c, float x, float y, float w, float h)
{
    void *b = NULL;
    GdipCreateSolidFill(argb(c), &b);
    GdipFillRectangle(gr, b, x, y, w, h);
    GdipDeleteBrush(b);
}

/* A glyph centred on (cx, cy); `bg` is what lies behind it (the triangle's
 * and the octagon's bar are cut out in that colour). */
static void glyph(HDC dc, int kind, int cx, int cy, COLORREF bg)
{
    float u = (float)g.dpi / 96.0f, x = (float)cx, y = (float)cy;
    void *gr = gp_begin(dc), *pen = NULL;
    switch (kind) {
    case G_HOLLOW:
    case G_HOLLOW_AMBER:
        gp_pen(&pen, kind == G_HOLLOW ? W_TEXT3 : W_AMBER, 2 * u);
        GdipDrawEllipse(gr, pen, x - 4 * u, y - 4 * u, 8 * u, 8 * u);
        break;
    case G_FILL:
        gp_fill_ellipse(gr, W_GREEN, x - 5 * u, y - 5 * u, 10 * u, 10 * u);
        break;
    case G_RING:
        gp_pen(&pen, W_GREEN, 2 * u);
        GdipDrawEllipse(gr, pen, x - 6 * u, y - 6 * u, 12 * u, 12 * u);
        gp_fill_ellipse(gr, W_GREEN, x - 3 * u, y - 3 * u, 6 * u, 6 * u);
        break;
    case G_TRI:
    case G_TRI_AMBER: {
        HW_PointF p[3] = {{x, y - 6 * u}, {x + 7 * u, y + 6 * u}, {x - 7 * u, y + 6 * u}};
        void *b = NULL;
        GdipCreateSolidFill(argb(kind == G_TRI ? W_RED : W_AMBER), &b);
        GdipFillPolygon(gr, b, p, 3, 0);
        GdipDeleteBrush(b);
        gp_fill_rect(gr, bg, x - 1 * u, y - 2 * u, 2 * u, 5 * u); /* the bar */
        break;
    }
    case G_HALF: {
        void *b = NULL;
        gp_pen(&pen, W_AMBER, 2 * u);
        GdipDrawEllipse(gr, pen, x - 4 * u, y - 4 * u, 8 * u, 8 * u);
        GdipCreateSolidFill(argb(W_AMBER), &b);
        GdipFillPie(gr, b, x - 5 * u, y - 5 * u, 10 * u, 10 * u, 90.0f, 180.0f);
        GdipDeleteBrush(b);
        break;
    }
    case G_OCT: {
        float l = x - 7 * u, t = y - 7 * u, a = 4.2f * u, b2 = 9.8f * u, e = 14 * u;
        HW_PointF p[8] = {{l + a, t}, {l + b2, t}, {l + e, t + a}, {l + e, t + b2},
                          {l + b2, t + e}, {l + a, t + e}, {l, t + b2}, {l, t + a}};
        void *b = NULL;
        GdipCreateSolidFill(argb(W_RED_TX), &b);
        GdipFillPolygon(gr, b, p, 8, 0);
        GdipDeleteBrush(b);
        gp_fill_rect(gr, bg, l + 3 * u, t + 6 * u, 8 * u, 2 * u); /* the bar */
        break;
    }
    case G_CHECK: {
        HW_PointF p[3] = {{x - 4.5f * u, y + 0.5f * u}, {x - 1.5f * u, y + 3.5f * u}, {x + 4.5f * u, y - 3.5f * u}};
        gp_pen(&pen, W_GREEN, 2 * u);
        GdipDrawLines(gr, pen, p, 3);
        break;
    }
    case G_INFO:
        gp_pen(&pen, W_TEXT2, 1.5f * u);
        GdipDrawEllipse(gr, pen, x - 5.25f * u, y - 5.25f * u, 10.5f * u, 10.5f * u);
        gp_fill_rect(gr, W_TEXT2, x - 0.75f * u, y - 0.5f * u, 1.5f * u, 4 * u);
        gp_fill_ellipse(gr, W_TEXT2, x - 0.9f * u, y - 3.4f * u, 1.8f * u, 1.8f * u);
        break;
    case G_UPG: {
        HW_PointF stem[2] = {{x, y - 4.5f * u}, {x, y + 3.5f * u}};
        HW_PointF head[3] = {{x - 3.2f * u, y + 0.4f * u}, {x, y + 3.6f * u}, {x + 3.2f * u, y + 0.4f * u}};
        gp_fill_ellipse(gr, W_UPD_ICO, x - 8 * u, y - 8 * u, 16 * u, 16 * u);
        gp_pen(&pen, W_UPD, 2 * u);
        GdipDrawLines(gr, pen, stem, 2);
        GdipDrawLines(gr, pen, head, 3);
        break;
    }
    }
    if (pen)
        GdipDeletePen(pen);
    GdipDeleteGraphics(gr);
}

/* Radio dot of the route selector: filled when selected, a ring otherwise. */
static void radio(HDC dc, int cx, int cy, int on, COLORREF c)
{
    float u = (float)g.dpi / 96.0f;
    void *gr = gp_begin(dc), *pen = NULL;
    if (on) {
        gp_fill_ellipse(gr, c, (float)cx - 4 * u, (float)cy - 4 * u, 8 * u, 8 * u);
    } else {
        gp_pen(&pen, c, 1.5f * u);
        GdipDrawEllipse(gr, pen, (float)cx - 3.25f * u, (float)cy - 3.25f * u, 6.5f * u, 6.5f * u);
        GdipDeletePen(pen);
    }
    GdipDeleteGraphics(gr);
}

/* ======================================================================== */
/*  The one message strip                                                   */
/* ======================================================================== */

/* Every message the window shows lives in one of three buffers that exist
 * (and are tested) as before: the Connection status line, the Input status
 * line and the Pico notice. The strip shows the one that spoke last, with
 * the severity its writer gave it, for as long as that buffer is not empty;
 * when none is, the hotkey warning, the Input tab's own hint, the armed line
 * or the idle line. Nothing here changes when a message appears or goes. */
static void msg_touch(int src)
{
    g.msg_stamp[src] = ++g.msg_seq;
}

static void hotkey_msg(char *out, size_t cap)
{
    char hk[48];
    hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
    snprintf(out, cap, "Toggle hotkey %s is in use — set toggle_hotkey in hidway.ini", hk);
}

/* The text and severity the strip shows now. */
static void msg_current(char *out, size_t cap, int *sev)
{
    /* the Pico notice was shown by LIVE INPUT only while disarmed: not while ARMED */
    const char *t[MSRC_COUNT] = {g.conn_status, g.input_status, g.armed ? "" : g.local_pico_notice};
    /* Each message was shown on one tab before; on that tab it is shown first
     * (the hotkey warning was on Tools), on the others the latest one. */
    int home = g.tab == TAB_CONNECTION ? MSRC_CONN : g.tab == TAB_INPUT ? MSRC_INPUT
             : g.tab == TAB_CONTROL ? MSRC_PICO : -1;
    int src = home >= 0 && t[home][0] ? home : -1;
    if (src < 0 && g.tab == TAB_TOOLS && !g.toggle_hk_ok) {
        hotkey_msg(out, cap);
        *sev = SEV_WARN;
        return;
    }
    if (src < 0)
        for (int i = 0; i < MSRC_COUNT; i++)
            if (t[i][0] && (src < 0 || g.msg_stamp[i] >= g.msg_stamp[src]))
                src = i;
    if (src >= 0) {
        snprintf(out, cap, "%s", t[src]);
        *sev = src == MSRC_CONN ? (g.conn_error ? SEV_ERR : SEV_INFO)
             : src == MSRC_INPUT ? (g.input_error ? SEV_ERR : SEV_OK)
             : g.pico_sev;
        return;
    }
    if (!g.toggle_hk_ok) {
        hotkey_msg(out, cap);
        *sev = SEV_WARN;
        return;
    }
    if (g.tab == TAB_INPUT) { /* what the Input tab used to say under its fields */
        int loop = g.cfg.loopback_mode != 0, through_pico = loop && g.pending.loopback_physical;
        *sev = g.input_dirty ? SEV_WARN : SEV_INFO;
        snprintf(out, cap, "%s",
                 g.input_dirty ? (through_pico ? "Changes not applied yet. Programs that read Raw Input still see "
                                                 "this PC's own keyboard and mouse too."
                                               : "Changes not applied yet. Apply and save disarms first, then saves.")
                 : through_pico ? "Programs that read Raw Input still see this PC's own keyboard and mouse too. "
                                  "The pointer stays native. Apply and save disarms first."
                 : loop ? "Gaming PC: virtual input only; Physical and Touchpad stay local. Apply and "
                          "save disarms first; changes not applied are dropped when you leave this tab."
                        : "Apply and save disarms first; changes not applied are dropped when you leave "
                          "this tab. Hold to repeat replaces dragging for its button.");
        return;
    }
    if (g.armed) {
        char hk[48];
        hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
        snprintf(out, cap, "Armed. Toggle with %s. PANIC releases every key and button.", hk);
        *sev = SEV_OK;
        return;
    }
    snprintf(out, cap, "Ready. Nothing is sent until you press ARM.");
    *sev = SEV_INFO;
}

static int msg_text_w(void)
{
    return (g.L.msg.right - g.L.msg.left) - S(45); /* bar, padding, glyph, gap, padding */
}

/* Lines `s` needs at `width` (word wrap, explicit breaks count). */
static int text_lines(HDC dc, HFONT f, const char *s, int width)
{
    WCHAR w[512];
    RECT r = {0, 0, width, 0};
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 512))
        w[0] = 0;
    SelectObject(dc, f);
    DrawTextW(dc, w, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int h = font_h(dc, f);
    return h > 0 ? (r.bottom - r.top + h - 1) / h : 1;
}

/* One line or two: the strip grows by 12 px at the body's expense. Called
 * from the 50 ms timer and whenever the tab changes, so it follows messages
 * without any writer having to know about layout. Also moves the rows of the
 * Input tab when the Physical note appears. */
static void msg_sync(void)
{
    if (!g.hwnd)
        return;
    char text[256];
    int sev;
    msg_current(text, sizeof text, &sev);
    HDC dc = GetDC(g.hwnd);
    int lines = text_lines(dc, g.f_fact, text, msg_text_w()) >= 2 ? 2 : 1;
    ReleaseDC(g.hwnd, dc);
    int note = g.cfg.loopback_mode && g.pending.loopback_physical;
    if (lines != (g.msg_lines == 2 ? 2 : 1) || note != g.L.note_shown) {
        g.msg_lines = lines;
        relayout();
    }
}

/* A message that needs more than two lines ends in an ellipsis in the strip;
 * its whole text is then the strip's tooltip (empty when it fits). */
static int msg_tip_text(char *out, size_t cap)
{
    int sev;
    out[0] = 0;
    if (!g.hwnd)
        return 0;
    msg_current(out, cap, &sev);
    HDC dc = GetDC(g.hwnd);
    int over = text_lines(dc, g.f_fact, out, msg_text_w()) > 2;
    ReleaseDC(g.hwnd, dc);
    if (!over)
        out[0] = 0;
    return over;
}

static void text_wrap(HDC dc, HFONT f, COLORREF c, RECT r, UINT fmt, const char *s)
{
    WCHAR w[512];
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 512))
        w[0] = 0;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, w, -1, &r, fmt | DT_NOPREFIX);
}

static void paint_msg(HDC dc)
{
    char text[256];
    int sev;
    msg_current(text, sizeof text, &sev);
    RECT m = g.L.msg;
    COLORREF bg = sev == SEV_ERR ? W_MSG_ERR_BG : sev == SEV_WARN ? W_MSG_WARN_BG : W_SURF;
    COLORREF bar = sev == SEV_ERR ? W_RED : sev == SEV_WARN ? W_AMBER : sev == SEV_OK ? W_GREEN : W_LINE2;
    COLORREF tx = sev == SEV_ERR ? W_MSG_ERR_TX : sev == SEV_WARN ? W_MSG_WARN_TX
                : sev == SEV_OK ? W_MSG_OK_TX : W_TEXT2;
    round_rect(dc, m, S(6), bg, W_LINE);
    RECT b = {m.left, m.top + S(3), m.left + S(3), m.bottom - S(3)};
    round_rect(dc, b, S(1), bar, bar);
    int cy = (m.top + m.bottom) / 2;
    glyph(dc, sev == SEV_ERR ? G_TRI : sev == SEV_WARN ? G_TRI_AMBER : sev == SEV_OK ? G_CHECK : G_INFO,
          m.left + S(3 + 11 + 6), cy, bg);
    int tw = msg_text_w(), th = text_lines(dc, g.f_fact, text, tw) * font_h(dc, g.f_fact);
    int max_h = 2 * font_h(dc, g.f_fact);
    if (th > max_h)
        th = max_h;
    RECT t = {m.left + S(35), cy - th / 2, m.left + S(35) + tw, cy - th / 2 + th};
    text_wrap(dc, g.f_fact, tx, t, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS, text);
}

/* ======================================================================== */
/*  Painting                                                                */
/* ======================================================================== */

/* ---- update banner ------------------------------------------------------
 * One read-only check per start (update_check.c); when it finds a newer
 * stable release, a blue bar under the tab strip offers Update now / Later.
 * It is hidden while armed or replaying and the body starts 38 px lower
 * while it shows. Its layout is L->banner*, its painting paint_banner(), its
 * buttons draw_button(). */

static int control_shown(HWND h)
{
    return h && (GetWindowLongPtrA(h, GWL_STYLE) & WS_VISIBLE) != 0;
}

/* Show or hide the banner buttons to match banner_visible(), and move the
 * body when that changes. Called from the UI timer, so arming hides them
 * within one tick; the painted text follows banner_visible() at once. The
 * focus is moved only while disarmed: while armed the badge may own it. */
static void banner_sync(void)
{
    int on = banner_visible(), changed = 0;
    HWND focus = GetFocus(), b[2] = {g.update_now, g.update_later};
    for (int i = 0; i < 2; i++) {
        if (!b[i] || control_shown(b[i]) == on)
            continue;
        if (!on && focus == b[i] && !g.armed)
            SetFocus(g.tabs ? g.tabs : g.arm);
        ShowWindow(b[i], on ? SW_SHOWNA : SW_HIDE);
        changed = 1;
    }
    if (g.hwnd && on != g.layout_banner)
        relayout();
    else if (changed && g.hwnd)
        InvalidateRect(g.hwnd, &g.L.banner, FALSE);
}

static void paint_banner(HDC dc)
{
    layout_t *L = &g.L;
    round_rect(dc, L->banner, S(6), W_UPD, W_UPD_BD);
    glyph(dc, G_UPG, L->banner.left + S(10 + 8), (L->banner.top + L->banner.bottom) / 2, W_UPD);
    RECT t = L->banner;
    t.left += S(10 + 16 + 9);
    t.right = L->banner_now.left - S(8);
    char text[96];
    if (g.update_notice[0])
        snprintf(text, sizeof text, "%s", g.update_notice);
    else
        snprintf(text, sizeof text, "HIDway %s is available", g.update_tag);
    text_in(dc, g.f_btn, g.update_notice[0] ? W_RED_TX : W_UPD_TX, t,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX, text);
}

/* The result of the start check (wParam of WM_UPDATE_RESULT). Only a newer
 * stable release with a well-formed tag shows the banner; a failed check adds
 * one quiet line to the recent list; anything else says nothing. */
static void update_result(int code, const char *tag)
{
    if (code == HW_UPDATE_AVAILABLE && !g.update_offer && hw_update_tag_valid(tag) &&
        strlen(tag) < sizeof g.update_tag) {
        strcpy(g.update_tag, tag);
        g.update_notice[0] = 0;
        g.update_offer = 1;
        banner_sync();
        if (g.hwnd)
            InvalidateRect(g.hwnd, &g.L.banner, FALSE);
    } else if (code == HW_UPDATE_FAILED) {
        history_add('i', "Update check did not finish - Tools > Updates checks again");
    }
}

/* Later: hidden until the next start (nothing is remembered). */
static void update_later(void)
{
    if (g.update_offer != 1)
        return;
    g.update_offer = 2;
    banner_sync();
}

/* Update now: only while disarmed and not replaying. Starts the updater
 * (HIDway-Setup.exe --update-now), which waits for this window to close,
 * then checks, downloads and installs and opens HIDway again, disarmed. This
 * window closes the normal way, so the settings are saved as on any exit. */
static void update_now(void)
{
    if (g.update_offer != 1)
        return;
    if (g.armed || replay.active) {
        history_add('i', "Update now: DISARM first");
        return;
    }
    if (g.preview) {
        snprintf(g.update_notice, sizeof g.update_notice, "Preview: the updater is not started");
    } else if (hw_companion_open(g.hwnd, HW_COMPANION_UPDATE_NOW) != HW_COMPANION_OK) {
        snprintf(g.update_notice, sizeof g.update_notice, "The updater did not start");
        history_add('w', "Update now: HIDway-Setup.exe did not start - HIDway stays open");
    } else {
        g.update_offer = 2;
        banner_sync();
        PostMessageA(g.hwnd, WM_CLOSE, 0, 0);
        return;
    }
    if (g.hwnd)
        InvalidateRect(g.hwnd, &g.L.banner, FALSE);
}

/* Once per start, after the window is up: never in previews, screenshots or
 * a start that loads a recording, and not with update_check = 0. The check
 * itself runs on a worker thread and needs the updater scripts beside the
 * executable. */
static void update_check_start(void)
{
    if (g.preview || g.shot_path[0] || replay.path[0] || replay.active || !g.cfg.update_check ||
        g.update_check_started)
        return;
    g.update_check_started = 1;
    hw_update_check_begin(g.hwnd, WM_UPDATE_RESULT);
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

/* Relay status: the full text (Control head, Connection), the short one for
 * the rail, a glyph and a text colour. */
static const char *relay_status(const char **short_text, int *kind, COLORREF *color)
{
    if (g.key_error) { *short_text = "invalid key"; *kind = G_TRI; *color = W_RED_TX; return "invalid key in hidway.ini"; }
    if (!g.net_ok && !g.preview) { *short_text = "socket error"; *kind = G_TRI; *color = W_RED_TX; return "socket error"; }
    if (!g.armed) {
        if (link_recent()) { *short_text = "reachable"; *kind = G_FILL; *color = W_GREEN_TX; return "relay reachable"; }
        if (g.ever_status || g.probes_unanswered > 5) { *short_text = "no reply"; *kind = G_TRI; *color = W_RED_TX; return "no reply from relay"; }
        *short_text = "probing"; *kind = G_HOLLOW; *color = W_TEXT2;
        return "probing relay";
    }
    if (!g.status_seen) { *short_text = "waiting"; *kind = G_HOLLOW_AMBER; *color = W_AMBER; return "waiting for relay"; }
    if (link_recent()) {
        *short_text = "linked"; *kind = G_FILL; *color = W_GREEN_TX;
        return (g.pi_flags & HIDWAY_STATUS_SERIAL_OPEN) ? "linked · serial open" : "linked · no serial";
    }
    *short_text = "link lost"; *kind = G_TRI; *color = W_RED_TX;
    return "link lost";
}

/* Target PC as the Pico sees it over USB, reported through the relay. */
static const char *target_text(COLORREF *color)
{
    int ts = target_state();
    *color = W_TEXT2;
    if (ts < 0) { *color = W_TEXT3; return "—"; }
    if (waking()) { *color = W_AMBER; return "Waking"; }
    if (ts == HIDWAY_TARGET_ON) { *color = W_GREEN_TX; return "On"; }
    if (ts == HIDWAY_TARGET_ASLEEP) { *color = W_AMBER; return "Asleep"; }
    if (ts == HIDWAY_TARGET_OFF) return "Off";
    return "Off?"; /* no report: Pico without power (PC off) or unplugged */
}

/* ---- rail ---------------------------------------------------------------- */

/* Text that must end inside `right`: an ellipsis, never a silent clip. */
static void text_fit(HDC dc, HFONT f, COLORREF c, int x, int cy, int right, const char *s)
{
    RECT r = {x, cy - font_h(dc, f) / 2 - S(1), right, cy + font_h(dc, f) / 2 + S(2)};
    text_in(dc, f, c, r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, s);
}

static void fact_value(HDC dc, int i, int kind, COLORREF color, const char *text)
{
    layout_t *L = &g.L;
    int cy = L->fact_cy[i], x = L->fact_vx;
    if (kind >= 0) {
        glyph(dc, kind, x + S(5), cy, W_RAIL);
        x += S(17);
    }
    text_fit(dc, g.f_fact, color, x, cy, L->rail.right - S(16), text);
}

static void paint_rail(HDC dc)
{
    layout_t *L = &g.L;
    fill_solid(dc, L->rail, W_RAIL);
    RECT ln = {L->rail.right - S(1), 0, L->rail.right, L->rail.bottom};
    fill_solid(dc, ln, W_LINE);

    /* brand and version (the subtitle is gone) */
    if (g.mark_icon)
        DrawIconEx(dc, L->mark.left, L->mark.top, g.mark_icon, L->mark.right - L->mark.left,
                   L->mark.bottom - L->mark.top, 0, NULL, DI_NORMAL);
    int tw = text_w(dc, g.f_title, "HIDway");
    text_vc(dc, g.f_title, W_TEXT, L->title_x, L->title_cy, "HIDway");
    int top = L->title_cy - font_h(dc, g.f_title) / 2;
    int vtop = top + font_ascent(dc, g.f_title) - font_ascent(dc, g.f_label);
    RECT ver = {L->title_x + tw + S(8), vtop, L->rail.right - S(16), vtop + font_h(dc, g.f_label)};
    text_in(dc, g.f_label, W_TEXT3, ver, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, HIDWAY_VERSION);

    /* state block: DISARMED and the route, or ARMED with the running timer */
    RECT s = L->state;
    int u2 = S(2) > 0 ? S(2) : 2;
    if (g.armed)
        round_rect_w(dc, s, S(10), W_GREEN_F, W_GREEN, u2);
    else
        round_rect(dc, s, S(10), W_SURF, W_LINE2);
    int gw = g.armed ? 14 : 10;
    glyph(dc, g.armed ? G_RING : G_HOLLOW, s.left + S(12 + 7), s.top + S(g.armed ? 24 : 22), g.armed ? W_GREEN_F : W_SURF);
    int tx = s.left + S(12 + gw + 11), wcy = s.top + S(23), scy = s.top + S(43);
    const char *word = g.armed ? "ARMED" : "DISARMED";
    text_vc(dc, g.f_pill, g.armed ? W_ARMED_TX : W_TEXT, tx, wcy, word);
    if (g.armed) {
        unsigned secs = (timeGetTime() - g.armed_at_ms) / 1000;
        char t[24];
        if (secs < 3600)
            snprintf(t, sizeof t, "%02u:%02u", secs / 60, secs % 60);
        else
            snprintf(t, sizeof t, "%u:%02u:%02u", secs / 3600, secs / 60 % 60, secs % 60);
        int x = tx + text_w(dc, g.f_pill, word) + S(6);
        HFONT tf = g.f_timer;
        if (x + text_w(dc, tf, t) > s.right - S(12)) /* hours: the smaller face */
            tf = g.f_mono;
        text_vc(dc, tf, W_GREEN_TX, x, wcy, t);
    }
    char sub[48];
    if (g.armed)
        snprintf(sub, sizeof sub, "%s", g.cfg.loopback_mode ? "Through the Pico" : "To the Gaming PC");
    else
        snprintf(sub, sizeof sub, "%s route", route_name(g.cfg.loopback_mode));
    text_fit(dc, g.f_label, g.armed ? W_ARMED_SUB : W_TEXT3, tx, scy, s.right - S(12), sub);

    /* ROUTE, RELAY, PICO, TARGET: a glyph and a word each */
    static const char *const names[4] = {"ROUTE", "RELAY", "PICO", "TARGET"};
    for (int i = 0; i < 4; i++)
        tracked(dc, g.f_section, W_TEXT3, L->fact_x, L->fact_cy[i] - font_h(dc, g.f_section) / 2, names[i]);
    text_fit(dc, g.f_btn, W_TEXT, L->fact_vx, L->fact_cy[0], L->rail.right - S(16),
             route_name(g.cfg.loopback_mode));
    const char *st;
    int kind;
    COLORREF col;
    relay_status(&st, &kind, &col);
    fact_value(dc, 1, kind, col, st);
    COLORREF pc;
    const char *pt = pico_text(&pc);
    fact_value(dc, 2, pc == C_LIVE ? G_FILL : pc == C_AMBER ? G_HALF : G_HOLLOW,
               pc == C_LIVE ? W_GREEN_TX : pc == C_AMBER ? W_AMBER : W_TEXT2, pt);
    COLORREF tc;
    const char *tt = target_text(&tc);
    char full[40];
    if (tt[0] == '\xe2') /* no report: just the dash */
        snprintf(full, sizeof full, "%s", tt);
    else
        snprintf(full, sizeof full, "Gaming PC %c%s", (char)tolower((unsigned char)tt[0]), tt + 1);
    if (text_w(dc, g.f_fact, full) > L->rail.right - S(16) - (L->fact_vx + S(17)))
        snprintf(full, sizeof full, "%s", tt); /* too wide for the rail: the state alone */
    int ts = target_state();
    fact_value(dc, 3, tc == W_GREEN_TX ? G_FILL : tc == W_AMBER ? (waking() ? G_HALF : G_HOLLOW_AMBER) : G_HOLLOW,
               tc == W_TEXT3 ? W_TEXT2 : tc, ts < 0 && !waking() ? tt : full);

    RECT dv = {L->fact_x, L->div_y, L->rail.right - S(16), L->div_y + (S(1) > 0 ? S(1) : 1)};
    fill_solid(dc, dv, W_LINE);
}

/* ---- Control ------------------------------------------------------------- */

/* One key/value row of the summaries. */
static void kv_row(HDC dc, int lx, int vx, int right, int cy, const char *k, const char *v)
{
    text_vc(dc, g.f_fact, W_TEXT3, lx, cy, k);
    text_fit(dc, g.f_fact, W_TEXT, vx, cy, right, v);
}

/* What ARM will send, from the saved settings. */
static void summary_text(int row, char *out, size_t cap)
{
    const hidway_config_t *c = &g.cfg;
    static const char *const buttons[] = {"Left", "Right", "Middle", "Back", "Forward"};
    switch (row) {
    case 0:
        snprintf(out, cap, "%s %s",
                 c->relay_keyboard && c->relay_mouse ? "Keyboard + mouse"
                 : c->relay_keyboard ? "Keyboard" : c->relay_mouse ? "Mouse" : "Nothing",
                 c->loopback_mode ? "through the Pico to this PC" : "to the Gaming PC");
        break;
    case 1:
        out[0] = 0;
        if (c->loopback_mode) {
            snprintf(out, cap, "%s", c->loopback_physical ? "Virtual, Physical through Pico" : "Virtual");
        } else {
            if (c->source_physical) strncat(out, "Physical", cap - strlen(out) - 1);
            if (c->source_touchpad) strncat(out, out[0] ? ", Touchpad" : "Touchpad", cap - strlen(out) - 1);
            if (c->source_virtual) strncat(out, out[0] ? ", Virtual" : "Virtual", cap - strlen(out) - 1);
            if (!out[0]) snprintf(out, cap, "none");
        }
        break;
    case 2:
        if (c->repeat_enabled)
            snprintf(out, cap, "repeat %s %d/s, %d/s",
                     buttons[c->repeat_button >= 0 && c->repeat_button < 5 ? c->repeat_button : 0],
                     c->repeat_cps, c->virtual_click_cps);
        else
            snprintf(out, cap, "repeat off, %d/s", c->virtual_click_cps);
        break;
    default:
        if (c->auto_disarm_ms <= 0)
            snprintf(out, cap, "off");
        else if (c->auto_disarm_ms % 1000 == 0)
            snprintf(out, cap, "link silent %d s", c->auto_disarm_ms / 1000);
        else if (c->auto_disarm_ms > 1000)
            snprintf(out, cap, "link silent %.1f s", c->auto_disarm_ms / 1000.0);
        else
            snprintf(out, cap, "link silent %d ms", c->auto_disarm_ms);
        break;
    }
}

/* Where the rows of the LIVE INPUT panel go: roomier when the panel is tall,
 * tighter where the banner and a second message line take height. */
typedef struct { int idle_cy, div, caps_cy, kcy, mcy, row0, pitch, bottom; } live_metrics_t;

static void live_metrics(int armed, RECT c, live_metrics_t *m)
{
    int lh = MulDiv(c.bottom - c.top, 96, (int)g.dpi);
    memset(m, 0, sizeof *m);
    if (!armed) {
        int compact = lh < 170;
        m->idle_cy = c.top + S(compact ? 16 : 20);
        m->div = c.top + S(compact ? 33 : 38);
        m->caps_cy = m->div + S(compact ? 12 : 16);
        m->row0 = m->caps_cy + S(compact ? 16 : 21);
        m->pitch = S(compact ? 19 : 22);
    } else {
        int compact = lh < 184;
        m->kcy = c.top + S(compact ? 21 : 24);
        m->mcy = m->kcy + S(compact ? 24 : 28);
        m->div = m->mcy + S(compact ? 15 : 18);
        m->row0 = m->div + S(compact ? 15 : 18);
        m->pitch = S(compact ? 19 : 22);
    }
    m->bottom = m->row0 + 3 * m->pitch + S(10);
}

static void paint_control(HDC dc)
{
    layout_t *L = &g.L;
    int x0 = L->body.left, cy = L->head_cy;

    /* section head: CONNECTION, the encryption chip, relay status and address */
    tracked(dc, g.f_section, W_TEXT3, x0, cy - font_h(dc, g.f_section) / 2, "CONNECTION");
    int bx = x0 + tracked_w(dc, g.f_section, "CONNECTION") + S(10), chw;
    if (g.key_error)
        chw = chip(dc, bx, cy, S(18), "KEY ERROR", W_RED_F, W_RED_BD, W_RED_TX);
    else if (g.crypto_on)
        chw = chip(dc, bx, cy, S(18), "E2E", W_GREEN_F, W_GREEN_BD, W_GREEN_TX);
    else
        chw = chip(dc, bx, cy, S(18), "E2E OFF", W_FIELD, W_LINE2, W_TEXT3);
    char addr[160];
    snprintf(addr, sizeof addr, "%s:%d", g.cfg.target, g.cfg.port);
    const char *st, *shortt;
    int kind;
    COLORREF col;
    st = relay_status(&shortt, &kind, &col);
    int right = L->body.right, aw = text_w(dc, g.f_mono_s, addr), sw = text_w(dc, g.f_label, st);
    int room = right - (bx + chw) - S(12) - S(10) - sw - S(14); /* what the address may take */
    if (aw > room && room > S(40))
        aw = room;                                                 /* a long host ends in an ellipsis */
    text_fit(dc, g.f_mono_s, W_TEXT3, right - aw, cy, right, addr);
    int sx = right - aw - S(10) - sw;
    text_vc(dc, g.f_label, col, sx, cy, st);
    glyph(dc, kind, sx - S(12), cy, W_BG);

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
    COLORREF tc;
    snprintf(v[3], 32, "%s", target_text(&tc));
    static const char *names[4] = {"RTT", "LOSS", "RATE", "GAMING PC"};
    for (int i = 0; i < 4; i++) {
        RECT t = L->tiles[i];
        int hot = i == 3 && g.wake_hot && wake_offered();
        round_rect(dc, t, S(8), hot ? W_SURF2 : W_SURF, hot ? W_FOCUS : W_LINE);
        tracked(dc, g.f_section, W_TEXT3, t.left + S(11), t.top + S(8), names[i]);
        text_vc(dc, g.f_tile, i == 3 ? tc : live[i] ? W_TEXT : W_TEXT3, t.left + S(11), t.top + S(38), v[i]);
    }
    /* A click on the tile wakes the target; the chip says so. */
    if (wake_offered() && !waking()) {
        RECT t = L->tiles[3];
        int cw = text_w(dc, g.f_chip, "WAKE") + 2 * S(8);
        chip(dc, t.right - S(9) - cw, t.bottom - S(18), S(18), "WAKE", W_GREEN_F, W_GREEN_BD, W_GREEN_TX);
    }

    /* LIVE INPUT head: the mode on the right */
    tracked(dc, g.f_section, W_TEXT3, x0, L->live_head_cy - font_h(dc, g.f_section) / 2, "LIVE INPUT");
    const char *mode = g.cfg.relay_keyboard && g.cfg.relay_mouse ? "keyboard + mouse"
                     : g.cfg.relay_keyboard ? "keyboard only"
                     : g.cfg.relay_mouse ? "mouse only" : "nothing relayed";
    char mode_text[80];
    uint32_t limited = 0;
    for (unsigned o = 0; o < 3; o++)
        for (unsigned i = 0; i < 5; i++)
            limited += g.pulse[o][i].limited;
    if (g.armed && (limited || g.cfg.repeat_enabled)) {
        snprintf(mode_text, sizeof mode_text, "%s  |  limited %lu", g.cfg.repeat_enabled ? "repeat ON" : "click pacing",
                 (unsigned long)limited);
        mode = mode_text;
    }
    int mw = text_w(dc, g.f_label, mode);
    text_vc(dc, g.f_label, W_TEXT3, right - mw, L->live_head_cy, mode);

    /* panel */
    RECT c = L->live;
    round_rect(dc, c, S(8), W_SURF, W_LINE);
    live_metrics_t lm;
    live_metrics(g.armed, c, &lm);
    int lx = c.left + S(14), vx = c.left + S(14 + 110), pr = c.right - S(14);
    char val[96];
    if (!g.armed) {
        int idle_cy = lm.idle_cy, div = lm.div, caps_cy = lm.caps_cy, row0 = lm.row0, pitch = lm.pitch;
        char hk[48], hint[96];
        hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
        snprintf(hint, sizeof hint, "Press ARM or %s to start relaying", hk);
        text_vc(dc, g.f_semi, W_TEXT, lx, idle_cy, "Input capture is off");
        int iw = text_w(dc, g.f_semi, "Input capture is off") + S(8);
        text_fit(dc, g.f_label, W_TEXT3, lx + iw, idle_cy, pr, hint);
        RECT dv = {c.left + S(14), div, c.right - S(14), div + (S(1) > 0 ? S(1) : 1)};
        fill_solid(dc, dv, W_LINE);
        tracked(dc, g.f_section, W_TEXT3, lx, caps_cy - font_h(dc, g.f_section) / 2, "ON ARM");
        static const char *const keys[4] = {"Sending", "Sources", "Click pacing", "Auto-disarm"};
        for (int i = 0; i < 4; i++) {
            summary_text(i, val, sizeof val);
            kv_row(dc, lx, vx, pr, row0 + pitch * i, keys[i], val);
        }
        return;
    }

    /* armed: the keys and mouse rows, then the same summary */
    int kcy = lm.kcy, mcy = lm.mcy, div = lm.div, row0 = lm.row0, pitch = lm.pitch;
    int ch = S(22), gap = S(6), cx0 = c.left + S(14 + 64), maxx = c.right - S(14);
    text_vc(dc, g.f_label, W_TEXT3, lx, kcy, "Keys");
    int x = cx0, shown = 0, total = 0, n = 0;
    const char *knames[24];
    char tmp[24][8];
    for (int i = 0; i < 8 && n < 24; i++)
        if (g.modifiers & MODS[i].bit)
            knames[n++] = MODS[i].name;
    for (unsigned u = HIDWAY_KEY_USAGE_MIN; u <= HIDWAY_KEY_USAGE_MAX && n < 24; u++)
        if (hidway_bitmap_test(g.keys, (uint8_t)u)) {
            knames[n] = hidway_usage_name((uint8_t)u, tmp[n]);
            n++;
        }
    total = n;
    for (int i = 0; i < n; i++) {
        int w = text_w(dc, g.f_chip, knames[i]) + 2 * S(8);
        if (w < ch) w = ch;
        if (x + w > maxx - S(40) && i < n - 1)
            break;
        x += chip(dc, x, kcy, ch, knames[i], W_KEY, W_KEY_BD, W_TEXT) + gap;
        shown++;
    }
    if (shown < total) {
        char more[8];
        snprintf(more, sizeof more, "+%d", total - shown);
        chip(dc, x, kcy, ch, more, W_FIELD, W_LINE2, W_TEXT2);
    }
    if (total == 0)
        text_vc(dc, g.f_label, W_TEXT3, cx0, kcy, "none held");

    text_vc(dc, g.f_label, W_TEXT3, lx, mcy, "Mouse");
    static const struct { uint8_t bit; const char *name; } BTNS[] = {
        {BTN_L, "Left"}, {BTN_R, "Right"}, {BTN_M, "Middle"}, {BTN_X1, "Back"}, {BTN_X2, "Fwd"}};
    x = cx0;
    for (int i = 0; i < 5; i++)
        if (g.buttons & BTNS[i].bit)
            x += chip(dc, x, mcy, ch, BTNS[i].name, W_KEY, W_KEY_BD, W_TEXT) + gap;
    char mv[64];
    snprintf(mv, sizeof mv, "dx %+ld   dy %+ld   wheel %+d", (long)g.show_dx, (long)g.show_dy, g.show_dw);
    text_vc(dc, g.f_mono, W_TEXT2, x, mcy, mv);

    RECT dv = {c.left + S(14), div, c.right - S(14), div + (S(1) > 0 ? S(1) : 1)};
    fill_solid(dc, dv, W_LINE);
    static const char *const keys[4] = {"Sending", "Sources", "Click pacing", "Auto-disarm"};
    for (int i = 0; i < 4; i++) {
        summary_text(i, val, sizeof val);
        kv_row(dc, lx, vx, pr, row0 + pitch * i, keys[i], val);
    }
}

/* ---- Connection ---------------------------------------------------------- */

/* A painted field frame around a borderless edit. */
static void field_frame(HDC dc, RECT f, int id, int enabled)
{
    round_rect(dc, f, S(6), W_FIELD, enabled && g.focus_id == id ? W_FOCUS : enabled ? W_LINE2 : W_LINE);
}

/* A caps group head with an optional muted remark after it. */
static void group_head(HDC dc, int x, int cy, const char *caps, const char *note)
{
    tracked(dc, g.f_section, W_TEXT3, x, cy - font_h(dc, g.f_section) / 2, caps);
    if (note)
        text_vc(dc, g.f_label, W_TEXT3, x + tracked_w(dc, g.f_section, caps) + S(10), cy, note);
}

static void paint_connection(HDC dc)
{
    layout_t *L = &g.L;
    int x0 = L->body.left;
    char head[48];
    snprintf(head, sizeof head, "%s route", route_name(g.cfg.loopback_mode));
    for (char *p = head; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    group_head(dc, x0, L->conn_head_cy[0], head, "saved separately for each route");
    group_head(dc, x0, L->conn_head_cy[1], "BOTH ROUTES", NULL);
    group_head(dc, x0, L->conn_head_cy[2], "CURRENT LINK", NULL);

    static const char *labels[4] = {"Relay address", "Port", "Send rate (Hz)", "Auto-disarm (ms)"};
    for (int i = 0; i < 4; i++) {
        text_vc(dc, g.f_label, W_TEXT2, L->frame[i].left + S(1), L->conn_lbl_cy[i / 2], labels[i]);
        field_frame(dc, L->frame[i], ID_TARGET + i, 1);
        if (g.conn_error && g.conn_status[0] && g.conn_bad == i + 1) /* the field a refused Save named */
            round_rect(dc, L->frame[i], S(6), W_FIELD, W_RED);
    }
    text_fit(dc, g.f_label, W_TEXT3, x0, L->conn_hint_cy, L->body.right,
             "Save disarms first. A wrong value is refused and nothing changes.");

    /* Current link: relay, encryption, Pi to Pico */
    const char *shortt, *st;
    int kind;
    COLORREF col;
    st = relay_status(&shortt, &kind, &col);
    int vx = x0 + S(110), pitch = S(20), right = L->body.right;
    char val[96];
    text_vc(dc, g.f_fact, W_TEXT3, x0, L->link_cy, "Relay");
    glyph(dc, kind, vx + S(5), L->link_cy, W_BG);
    if ((g.armed && g.status_seen) || (!g.armed && link_recent()))
        snprintf(val, sizeof val, "%s, RTT %d ms", st, g.rtt_ms);
    else
        snprintf(val, sizeof val, "%s", st);
    text_fit(dc, g.f_fact, col, vx + S(17), L->link_cy, right, val);
    kv_row(dc, x0, vx, right, L->link_cy + pitch, "Encryption",
           g.key_error ? "KEY ERROR: invalid key in hidway.ini" : g.crypto_on ? "E2E on (key in hidway.ini)" : "E2E off");
    kv_row(dc, x0, vx, right, L->link_cy + 2 * pitch, "Pi to Pico",
           ((g.armed && g.status_seen) || (!g.armed && link_recent()))
               ? ((g.pi_flags & HIDWAY_STATUS_SERIAL_OPEN) ? "serial open" : "no serial") : "unknown");
}

/* ---- Input --------------------------------------------------------------- */

static void paint_input(HDC dc)
{
    layout_t *L = &g.L;
    int x0 = L->body.left, loop = g.cfg.loopback_mode != 0, rep = g.pending.repeat_enabled != 0;

    /* left column */
    text_vc(dc, g.f_label, W_TEXT2, x0, L->in_route_cy, "Send input to");
    round_rect(dc, L->seg, S(7), W_FIELD, W_LINE2);
    char desc[160];
    if (loop)
        snprintf(desc, sizeof desc, "The Pico is plugged into this PC.");
    else
        snprintf(desc, sizeof desc, "Through the relay at %s.", g.cfg.target);
    text_fit(dc, g.f_sub, W_TEXT3, x0, L->in_desc_cy, x0 + S(254), desc);
    text_vc(dc, g.f_label, W_TEXT2, x0, L->in_relay_cy, "Relay");
    text_vc(dc, g.f_label, W_TEXT2, x0, L->in_src_cy, "Sources");
    if (L->note_shown) { /* Gaming PC with Physical ticked: say what it does */
        glyph(dc, G_TRI_AMBER, L->note.left - S(10), L->note.top + S(7), W_BG);
        text_wrap(dc, g.f_sub, W_WARN_LINE, L->note, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS,
                  "Experimental: keys, buttons and wheel of this PC go through the Pico.");
    }
    text_vc(dc, g.f_label, loop ? W_TEXT2 : W_TEXT3, x0, L->in_ptr_cy, "Gaming PC pointer");

    /* the rule and the right column */
    fill_solid(dc, L->vr, W_LINE);
    int x2 = L->repeat.left;
    tracked(dc, g.f_section, W_TEXT3, x2, L->in_cap_cy - font_h(dc, g.f_section) / 2, "CLICK PACING");
    COLORREF rc = rep ? W_TEXT2 : W_TEXT3;
    text_vc(dc, g.f_label, rc, x2, L->in_btn_cy, "Button");
    {
        const char *s = "Repeat clicks/sec";
        text_vc(dc, g.f_label, rc, L->in_cps_right - text_w(dc, g.f_label, s), L->in_btn_cy, s);
    }
    field_frame(dc, L->repeat_cps, ID_REPEAT_CPS, rep);
    text_vc(dc, g.f_label, rep && !loop ? W_TEXT2 : W_TEXT3, x2, L->in_rsrc_cy, "Repeat source");
    text_vc(dc, g.f_label, W_TEXT2, x2, L->in_click_cy, "Virtual clicks/sec");
    field_frame(dc, L->click_cps, ID_CLICK_CPS, 1);
    text_vc(dc, g.f_label, W_TEXT3, L->click_cps.right + S(8), (L->click_cps.top + L->click_cps.bottom) / 2,
            "1-25 per button");
    char limited[96];
    snprintf(limited, sizeof limited, "Last ARM: %lu extra click attempts limited", (unsigned long)g.last_limited);
    text_wrap(dc, g.f_sub, W_TEXT3, L->limited, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS, limited);
}

/* ---- Tools --------------------------------------------------------------- */

static void paint_tools(HDC dc)
{
    layout_t *L = &g.L;
    int x = L->body.left, cy = L->hk_cy, ch = S(20), gap = S(5);

    /* hotkeys as keycaps; a toggle hotkey that is in use is dimmed (the
     * message strip says why) */
    const char *groups[2] = {g.cfg.toggle.text, g.cfg.panic.text};
    const char *gl[2] = {"Toggle", "Panic"};
    for (int k = 0; k < 2; k++) {
        int dim = k == 0 && !g.toggle_hk_ok;
        text_vc(dc, g.f_label, dim ? W_AMBER : W_TEXT3, x, cy, gl[k]);
        x += (k == 0 ? S(46) : S(42));
        char tok[6][12];
        int n = hotkey_tokens(groups[k], tok);
        for (int i = 0; i < n; i++) {
            int cw = text_w(dc, g.f_chip, tok[i]) + 2 * S(8);
            if (x + cw > L->body.right - S(24)) { /* does not fit: say so */
                chip(dc, x, cy, ch, "...", W_FIELD, W_LINE, W_TEXT3);
                break;
            }
            x += chip(dc, x, cy, ch, tok[i], dim ? W_FIELD : W_KEY, dim ? W_LINE : W_KEY_BD,
                      dim ? W_TEXT3 : W_TEXT2) + gap;
        }
        x += S(13);
    }

    tracked(dc, g.f_section, W_TEXT3, L->body.left, L->rh_cy - font_h(dc, g.f_section) / 2, "RECENT INPUT");
    text_vc(dc, g.f_label, W_TEXT3, L->body.left + tracked_w(dc, g.f_section, "RECENT INPUT") + S(10), L->rh_cy,
            "memory only");
    round_rect(dc, L->list, S(6), W_FIELD, W_LINE2);
}

/* The whole client area except the child controls, into `dc`. */
static void paint_content(HDC dc, RECT cr)
{
    fill_solid(dc, cr, W_BG);
    paint_rail(dc);
    if (banner_visible())
        paint_banner(dc);
    switch (g.tab) {
    case TAB_CONTROL: paint_control(dc); break;
    case TAB_CONNECTION: paint_connection(dc); break;
    case TAB_INPUT: paint_input(dc); break;
    default: paint_tools(dc); break;
    }
    paint_msg(dc);
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
 * as WM_COMMAND ID_TAB_CONTROL + index, the same command the tests post.
 * Underline tabs: 12 px in from the left, 12 px padding each side of a name,
 * 4 px between tabs; the selected one has a 2 px underline; the Input tab
 * carries an amber dot while there are edits that are not applied. */
static int tab_hot = -1;

/* The rectangle of each tab and the centre of the Input tab's dot. */
static void tabs_layout(HDC dc, RECT rc[TAB_COUNT], POINT *dotp)
{
    int x = S(12);
    for (int i = 0; i < TAB_COUNT; i++) {
        int tw = text_w(dc, g.f_tab, TAB_NAMES[i]);
        int extra = i == TAB_INPUT && g.input_dirty ? S(12) : 0;
        RECT r = {x, 0, x + tw + S(24) + extra, S(43)};
        rc[i] = r;
        if (i == TAB_INPUT) {
            dotp->x = x + S(12) + tw + S(6) + S(3);
            dotp->y = S(22);
        }
        x = r.right + S(4);
    }
}

static void tabs_pick(HWND h, int t)
{
    if (t >= 0 && t < TAB_COUNT)
        SendMessageA(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_TAB_CONTROL + t, BN_CLICKED), (LPARAM)h);
}

static int tabs_hit(HWND h, int x)
{
    HDC dc = GetDC(h);
    RECT rc[TAB_COUNT];
    POINT dotp;
    tabs_layout(dc, rc, &dotp);
    ReleaseDC(h, dc);
    for (int i = 0; i < TAB_COUNT; i++)
        if (x >= rc[i].left - S(2) && x < rc[i].right + S(2))
            return i;
    return -1;
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
    fill_solid(dc, r, W_BG);
    RECT line = {0, r.bottom - (S(1) > 0 ? S(1) : 1), r.right, r.bottom};
    fill_solid(dc, line, W_LINE);
    int focus = GetFocus() == h && !(SendMessageA(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    RECT rc[TAB_COUNT];
    POINT dotp = {0, 0};
    tabs_layout(dc, rc, &dotp);
    for (int i = 0; i < TAB_COUNT; i++) {
        RECT t = rc[i];
        if (i == g.tab && focus) {
            RECT fr = {t.left, S(6), t.right, t.bottom - S(4)};
            round_rect(dc, fr, S(5), W_BG, W_FOCUS);
        }
        RECT tr = {t.left + S(12), 0, t.right - S(12), t.bottom};
        text_in(dc, g.f_tab, i == g.tab ? W_TEXT : i == tab_hot ? W_TEXT : W_TEXT2, tr,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE, TAB_NAMES[i]);
        if (i == TAB_INPUT && g.input_dirty)
            dot(dc, dotp.x, dotp.y, S(3), W_AMBER);
        if (i == g.tab) {
            RECT ul = {t.left + S(10), r.bottom - S(2), t.right - S(10), r.bottom};
            round_rect(dc, ul, S(1), W_TEXT, W_TEXT);
        }
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
 * the Tools tab, applies at once as before. In the Gaming PC route Touchpad
 * shows off and Virtual shows on: that is what the route captures, whatever
 * the boxes remember for the Remote PC route. Physical there is its own
 * setting, loopback_physical (this PC's keys through the Pico). */
static int cb_checked(int id)
{
    if (id == ID_CB_HIST) return g.cfg.history_enabled;
    if (id == ID_REPEAT) return g.pending.repeat_enabled;
    if (g.cfg.loopback_mode && id == ID_SRC_PHYSICAL) return g.pending.loopback_physical;
    if (g.cfg.loopback_mode && id == ID_SRC_TOUCHPAD) return 0;
    if (g.cfg.loopback_mode && id == ID_SRC_VIRTUAL) return 1;
    if (id == ID_CB_KBD) return g.pending.relay_keyboard;
    if (id == ID_CB_MOUSE) return g.pending.relay_mouse;
    if (id == ID_SRC_PHYSICAL) return g.pending.source_physical;
    if (id == ID_SRC_TOUCHPAD) return g.pending.source_touchpad;
    return g.pending.source_virtual;
}

/* The two-line tool buttons of the Tools tab: a title and what it does. */
static const char *tool_subtitle(int id, const char *label)
{
    if (id == ID_MONITOR) return "devices and events";
    if (id == ID_REPLAY) return strcmp(label, "Stop") ? "play a recording" : "stop playback";
    return "check for updates";
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
    bool off = (d->itemState & ODS_DISABLED) != 0;

    /* paint what is behind the control first: no default-grey corners. The
     * rail buttons sit on the rail, the banner buttons on the blue bar, the
     * route selector inside its field frame, everything else on the pane. */
    COLORREF behind = (id == ID_ARM || id == ID_PANIC || id == ID_LOCAL_PICO) ? W_RAIL
                    : (id == ID_UPDATE_NOW || id == ID_UPDATE_LATER) ? W_UPD
                    : (id == ID_PROFILE_REMOTE || id == ID_PROFILE_GAMING) ? W_FIELD : W_BG;
    fill_solid(dc, lr, behind);

    char label[32];
    GetWindowTextA(d->hwndItem, label, sizeof label);

    if (is_checkbox(id)) {
        int on = cb_checked(id);
        int box = S(16);
        RECT b = {S(1), (h - box) / 2, S(1) + box, (h - box) / 2 + box};
        COLORREF fill, bd;
        if (off) {
            fill = on ? W_BX_DIS_ON : W_SURF;
            bd = on ? W_BX_DIS_ON : W_BX_DIS_BD;
        } else if (on) {
            fill = bd = hot ? C_WHITE : W_CHECK;
        } else {
            fill = hot ? W_SURF2 : W_FIELD;
            bd = hot ? W_FOCUS : W_BX_BD;
        }
        round_rect(dc, b, S(4), fill, bd);
        if (on)
            check_mark(dc, b, W_BG);
        text_vc(dc, g.f_body, off ? W_TEXT3 : hot ? W_TEXT : W_TEXT, b.right + S(8), h / 2, label);
        if (focus) {
            RECT fr = b;
            InflateRect(&fr, S(2), S(2));
            round_rect(dc, fr, S(5), W_FOCUS, W_FOCUS);
            round_rect(dc, b, S(4), fill, fill == bd ? fill : bd);
            if (on)
                check_mark(dc, b, W_BG);
        }
    } else if (id == ID_ARM) {
        /* two lines: the word and the toggle hotkey */
        COLORREF fill, bd, tx, sub;
        if (g.armed) {
            fill = down ? W_DIS_P : hot ? W_DIS_H : W_DIS;
            bd = W_DIS_BD;
            tx = W_TEXT;
            sub = W_DIS_SUB;
        } else {
            fill = down ? W_ARM_P : hot ? W_ARM_H : W_ARM;
            bd = W_ARM_BD;
            tx = W_ARM_TX;
            sub = W_ARM_SUB;
        }
        round_rect(dc, lr, S(8), fill, bd);
        if (focus) {
            RECT fr = lr;
            InflateRect(&fr, -S(3), -S(3));
            round_rect(dc, fr, S(6), W_FOCUS, W_FOCUS);
            InflateRect(&fr, -S(1), -S(1));
            round_rect(dc, fr, S(5), fill, fill);
        }
        char hk[48];
        hotkey_pretty(g.cfg.toggle.text, hk, sizeof hk);
        int a = font_h(dc, g.f_btn_big), b = font_h(dc, g.f_arm_sub), top = (h - (a + b)) / 2;
        SetTextCharacterExtra(dc, S(1) * 6 / 10);
        RECT t1 = {0, top, w, top + a};
        text_in(dc, g.f_btn_big, tx, t1, DT_CENTER | DT_VCENTER | DT_SINGLELINE, g.armed ? "DISARM" : "ARM");
        SetTextCharacterExtra(dc, 0);
        RECT t2 = {0, top + a, w, top + a + b};
        text_in(dc, g.f_arm_sub, sub, t2, DT_CENTER | DT_VCENTER | DT_SINGLELINE, hk);
    } else if (id == ID_PANIC) {
        COLORREF fill = down ? W_RED_FP : hot ? W_RED_FH : W_RED_F;
        round_rect(dc, lr, S(8), fill, hot ? W_RED_BDH : W_RED_BD);
        if (focus) {
            RECT fr = lr;
            InflateRect(&fr, -S(3), -S(3));
            round_rect(dc, fr, S(6), W_FOCUS, W_FOCUS);
            InflateRect(&fr, -S(1), -S(1));
            round_rect(dc, fr, S(5), fill, fill);
        }
        SetTextCharacterExtra(dc, S(1) * 11 / 10);
        int tw = text_w(dc, g.f_panic, "PANIC"), gw = S(14), gap = S(9);
        int x = (w - (gw + gap + tw)) / 2;
        glyph(dc, G_OCT, x + gw / 2, h / 2, fill);
        text_vc(dc, g.f_panic, hot ? RGB(255, 186, 178) : W_RED_TX, x + gw + gap, h / 2, label);
        SetTextCharacterExtra(dc, 0);
    } else if (id == ID_MONITOR || id == ID_REPLAY || id == ID_UPDATES) {
        COLORREF fill = down ? W_SURF : W_SURF2;
        round_rect(dc, lr, S(8), fill, hot ? W_FOCUS : W_LINE2);
        if (focus) {
            RECT fr = lr;
            InflateRect(&fr, -S(3), -S(3));
            round_rect(dc, fr, S(6), W_FOCUS, W_FOCUS);
            InflateRect(&fr, -S(1), -S(1));
            round_rect(dc, fr, S(5), fill, fill);
        }
        int a = font_h(dc, g.f_semi), b = font_h(dc, g.f_sub), top = (h - (a + b)) / 2;
        text_vc(dc, g.f_semi, W_TEXT, S(11), top + a / 2, label);
        text_vc(dc, g.f_sub, W_TEXT3, S(11), top + a + b / 2, tool_subtitle(id, label));
    } else if (id == ID_UPDATE_NOW || id == ID_UPDATE_LATER) {
        /* update banner: small buttons on the blue bar; Later has no frame */
        int primary = id == ID_UPDATE_NOW;
        COLORREF fill = primary ? (down ? W_UPD : hot ? W_UPD_BTN_BD : W_UPD_BTN) : behind;
        COLORREF bd = primary ? W_UPD_BTN_BD : (hot ? W_UPD_ICO : behind);
        round_rect(dc, lr, S(5), fill, bd);
        if (focus) {
            RECT fr = lr;
            InflateRect(&fr, -S(3), -S(3));
            round_rect(dc, fr, S(3), W_FOCUS, W_FOCUS);
            InflateRect(&fr, -S(1), -S(1));
            round_rect(dc, fr, S(2), fill, fill);
        }
        text_in(dc, g.f_chip, W_UPD_TX, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE, label);
    } else if (id == ID_PROFILE_REMOTE || id == ID_PROFILE_GAMING) {
        /* two-way route selector in its field frame: the route in use is lit
         * with a filled radio dot */
        int on = (id == ID_PROFILE_GAMING) == (g.cfg.loopback_mode != 0);
        COLORREF fill = on ? W_SEG_ON : behind;
        if (!on && hot)
            fill = W_SURF2;
        round_rect(dc, lr, S(5), fill, fill);
        if (focus) {
            RECT fr = lr;
            round_rect(dc, fr, S(5), W_FOCUS, W_FOCUS);
            InflateRect(&fr, -S(1), -S(1));
            round_rect(dc, fr, S(4), fill, fill);
        }
        int tw = text_w(dc, g.f_btn, label), dd = S(8) + S(7);
        int x = (w - tw - dd) / 2;
        radio(dc, x + S(4), h / 2, on, on ? W_TEXT : W_TEXT2);
        text_vc(dc, g.f_btn, on || hot ? W_TEXT : W_TEXT2, x + dd, h / 2, label);
    } else {
        /* Pico on this PC, Save, Apply and save, Clear */
        COLORREF fill = down ? W_SURF : hot ? W_SURF2 : W_SURF2;
        COLORREF bd = hot ? W_FOCUS : W_LINE2, tx = W_TEXT;
        if (id == ID_SAVE && g.saved_flash)
            strcpy(label, "Saved ✓");
        if (id == ID_LOCAL_PICO && g.cfg.loopback_mode)
            bd = W_GREEN_BD;
        if (off) {
            fill = W_SURF;
            bd = W_LINE;
            tx = W_TEXT3;
        }
        round_rect(dc, lr, S(6), fill, bd);
        if (focus) {
            RECT fr = lr;
            InflateRect(&fr, -S(3), -S(3));
            round_rect(dc, fr, S(4), W_FOCUS, W_FOCUS);
            InflateRect(&fr, -S(1), -S(1));
            round_rect(dc, fr, S(3), fill, fill);
        }
        text_in(dc, g.f_btn, tx, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE, label);
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
    fill_solid(d->hDC, r, W_FIELD);

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
    text_vc(d->hDC, g.f_mono_s, W_TEXT3, r.left + S(10), cy, t);
    int ax = r.left + S(112), lx = r.left + S(132);
    if (k == 'd') {
        text_vc(d->hDC, g.f_body, W_TEXT, ax, cy, "↓");
        text_vc(d->hDC, g.f_body, W_TEXT, lx, cy, lab);
    } else if (k == 'u') {
        text_vc(d->hDC, g.f_body, W_TEXT3, ax, cy, "↑");
        text_vc(d->hDC, g.f_body, W_TEXT2, lx, cy, lab);
    } else if (k == 'w') {
        dot(d->hDC, ax + S(4), cy, S(3), W_RED);
        text_vc(d->hDC, g.f_body, W_RED_TX, lx, cy, lab);
    } else {
        dot(d->hDC, ax + S(4), cy, S(3), W_TEXT3);
        text_vc(d->hDC, g.f_body, W_TEXT2, lx, cy, lab);
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
        fill_solid(dc, r, W_FIELD);
        text_in(dc, g.f_label, W_TEXT3, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
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
    if (!g.conn_status[0])
        g.conn_bad = 0;
    else
        msg_touch(MSRC_CONN);
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
        g.conn_bad = 1;
        return 0;
    }
    if (!field_int(g.edit[1], 1, 65535, &port)) {
        connection_status("Settings not saved: port must be 1 to 65535.", 1);
        g.conn_bad = 2;
        return 0;
    }
    if (!field_int(g.edit[2], 10, 1000, &rate)) {
        connection_status("Settings not saved: rate must be 10 to 1000 Hz.", 1);
        g.conn_bad = 3;
        return 0;
    }
    if (!field_int(g.edit[3], 0, 60000, &disarm)) {
        connection_status("Settings not saved: disarm must be 0 (off) to 60000 ms.", 1);
        g.conn_bad = 4;
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
    fill_solid(d->hDC, r, !edit && (d->itemState & ODS_SELECTED) ? W_SEG_ON : W_FIELD);
    char text[128];
    combo_text(d->hwndItem, d->itemID == (UINT)-1 ? -1 : (LRESULT)d->itemID, text, sizeof text);
    r.left = edit ? S(10) : r.left + S(10); /* the closed box: same x as paint_combo */
    text_in(d->hDC, g.f_body, off ? W_TEXT3 : W_TEXT, r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, text);
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
    fill_solid(dc, r, W_BG); /* the pane behind the rounded corners */
    round_rect(dc, r, S(6), W_FIELD, on && GetFocus() == h ? W_FOCUS : on ? W_LINE2 : W_LINE);
    char text[128];
    combo_text(h, SendMessageA(h, CB_GETCURSEL, 0, 0), text, sizeof text);
    RECT tr = {S(10), 0, r.right - S(30), r.bottom};
    text_in(dc, g.f_body, on ? W_TEXT : W_TEXT3, tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, text);
    chevron(dc, r.right - S(16), r.bottom / 2, on ? W_TEXT2 : W_TEXT3);
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
    if (g.input_status[0])
        msg_touch(MSRC_INPUT);
    invalidate_fields();
}

/* Enable rules of the Input tab, from the route in use and the pending
 * Hold to repeat choice. Disabled controls keep their values. */
static void input_tab_refresh(void)
{
    int loop = g.cfg.loopback_mode != 0, rep = g.pending.repeat_enabled != 0;
    for (int i = 0; i < 3; i++)
        if (g.src[i]) {
            EnableWindow(g.src[i], !loop || i == 0); /* Gaming PC: Physical = through the Pico */
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
    msg_sync(); /* the Physical note moves the rows below it */
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
        else pico_notice(message,SEV_ERR);
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
    if(next.loopback_mode)next.loopback_physical=g.pending.loopback_physical;
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
    msg_sync(); /* the message strip follows the tab (the Input hint) */
    if (g.tabs)
        InvalidateRect(g.tabs, NULL, FALSE);
    if (g.hwnd)
        InvalidateRect(g.hwnd, NULL, FALSE);
}

static void local_pico_notice(const char *message)
{
    pico_notice(message, SEV_WARN);
}

/* Pico on this PC: the existing checks, then the Gaming PC route with its
 * own saved values. Never ARMs. The result is shown in the message strip
 * (the Control tab is selected). */
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
        char done[192];
        snprintf(done,sizeof done,
            "Pico on this PC: virtual keys, clicks and wheels -> USB HID.\r\n"
            "Pointer %s. Press ARM when ready.",
            g.cfg.loopback_pointer_pico?"moves through Pico (experimental)":"stays native");
        pico_notice(done,SEV_OK);
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

/* Creation order is the keyboard order, and follows the visual one: the
 * rail first (ARM, Pico on this PC, PANIC), then the tab strip, the update
 * banner's buttons and the controls of the pages in reading order. */
static void create_controls(HWND h)
{
    register_tabs_class();
    g.arm = mk(h, "BUTTON", "ARM", BS_OWNERDRAW | WS_TABSTOP, ID_ARM);
    g.local_pico_button = mk(h, "BUTTON", "Pico on this PC", BS_OWNERDRAW | WS_TABSTOP, ID_LOCAL_PICO);
    g.panic = mk(h, "BUTTON", "PANIC", BS_OWNERDRAW | WS_TABSTOP, ID_PANIC);
    if (g.armed && g.local_pico_button)
        EnableWindow(g.local_pico_button, FALSE); /* same rule as set_armed() */
    g.tabs = CreateWindowExA(0, "HIDwayTabs", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, h,
                             (HMENU)(INT_PTR)ID_TABS, g.inst, NULL);
    /* Update banner under the tab strip; hidden (and so skipped by Tab) until
     * a newer release is found. */
    g.update_now = mk(h, "BUTTON", "Update now", BS_OWNERDRAW | WS_TABSTOP, ID_UPDATE_NOW);
    g.update_later = mk(h, "BUTTON", "Later", BS_OWNERDRAW | WS_TABSTOP, ID_UPDATE_LATER);
    ShowWindow(g.update_now, SW_HIDE);
    ShowWindow(g.update_later, SW_HIDE);

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

    HWND btns[] = {g.clear, g.save, g.arm, g.panic, g.cb[0], g.cb[1], g.cb[2], g.src[0], g.src[1], g.src[2],
                   g.monitor_button, g.replay_button, g.updates_button, g.local_pico_button, g.repeat,
                   g.profile_button[0], g.profile_button[1], g.input_apply, g.update_now, g.update_later};
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++)
        if (btns[i])
            subclass(btns[i], btn_proc);
    HWND combos[] = {g.pointer_route, g.repeat_button, g.repeat_source};
    for (size_t i = 0; i < sizeof combos / sizeof combos[0]; i++)
        if (combos[i])
            subclass(combos[i], combo_proc);

    apply_layout();
    input_tab_load();
    banner_sync();
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
    SendMessageA(g.tooltip, TTM_SETTIPBKCOLOR, W_SURF2, 0); /* no visual styles: colours apply */
    SendMessageA(g.tooltip, TTM_SETTIPTEXTCOLOR, W_TEXT, 0);
    struct { HWND h; const char *text; } tips[] = {
        {g.arm, "ARM: send this PC's keyboard and mouse to the Gaming PC. Press again (or the toggle hotkey) to DISARM."},
        {g.local_pico_button, "Check the local Pico USB devices and the relay address, then select and save the Gaming PC route. Does not ARM."},
        {g.panic, "PANIC: release every key and button immediately and disarm."},
        {g.tabs, "Control, Connection, Input, Tools. Left / Right arrows switch tabs; switching never disarms."},
        {g.profile_button[0], "Remote PC route: this PC controls the Gaming PC. Switching disarms, keeps each route's own settings and saves."},
        {g.profile_button[1], "Gaming PC route: the Pico is connected to this PC; virtual input, and this PC's own keys if Physical is ticked (experimental). Switching disarms, keeps each route's own settings and saves."},
        {g.input_apply, "Disarm, then apply and save the input settings of the route in use."},
        {g.monitor_button, "Open the Input Monitor: devices, live events and recordings."},
        {g.replay_button, "Load a recording and replay it through the Pi and Pico after confirmation."},
        {g.save, "Save the relay address and port of the route in use, and the shared rate and disarm settings."},
        {g.clear, "Clear the recent input list (kept in memory only)."},
        {g.updates_button, "Check for HIDway updates."},
        {g.update_now, "Close HIDway and install the new version: the updater checks, downloads and installs it, then opens HIDway again (disarmed)."},
        {g.update_later, "Hide this until HIDway is opened again."},
    };
    {   /* the message strip: the whole text of a long message */
        TTTOOLINFOA ti = {0};
        ti.cbSize = TTTOOLINFOA_V2_SIZE;
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = hwnd;
        ti.uId = ID_MSG_TIP;
        ti.rect = g.L.msg;
        ti.lpszText = LPSTR_TEXTCALLBACKA;
        SendMessageA(g.tooltip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
    }
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
    COLORREF cap = W_BG, txt = W_TEXT2, bd = W_LINE;
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
            banner_sync();             /* --update-banner */
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
        msg_sync();
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
            mi->itemHeight = (UINT)S(24);
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
        SetBkColor((HDC)wp, W_FIELD);
        SetTextColor((HDC)wp, W_TEXT2);
        return (LRESULT)g.b_field;
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wp, W_FIELD);
        SetTextColor((HDC)wp, W_TEXT);
        return (LRESULT)g.b_field;
    case WM_CTLCOLORSTATIC: /* a greyed-out field stays a field */
        if ((HWND)lp == g.repeat_cps || (HWND)lp == g.click_cps) {
            SetBkColor((HDC)wp, W_FIELD);
            SetTextColor((HDC)wp, W_TEXT3);
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

    case WM_UPDATE_RESULT:
        update_result((int)wp, hw_update_check_tag());
        return 0;

    case WM_NOTIFY: {
        const NMHDR *nh = (const NMHDR *)lp;
        if (nh && g.tooltip && nh->hwndFrom == g.tooltip && nh->code == TTN_GETDISPINFOA && nh->idFrom == ID_MSG_TIP) {
            static char tip[256];
            msg_tip_text(tip, sizeof tip);
            ((NMTTDISPINFOA *)lp)->lpszText = tip;
            return 0;
        }
        break;
    }

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
            case ID_UPDATE_NOW: update_now(); break;
            case ID_UPDATE_LATER: update_later(); break;
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
            /* Gaming PC: Touchpad is not selectable and Virtual is forced on
             * (the boxes are disabled; a queued click changes nothing);
             * Physical there means this PC's keys through the Pico. */
            case ID_SRC_PHYSICAL:
                if (!g.cfg.loopback_mode) g.pending.source_physical = !g.pending.source_physical;
                else g.pending.loopback_physical = !g.pending.loopback_physical;
                g.input_dirty = 1;
                msg_sync();          /* the experimental note moves the rows below it */
                invalidate_fields();
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
            banner_sync(); /* hidden while armed, back after disarm */
            msg_sync();    /* one or two message lines */
            if (g.tabs && g.tab_dirty_drawn != g.input_dirty) { /* the amber dot on the Input tab */
                g.tab_dirty_drawn = g.input_dirty;
                InvalidateRect(g.tabs, NULL, FALSE);
            }
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
        /* --update-banner[=vX.Y.Z]: show the update banner (no check, no updater) */
        const char *banner = strstr(cmd, "--update-banner");
        if (banner) {
            char tag[HW_UPDATE_TAG_MAX] = "v0.6.1";
            if (banner[15] == '=') {
                size_t n = strcspn(banner + 16, " ");
                if (n < sizeof tag) {
                    memcpy(tag, banner + 16, n);
                    tag[n] = 0;
                }
            }
            update_result(HW_UPDATE_AVAILABLE, hw_update_tag_valid(tag) ? tag : "v0.6.1");
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
    g.b_bg = CreateSolidBrush(W_BG);
    g.b_field = CreateSolidBrush(W_FIELD);

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
    update_check_start(); /* window up, disarmed; the check runs on its own thread */

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
