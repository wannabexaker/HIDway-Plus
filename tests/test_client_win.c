/* Regression coverage of production handlers. Hidden fixture controls only;
 * no global hooks or physical HID devices are opened. Peer-address checks
 * connect UDP sockets without sending any packets. */
#define hidway_net_send test_send
#define hidway_net_recv test_recv
#define hidway_net_peer_is_loopback test_peer_is_loopback
#define hidway_local_pico_interfaces test_local_pico_interfaces
#define hidway_config_save test_config_save
/* No test ever starts PowerShell, the updater or a network request. */
#define hw_update_check_begin test_update_check_begin
#define hw_companion_open test_companion_open
#include "../client/src/main.c"
#undef hidway_net_send
#undef hidway_net_recv
#undef hidway_net_peer_is_loopback
#undef hidway_local_pico_interfaces
#undef hidway_config_save
#undef hw_update_check_begin
#undef hw_companion_open

static unsigned fixture_checks, fixture_launches;
static int fixture_launch_target = -1, fixture_launch_result = HW_COMPANION_OK;
int test_update_check_begin(HWND window, UINT message)
{
    (void)window; (void)message; fixture_checks++; return 1;
}
int test_companion_open(HWND owner, int target)
{
    (void)owner; fixture_launches++; fixture_launch_target = target; return fixture_launch_result;
}

int hidway_net_peer_is_loopback(void);
static int fixture_peer,fixture_save_ok=1,fixture_saves;
static unsigned fixture_pico;
static hidway_config_t fixture_saved;
static unsigned fixture_pico_calls;
int test_peer_is_loopback(void) { return fixture_peer; }
unsigned test_local_pico_interfaces(const char *token) { (void)token; fixture_pico_calls++; return fixture_pico; }
int test_config_save(const char *path,const hidway_config_t *cfg)
{
    (void)path;fixture_saves++;fixture_saved=*cfg;return fixture_save_ok;
}

/* Hidden main-window fixture: commands, owner-draw and colour messages go to
 * the production wnd_proc; WM_CREATE does not (no badge, tray, hotkeys or
 * socket from window creation). */
static LRESULT CALLBACK fixture_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_COMMAND || m == WM_DRAWITEM || m == WM_MEASUREITEM || m == WM_CTLCOLOREDIT ||
        m == WM_CTLCOLORSTATIC || m == WM_CTLCOLORLISTBOX)
        return wnd_proc(h, m, w, l);
    return DefWindowProcA(h, m, w, l);
}

typedef struct { HWND h; int tab; } page_control;

/* The control's own visibility, independent of the hidden fixture parent. */
static int shown(HWND h);

/* Controls of the selected tab shown, all others hidden, the always-visible
 * ones (action row, tab strip) shown. Returns the number of mismatches. */
static unsigned visibility_errors(const page_control *p, size_t n, const HWND *always, size_t na)
{
    unsigned errors = 0;
    for (size_t i = 0; i < n; i++)
        if (!p[i].h || shown(p[i].h) != (p[i].tab == g.tab))
            errors++;
    for (size_t i = 0; i < na; i++)
        if (!shown(always[i]))
            errors++;
    return errors;
}

/* Every per-route value of `c` equals the remembered route `p`. */
static int profile_matches(const hidway_route_profile_t *p, const hidway_config_t *c)
{
    return p->valid && !strcmp(p->target, c->target) && p->port == c->port &&
           p->relay_keyboard == c->relay_keyboard && p->relay_mouse == c->relay_mouse &&
           p->source_physical == c->source_physical && p->source_touchpad == c->source_touchpad &&
           p->source_virtual == c->source_virtual && p->loopback_pointer_pico == c->loopback_pointer_pico &&
           p->repeat_enabled == c->repeat_enabled && p->repeat_button == c->repeat_button &&
           p->repeat_cps == c->repeat_cps && p->repeat_sources == c->repeat_sources &&
           p->virtual_click_cps == c->virtual_click_cps;
}

/* Same per-route values in two configurations. */
static int route_values_equal(const hidway_config_t *a, const hidway_config_t *b)
{
    hidway_route_profile_t p;
    memset(&p, 0, sizeof p);
    p.valid = 1;
    strcpy(p.target, a->target);
    p.port = a->port; p.relay_keyboard = a->relay_keyboard; p.relay_mouse = a->relay_mouse;
    p.source_physical = a->source_physical; p.source_touchpad = a->source_touchpad;
    p.source_virtual = a->source_virtual; p.loopback_pointer_pico = a->loopback_pointer_pico;
    p.repeat_enabled = a->repeat_enabled; p.repeat_button = a->repeat_button; p.repeat_cps = a->repeat_cps;
    p.repeat_sources = a->repeat_sources; p.virtual_click_cps = a->virtual_click_cps;
    return profile_matches(&p, b);
}

static hidway_input_pkt_t output[16];
static unsigned output_count;
static hidway_status_pkt_t input[300];
static unsigned input_count, input_at;
static int failures;

int test_send(const uint8_t *buf, size_t len)
{
    if (output_count < 16 && hidway_input_decode(buf, len, &output[output_count]))
        output_count++;
    return (int)len;
}

int test_recv(uint8_t *buf, size_t cap)
{
    (void)cap;
    if (input_at == input_count)
        return 0;
    return (int)hidway_status_encode(buf, &input[input_at++]);
}

#define VERIFY(x)                                                                                  \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL line %d: %s\n", __LINE__, #x);                                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void reset_client(void)
{
    hw_rec_free(&replay.file);memset(&replay,0,sizeof replay);
    memset(&g, 0, sizeof g);
    g.preview = 1; /* prevent real capture shutdown when testing disarm */
    g.armed = g.net_ok = 1;
    g.cfg.relay_keyboard = g.cfg.relay_mouse = 1;
    g.cfg.source_virtual = 1;
    g.cfg.source_physical = 1;
    g.cfg.virtual_click_cps = g.cfg.repeat_cps = 10;
    g.cfg.repeat_sources = 1;
    capture_queue_clear();
    InterlockedExchange(&capture_fault, 0);
    g.cfg.auto_disarm_ms = 3000;
    g.session_id = 456;
    output_count = input_count = input_at = 0;
}

/* The control's own visibility, independent of the hidden fixture parent. */
static int shown(HWND h)
{
    return h && (GetWindowLongPtrA(h, GWL_STYLE) & WS_VISIBLE) != 0;
}

/* A known Remote PC configuration for the window fixture, keeping its
 * controls; preview (no hooks, no ini writes), DISARMED, on Control. */
static void ui_state(void)
{
    hw_rec_free(&replay.file);memset(&replay,0,sizeof replay);
    memset(&g.cfg, 0, sizeof g.cfg);
    strcpy(g.cfg.target, "192.0.2.10");
    g.cfg.port = 47800; g.cfg.send_rate_hz = 250; g.cfg.auto_disarm_ms = 3000;
    g.cfg.relay_keyboard = g.cfg.relay_mouse = 1; g.cfg.source_physical = 1;
    g.cfg.virtual_click_cps = g.cfg.repeat_cps = 10; g.cfg.repeat_sources = 1; g.cfg.history_enabled = 1;
    g.preview = 1; g.armed = 0; g.net_ok = 1; g.session_id = 456; g.toggle_hk_ok = 1;
    output_count = input_count = input_at = 0;
    fixture_saves = 0; fixture_save_ok = 1; fixture_peer = 0;
    g.local_pico_notice[0] = 0;
    if (g.local_pico_button) EnableWindow(g.local_pico_button, TRUE);
    connection_fields_load();
    select_tab(TAB_CONTROL);
    input_tab_load();
}

static void queue_status(uint32_t session, uint32_t echoed_at)
{
    hidway_status_pkt_t *p = &input[input_count++];
    memset(p, 0, sizeof *p);
    p->session_id = session;
    p->client_time_us = echoed_at;
}

/* ---- Rail window: geometry, text fit, the message strip -------------------
 * Positions are checked at 96, 144 and 192 DPI, on all four tabs, disarmed
 * and armed, on both routes, with and without the update banner and with a
 * message that takes two lines. */

/* A child's rectangle in the main window's client coordinates. */
static RECT child_rect(HWND h)
{
    RECT r = {0, 0, 0, 0};
    if (h) {
        GetWindowRect(h, &r);
        MapWindowPoints(NULL, g.hwnd, (POINT *)&r, 2);
    }
    return r;
}

/* A combo's closed box (its window also holds the drop-down list). */
static RECT closed_rect(HWND h)
{
    RECT r = child_rect(h), c;
    char cls[16] = {0};
    GetClassNameA(h, cls, sizeof cls);
    if (!_stricmp(cls, "COMBOBOX") && GetClientRect(h, &c))
        r.bottom = r.top + c.bottom;
    return r;
}

static int inside(RECT in, RECT out)
{
    return in.left >= out.left && in.right <= out.right && in.top >= out.top && in.bottom <= out.bottom;
}

static int touches(RECT a, RECT b)
{
    RECT x;
    return IntersectRect(&x, &a, &b);
}

static size_t page_controls(page_control *p)
{
    page_control all[] = {
        {g.edit[0],TAB_CONNECTION},{g.edit[1],TAB_CONNECTION},{g.edit[2],TAB_CONNECTION},{g.edit[3],TAB_CONNECTION},
        {g.save,TAB_CONNECTION},
        {g.profile_button[0],TAB_INPUT},{g.profile_button[1],TAB_INPUT},{g.cb[0],TAB_INPUT},{g.cb[1],TAB_INPUT},
        {g.src[0],TAB_INPUT},{g.src[1],TAB_INPUT},{g.src[2],TAB_INPUT},{g.pointer_route,TAB_INPUT},{g.repeat,TAB_INPUT},
        {g.repeat_button,TAB_INPUT},{g.repeat_cps,TAB_INPUT},{g.repeat_source,TAB_INPUT},{g.click_cps,TAB_INPUT},
        {g.input_apply,TAB_INPUT},
        {g.monitor_button,TAB_TOOLS},{g.replay_button,TAB_TOOLS},{g.updates_button,TAB_TOOLS},{g.cb[2],TAB_TOOLS},
        {g.clear,TAB_TOOLS},{g.history,TAB_TOOLS},
    };
    memcpy(p, all, sizeof all);
    return sizeof all / sizeof all[0];
}

static const char two_line_notice[] = "Pico keyboard and mouse USB devices were not both found here.\r\n"
                                      "Use this button on the Gaming PC with Pico USB connected.";

/* One combination: DPI, banner, a two-line message, tab, route (Gaming PC
 * also ticks Physical, which adds the experimental note), disarmed or armed. */
static void layout_case(UINT dpi, int banner, int two_lines, int tab, int gaming, int armed)
{
    g.armed = 0;
    ui_state();
    g.dpi = dpi;
    create_fonts();
    g.cfg.loopback_mode = gaming;
    g.cfg.loopback_physical = gaming;
    strcpy(g.update_tag, "v0.6.1");
    strcpy(g.cfg.toggle.text, "ctrl+shift+f12");
    g.update_offer = banner ? 1 : 2;
    g.armed = armed;
    g.armed_at_ms = timeGetTime() - 83000;
    g.status_seen = armed;
    input_tab_load();
    if (two_lines)
        pico_notice(two_line_notice, SEV_WARN);
    else
        g.local_pico_notice[0] = 0;
    select_tab(tab);
    banner_sync();
    compute_layout();
    apply_layout();
    msg_sync();
    compute_layout();
    apply_layout();
}

/* The layout is consistent: everything inside its area, nothing overlapping. */
#define ECHK(x) do { unsigned v_ = (unsigned)!!(x); if (v_) printf("  check line %d\n", __LINE__); e += v_; } while (0)
static unsigned layout_errors(void)
{
    layout_t *L = &g.L;
    unsigned e = 0;
    RECT x;
    int one = S(1);

    /* rail: ARM, Pico on this PC, divider, PANIC from top to bottom */
    RECT a = child_rect(g.arm), p = child_rect(g.local_pico_button), n = child_rect(g.panic), t = child_rect(g.tabs);
    ECHK(!EqualRect(&a, &L->arm) || !EqualRect(&p, &L->local_pico) || !EqualRect(&n, &L->panic) || !EqualRect(&t, &L->tabs));
    ECHK(!inside(a, L->rail) || !inside(p, L->rail) || !inside(n, L->rail));
    ECHK(!(a.bottom <= p.top && p.bottom < L->div_y && L->div_y < n.top && n.bottom <= L->H));
    ECHK(!(L->rail.right <= L->tabs.left && L->rail.right == S(208) && L->W == S(700) && L->H == S(432)));
    for (int i = 0; i < 4; i++)
        ECHK(!(L->fact_cy[i] > L->state.bottom && L->fact_cy[i] + S(10) < a.top));
    ECHK(!(L->state.bottom < L->fact_cy[0] && L->mark.bottom < L->state.top));

    /* pane: tab strip, banner, body, one message strip */
    ECHK(!inside(L->body, L->page) || !inside(L->msg, L->page) || !(L->tabs.bottom <= L->body.top));
    ECHK(L->msg.top - L->body.bottom != S(10) || L->msg.bottom != S(420));
    ECHK(!(L->msg.bottom - L->msg.top == S(g.msg_lines == 2 ? 46 : 34) || abs((L->msg.bottom - L->msg.top) - S(g.msg_lines == 2 ? 46 : 34)) <= 1));
    if (banner_visible()) {
        ECHK(!inside(L->banner, L->page) || touches(L->banner, L->tabs) || !(L->banner.bottom <= L->body.top));
        ECHK(!inside(L->banner_now, L->banner) || !inside(L->banner_later, L->banner) || touches(L->banner_now, L->banner_later));
        ECHK(!shown(g.update_now) || !shown(g.update_later));
        ECHK(!(L->banner_later.right == L->banner.right - S(6) && L->banner.top >= L->tabs.bottom + S(8)));
    } else {
        ECHK(shown(g.update_now) || shown(g.update_later));
    }

    /* the visible page controls sit inside the body and never overlap */
    page_control pc[25];
    size_t np = page_controls(pc);
    RECT seen[25];
    size_t ns = 0;
    for (size_t i = 0; i < np; i++) {
        if (pc[i].tab != g.tab)
            continue;
        RECT r = closed_rect(pc[i].h);
        ECHK(!shown(pc[i].h) || !inside(r, L->body));
        if (g.tab == TAB_INPUT && pc[i].h == g.input_apply)
            ECHK(!(r.bottom <= L->body.bottom));
        for (size_t k = 0; k < ns; k++)
            ECHK(touches(r, seen[k]));
        seen[ns++] = r;
    }
    if (g.tab == TAB_CONTROL) {
        for (int i = 0; i < 4; i++)
            ECHK(!inside(L->tiles[i], L->body) || (i && L->tiles[i].left < L->tiles[i - 1].right + S(7)));
        ECHK(!inside(L->live, L->body) || touches(L->live, L->tiles[3]) || L->live.bottom != L->body.bottom);
        ECHK(!(L->live_head_cy - S(9) >= L->tiles[0].bottom && L->live_head_cy + S(9) <= L->live.top));
        ECHK(!(L->head_cy + S(9) <= L->tiles[0].top && L->head_cy - S(10) >= L->body.top));
        live_metrics_t m;
        for (int armed = 0; armed < 2; armed++) {
            live_metrics(armed, L->live, &m);
            ECHK(!(m.bottom <= L->live.bottom && m.row0 > m.div));
        }
    } else if (g.tab == TAB_CONNECTION) {
        for (int i = 0; i < 4; i++)
            ECHK(!inside(L->frame[i], L->body));
        ECHK(!inside(L->save, L->body) || touches(L->save, L->frame[3]) || touches(L->frame[0], L->frame[1]));
        ECHK(!(L->conn_hint_cy + S(8) <= L->body.bottom && L->link_cy + 2 * S(20) + S(10) <= L->body.bottom));
        ECHK(!(L->conn_head_cy[2] + S(10) < L->link_cy - S(8) && L->conn_hint_cy + S(8) < L->conn_head_cy[2] - S(8)));
    } else if (g.tab == TAB_INPUT) {
        RECT ptr = closed_rect(g.pointer_route), rs = closed_rect(g.repeat_source), rb = closed_rect(g.repeat_button);
        ECHK(!inside(L->seg, L->body) || !inside(L->profile[0], L->seg) || !inside(L->profile[1], L->seg));
        ECHK(!inside(L->limited, L->body) || !inside(L->apply, L->body) || !inside(L->vr, L->body));
        ECHK(!inside(ptr, L->body) || !inside(rs, L->body) || !inside(rb, L->body));
        ECHK(!(ptr.bottom - ptr.top >= S(24) && ptr.bottom - ptr.top <= S(32)));
        ECHK(touches(L->limited, L->apply) || !inside(L->repeat_cps, L->body) || !inside(L->click_cps, L->body));
        ECHK(L->note_shown != (g.cfg.loopback_mode && g.pending.loopback_physical));
        if (L->note_shown)
            ECHK(!inside(L->note, L->body) || !(L->note.bottom <= L->src[1].top) || !(L->src[0].bottom <= L->note.top));
        ECHK(L->vr.left < L->seg.right + S(10) || L->vr.right > L->repeat.left - S(10));
    } else {
        for (int i = 0; i < 3; i++) {
            RECT b = i == 0 ? L->monitor_btn : i == 1 ? L->replay_btn : L->updates;
            ECHK(!inside(b, L->body));
        }
        ECHK(touches(L->monitor_btn, L->replay_btn) || touches(L->replay_btn, L->updates));
        ECHK(!(L->monitor_btn.bottom + S(8) < L->hk_cy && L->hk_cy + S(10) < L->rh_cy - S(10)));
        e += !inside(L->list, L->body) || L->list.bottom != L->body.bottom || !(L->clear.bottom <= L->list.top) ||
             !(L->rh_cy + S(12) <= L->list.top) || !(L->list.bottom - L->list.top >= S(100));
        ECHK(touches(L->history_cb, L->clear));
    }
    (void)x;
    (void)one;
    return e;
}

/* The glyph and label of every rail fact, the state block and the buttons fit
 * their boxes (an ellipsis would hide text: it is not allowed to happen for
 * any state or message of the application). */
static unsigned text_errors(HDC dc)
{
    layout_t *L = &g.L;
    unsigned e = 0;
    int value_w = L->rail.right - S(16) - (L->fact_vx + S(17));
    static const char *const facts[] = {"reachable", "no reply", "probing", "socket error", "invalid key", "waiting",
                                         "linked", "link lost", "On this PC", "Partly detected", "Not on this PC",
                                         "Checking...", "Gaming PC on", "Gaming PC off", "Gaming PC off?"};
    for (size_t i = 0; i < sizeof facts / sizeof facts[0]; i++)
        ECHK(text_w(dc, g.f_fact, facts[i]) > value_w);
    static const char *const targets[] = {"On", "Off", "Asleep", "Waking", "Off?", "—"};
    for (size_t i = 0; i < sizeof targets / sizeof targets[0]; i++)
        ECHK(text_w(dc, g.f_fact, targets[i]) > value_w);
    ECHK(text_w(dc, g.f_btn, "Remote PC") > L->rail.right - S(16) - L->fact_vx);
    ECHK(text_w(dc, g.f_btn, "Gaming PC") > L->rail.right - S(16) - L->fact_vx);
    ECHK(tracked_w(dc, g.f_section, "TARGET") + S(6) > L->fact_vx - L->fact_x);
    /* state block */
    int tx = L->state.left + S(12 + 14 + 11), inner = L->state.right - S(12);
    ECHK(tx + text_w(dc, g.f_pill, "DISARMED") > inner);
    ECHK(tx + text_w(dc, g.f_pill, "ARMED") + S(6) + text_w(dc, g.f_timer, "00:00") > inner);
    ECHK(tx + text_w(dc, g.f_pill, "ARMED") + S(6) + text_w(dc, g.f_mono, "9:59:59") > inner); /* hours: smaller face */
    static const char *const subs[] = {"Remote PC route", "Gaming PC route", "To the Remote PC", "To the Gaming PC"};
    for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++)
        ECHK(tx + text_w(dc, g.f_label, subs[i]) > inner);
    ECHK(L->mark.right + S(9) + text_w(dc, g.f_title, "HIDway") + S(8) + text_w(dc, g.f_label, "v10.20.300") > L->rail.right - S(16));
    /* buttons: ARM (two lines), Pico on this PC, PANIC */
    int aw = L->arm.right - L->arm.left - S(16);
    ECHK(text_w(dc, g.f_btn_big, "DISARM") + 6 * S(1) > aw);
    ECHK(text_w(dc, g.f_arm_sub, "Ctrl+Alt+Shift+F12") > aw);
    ECHK(font_h(dc, g.f_btn_big) + font_h(dc, g.f_arm_sub) > L->arm.bottom - L->arm.top);
    ECHK(text_w(dc, g.f_btn, "Pico on this PC") > L->local_pico.right - L->local_pico.left - S(16));
    ECHK(S(14) + S(9) + text_w(dc, g.f_panic, "PANIC") + 5 * S(2) > L->panic.right - L->panic.left);
    /* tabs: all four with the Input dot, inside the strip */
    int was = g.input_dirty;
    RECT rc[TAB_COUNT];
    POINT dp;
    g.input_dirty = 1;
    tabs_layout(dc, rc, &dp);
    g.input_dirty = was;
    ECHK(rc[TAB_COUNT - 1].right > L->tabs.right - L->tabs.left - S(12));
    /* tiles: values, labels, the WAKE chip clear of the value */
    for (int i = 0; i < 4; i++) {
        int w = L->tiles[i].right - L->tiles[i].left;
        ECHK(tracked_w(dc, g.f_section, i == 3 ? "GAMING PC" : i == 0 ? "RTT" : i == 1 ? "LOSS" : "RATE") + S(22) > w);
        ECHK(text_w(dc, g.f_tile, i == 0 ? "999 ms" : i == 1 ? "99.9 %" : i == 2 ? "1000/s" : "Asleep") + S(22) > w);
    }
    ECHK(text_w(dc, g.f_tile, "Off") + S(11) + S(8) >= L->tiles[3].right - L->tiles[3].left - S(9) - text_w(dc, g.f_chip, "WAKE") - 2 * S(8));
    /* Control head: CONNECTION, chip, status and a typical address in one row */
    int head = tracked_w(dc, g.f_section, "CONNECTION") + S(10) + text_w(dc, g.f_chip, "E2E OFF") + 2 * S(8) + S(12) +
               text_w(dc, g.f_label, "linked · serial open") + S(24) + text_w(dc, g.f_mono_s, "255.255.255.255:65535");
    ECHK(head > L->body.right - L->body.left);
    e += text_w(dc, g.f_semi, "Input capture is off") + S(8) + text_w(dc, g.f_label, "Press ARM or Ctrl+Shift+F12 to start relaying") >
         L->live.right - L->live.left - S(28);
    ECHK(S(110) + text_w(dc, g.f_fact, "Keyboard + mouse to the Remote PC") > L->live.right - L->live.left - S(28));
    ECHK(S(110) + text_w(dc, g.f_fact, "Virtual, Physical through Pico") > L->live.right - L->live.left - S(28));
    ECHK(S(110) + text_w(dc, g.f_fact, "repeat Forward 25/s, 25/s") > L->live.right - L->live.left - S(28));
    /* Connection */
    int bw = L->body.right - L->body.left;
    ECHK(text_w(dc, g.f_label, "Relay address") > L->frame[0].right - L->frame[0].left);
    ECHK(text_w(dc, g.f_label, "Send rate (Hz)") + S(8) > L->frame[3].left - L->frame[2].left);
    ECHK(text_w(dc, g.f_label, "Auto-disarm (ms)") > L->frame[3].right - L->frame[3].left);
    ECHK(tracked_w(dc, g.f_section, "GAMING PC ROUTE") + S(10) + text_w(dc, g.f_label, "saved separately for each route") > bw);
    ECHK(text_w(dc, g.f_label, "Save disarms first. A wrong value is refused and nothing changes.") > bw);
    ECHK(S(110) + text_w(dc, g.f_fact, "KEY ERROR: invalid key in hidway.ini") > bw);
    ECHK(S(17) + text_w(dc, g.f_fact, "linked · serial open, RTT 999 ms") + S(110) > bw);
    ECHK(text_w(dc, g.f_btn, "Saved ✓") + S(16) > L->save.right - L->save.left);
    /* Input */
    int lw = L->seg.right - L->seg.left, rw = L->repeat.right - L->repeat.left;
    ECHK(text_w(dc, g.f_btn, "Remote PC") + S(15) + S(10) > L->profile[0].right - L->profile[0].left);
    ECHK(text_w(dc, g.f_btn, "Gaming PC") + S(15) + S(10) > L->profile[1].right - L->profile[1].left);
    ECHK(text_w(dc, g.f_sub, "The Pico is plugged into this PC.") > lw);
    ECHK(text_w(dc, g.f_label, "Gaming PC pointer") > lw);
    static const struct { const char *s; int box; } boxes[] = {{"Keyboard", 0}, {"Mouse", 1}, {"Physical", 2}, {"Touchpad", 3}, {"Virtual / remote", 4}};
    for (int i = 0; i < 5; i++) {
        RECT b = i == 0 ? L->cb[0] : i == 1 ? L->cb[1] : i == 2 ? L->src[0] : i == 3 ? L->src[1] : L->src[2];
        ECHK(S(1) + S(16) + S(8) + text_w(dc, g.f_body, boxes[i].s) > b.right - b.left);
    }
    ECHK(S(25) + text_w(dc, g.f_body, "Hold to repeat") > L->repeat.right - L->repeat.left);
    ECHK(text_w(dc, g.f_label, "Button") + S(8) + text_w(dc, g.f_label, "Repeat clicks/sec") > rw);
    ECHK(text_w(dc, g.f_label, "Repeat source") > rw || text_w(dc, g.f_label, "Virtual clicks/sec") > rw);
    ECHK(text_w(dc, g.f_label, "1-25 per button") + S(8) > L->body.right - L->click_cps.right);
    ECHK(text_lines(dc, g.f_sub, "Last ARM: 999999 extra click attempts limited", rw) > 2);
    ECHK(text_lines(dc, g.f_sub, "Experimental: keys, buttons and wheel of this PC go through the Pico.", S(230)) > 2);
    ECHK(font_h(dc, g.f_sub) * 2 > S(32));
    ECHK(text_w(dc, g.f_btn, "Apply and save") + S(16) > L->apply.right - L->apply.left);
    /* Tools */
    int tw = L->monitor_btn.right - L->monitor_btn.left;
    static const char *const titles[] = {"Monitor", "Replay...", "Updates", "Stop"};
    static const char *const subs2[] = {"devices and events", "play a recording", "check for updates", "stop playback"};
    for (int i = 0; i < 4; i++)
        ECHK(S(22) + text_w(dc, g.f_semi, titles[i]) > tw || S(22) + text_w(dc, g.f_sub, subs2[i]) > tw);
    ECHK(font_h(dc, g.f_semi) + font_h(dc, g.f_sub) > L->monitor_btn.bottom - L->monitor_btn.top);
    ECHK(text_w(dc, g.f_label, "Toggle") > S(46) || text_w(dc, g.f_label, "Panic") > S(42));
    {   /* the two hotkey groups, widest plausible, in one row */
        int x = 0;
        for (int k = 0; k < 2; k++) {
            x += k == 0 ? S(46) : S(42);
            const char *const toks_t[] = {"Ctrl", "Shift", "F12"}, *const toks_p[] = {"Ctrl", "Alt", "Shift", "End"};
            const char *const *toks = k == 0 ? toks_t : toks_p;
            for (int i = 0; i < (k == 0 ? 3 : 4); i++)
                x += text_w(dc, g.f_chip, toks[i]) + 2 * S(8) + S(5);
            x += S(13);
        }
        ECHK(x > bw);
    }
    e += tracked_w(dc, g.f_section, "RECENT INPUT") + S(10) + text_w(dc, g.f_label, "memory only") + S(8) >
         L->history_cb.left - L->body.left;
    /* banner text and buttons */
    int room = L->banner_now.left - S(8) - (L->banner.left + S(35));
    ECHK(text_w(dc, g.f_btn, "HIDway v10.20.300 is available") > room);
    ECHK(text_w(dc, g.f_btn, "Preview: the updater is not started") > room);
    ECHK(text_w(dc, g.f_btn, "The updater did not start") > room);
    ECHK(text_w(dc, g.f_chip, "Update now") + S(10) > L->banner_now.right - L->banner_now.left);
    ECHK(text_w(dc, g.f_chip, "Later") + S(10) > L->banner_later.right - L->banner_later.left);
    ECHK(font_h(dc, g.f_chip) > L->banner_now.bottom - L->banner_now.top);
    return e;
}

/* Every message of the application takes at most two lines of the strip. */
static unsigned message_fit_errors(HDC dc)
{
    unsigned e = 0;
    static const char *const messages[] = {
        "Settings not saved: enter the relay address.",
        "Settings not saved: port must be 1 to 65535.",
        "Settings not saved: rate must be 10 to 1000 Hz.",
        "Settings not saved: disarm must be 0 (off) to 60000 ms.",
        "Could not save hidway.ini. Settings have not changed.",
        "Settings not saved: clicks/sec must be 1 to 25.",
        "Settings not saved: the repeat source includes a source that is not ticked under Sources.",
        "Saved. Press ARM when ready.",
        "Gaming PC route selected and saved. Press ARM when ready.",
        "Remote PC route selected and saved. Press ARM when ready.",
        "DISARM before selecting Pico on this PC.",
        "Save the Pi relay address and port on the Connection tab first.",
        "Save the Pi relay address on the Connection tab first.\r\n127.0.0.1 / localhost cannot forward USB HID through the Pi.",
        two_line_notice,
        "The Gaming PC route uses relay 192.168.100.10:47800. Select Gaming PC on the Input tab and check its address on the Connection tab.",
        "Pico on this PC: virtual keys, clicks and wheels -> USB HID.\r\nPointer moves through Pico (experimental). Press ARM when ready.",
        "Physical through the Pico: the Pico is not on this PC - not armed",
        "The Pico did not return the input - auto-disarmed",
        "Input loop through the Pico detected - auto-disarmed",
        "Toggle hotkey Ctrl+Alt+Shift+F12 is in use — set toggle_hotkey in hidway.ini",
        "Armed. Toggle with Ctrl+Alt+Shift+F12. PANIC releases every key and button.",
        "Ready. Nothing is sent until you press ARM.",
        "Changes not applied yet. Apply and save disarms first, then saves.",
        "Changes not applied yet. Programs that read Raw Input still see this PC's own keyboard and mouse too.",
        "Programs that read Raw Input still see this PC's own keyboard and mouse too. The pointer stays native. Apply and save disarms first.",
        "Apply and save disarms first; changes not applied are dropped when you leave this tab. Hold to repeat replaces dragging for its button.",
    };
    for (size_t i = 0; i < sizeof messages / sizeof messages[0]; i++) {
        int lines = text_lines(dc, g.f_fact, messages[i], msg_text_w());
        if (lines > 2)
            printf("message takes %d lines at %u DPI: %s\n", lines, g.dpi, messages[i]);
        e += lines > 2;
    }
    return e;
}

/* The client area painted into a bitmap, for pixel checks. */
static HBITMAP paint_bitmap(HDC *mdc_out)
{
    HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, g.L.W, g.L.H);
    SelectObject(mdc, bm);
    RECT all = {0, 0, g.L.W, g.L.H};
    paint_content(mdc, all);
    ReleaseDC(NULL, sdc);
    *mdc_out = mdc;
    return bm;
}

static void free_bitmap(HDC mdc, HBITMAP bm)
{
    DeleteDC(mdc);
    DeleteObject(bm);
}

/* Optional: HIDWAY_TEST_SHOTS=<folder> writes screenshots of the states the
 * previews cannot reach (messages), through the production save_screenshot(). */
static void test_shot(const char *name)
{
    const char *dir = getenv("HIDWAY_TEST_SHOTS");
    if (!dir || !dir[0])
        return;
    RECT r = {0, 0, g.L.W, g.L.H};
    AdjustWindowRectExForDpi(&r, WIN_STYLE, FALSE, 0, g.dpi);
    SetWindowPos(g.hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    snprintf(g.shot_path, sizeof g.shot_path, "%s\\%s.bmp", dir, name);
    save_screenshot(g.hwnd);
    g.shot_path[0] = 0;
}

int main(void)
{
    reset_client();
    uint32_t now = hidway_now_us();
    queue_status(g.session_id, now - 80000);
    queue_status(g.session_id, now - 3080000);
    net_drain_status();
    VERIFY(g.rtt_ms >= 80 && g.rtt_ms < 250);
    VERIFY(g.last_status_us == now - 80000);
    uint32_t lease = g.last_status_us;
    input_at = input_count = 0;
    queue_status(g.session_id, lease);   /* duplicate cannot renew health */
    queue_status(g.session_id + 1, now); /* another session cannot renew it */
    net_drain_status();
    VERIFY(g.last_status_us == lease);
    input_at = input_count = 0;
    queue_status(g.session_id, hidway_now_us() + 1000000);  /* impossible future echo */
    queue_status(g.session_id, hidway_now_us() - 11000000); /* expired */
    net_drain_status();
    VERIFY(g.last_status_us == lease);
    input_at = input_count = 0;
    g.last_status_received_ms = timeGetTime() - 1800000u;
    g.status_order.last_echo_us = hidway_now_us() - 0x90000000u;
    queue_status(g.session_id, hidway_now_us() - 80000);
    net_drain_status();
    VERIFY(g.rtt_ms < 250); /* recover after a long disconnected interval */

    reset_client();
    now = hidway_now_us();
    for (unsigned i = 0; i < 64; i++)
        queue_status(g.session_id, now - 3080000 + i * (3000000 / 63));
    net_drain_status();
    VERIFY(input_at == input_count && g.rtt_ms < 250);
    /* The receive budget yields after 256 packets, with state preserved. */
    reset_client();
    for (unsigned i = 0; i < 300; i++)
        queue_status(g.session_id, now - 400000 + i * 1000);
    net_drain_status();
    VERIFY(input_at == 256);
    net_drain_status();
    VERIFY(input_at == 300);

    reset_client();
    g.armed_at_ms = timeGetTime() - 4000;
    net_tick();
    VERIFY(!g.armed && output_count == 4); /* initial state, three RELEASE */
    reset_client();
    queue_status(g.session_id, hidway_now_us() - 4000000);
    net_tick();
    VERIFY(!g.armed); /* an old queued reply cannot keep capture healthy */

    reset_client();
    on_capture_mouse(WM_LBUTTONDOWN, 0);
    on_capture_mouse(WM_LBUTTONUP, 0);
    net_send_state(HIDWAY_MSG_STATE);
    g.events.shown_at -= 16u;
    net_send_state(HIDWAY_MSG_STATE);
    VERIFY(output_count == 2 && output[0].buttons == 1 && output[1].buttons == 0);
    set_armed(0);
    VERIFY(!g.buttons && !g.events.count);
    VERIFY(output[output_count - 1].type == HIDWAY_MSG_RELEASE);
    reset_client();
    for (unsigned i = 0; i < 10000; i++) {
        on_capture_mouse(WM_LBUTTONDOWN, 0);
        on_capture_mouse(WM_LBUTTONUP, 0);
    }
    VERIFY(g.armed && !g.buttons && g.events.count <= 4);
    VERIFY(g.pulse[2][0].limited > 9000);
    set_armed(0);
    VERIFY(output_count == 3 && output[2].type == HIDWAY_MSG_RELEASE);

    reset_client();
    on_capture_key('A', 0x1e);
    on_capture_key('A', 0x1e | KEYF_VIRTUAL);
    on_capture_key('A', 0x1e | KEYF_VIRTUAL | KEYF_UP);
    VERIFY(hidway_bitmap_test(g.keys, 4));
    on_capture_key('A', KEYF_UP);
    VERIFY(!hidway_bitmap_test(g.keys, 4));
    on_capture_key(VK_LSHIFT, 0x2a);
    on_capture_key(VK_LSHIFT, 0x2a | KEYF_VIRTUAL);
    on_capture_key(VK_LSHIFT, KEYF_VIRTUAL | KEYF_UP);
    VERIFY(g.modifiers == 2);
    on_capture_key(VK_LSHIFT, KEYF_UP);
    VERIFY(!g.modifiers);
    on_capture_key('C', KEYF_VIRTUAL);
    VERIFY(hidway_bitmap_test(g.keys, 6));
    on_capture_key('C', KEYF_VIRTUAL | KEYF_UP);
    VERIFY(!hidway_bitmap_test(g.keys, 6));
    on_capture_key(VK_PACKET, 0x41 | KEYF_VIRTUAL);
    VERIFY(!hidway_bitmap_test(g.keys, 0x40));
    VERIFY(g.unicode_warned);
    reset_client();
    queue_status(g.session_id, hidway_now_us() - 1000);
    input[0].flags = HIDWAY_STATUS_INPUT_FAULT;
    net_drain_status();
    VERIFY(!g.armed && output_count == 3 && output[2].type == HIDWAY_MSG_RELEASE);

    reset_client();
    on_capture_key('A', 0x1e);
    on_capture_key('A', 0x1e | KEYF_UP);
    net_send_state(HIDWAY_MSG_STATE);
    g.events.shown_at -= HIDWAY_KEY_DWELL_MS;
    net_send_state(HIDWAY_MSG_STATE);
    VERIFY(output_count == 2 && hidway_bitmap_test(output[0].keys, 4) &&
           !hidway_bitmap_test(output[1].keys, 4));
    reset_client();
    on_capture_key(VK_LCONTROL, 0x1d);
    on_capture_key(VK_LMENU, 0x38);
    on_capture_mouse(WM_LBUTTONDOWN, 0);
    on_capture_mouse(WM_LBUTTONUP, 0);
    on_capture_key(VK_LMENU, 0x38 | KEYF_UP);
    on_capture_key(VK_LCONTROL, 0x1d | KEYF_UP);
    for (unsigned i = 0; i < 6; i++) {
        g.events.shown_at -= 16;
        net_send_state(HIDWAY_MSG_STATE);
    }
    VERIFY(output_count == 6 && output[0].mods == 1 && output[1].mods == 5);
    VERIFY(output[2].mods == 5 && output[2].buttons == 1 && output[3].mods == 5 &&
           !output[3].buttons);
    VERIFY(output[4].mods == 1 && output[5].mods == 0);
    reset_client();
    for (unsigned i = 0; i < 3000; i++) {
        on_capture_mouse(WM_LBUTTONDOWN, 0);
        on_capture_mouse(WM_LBUTTONUP, 0);
        on_capture_mouse(WM_RBUTTONDOWN, 0);
        on_capture_mouse(WM_RBUTTONUP, 0);
        on_capture_mouse(WM_MBUTTONDOWN, 0);
        on_capture_mouse(WM_MBUTTONUP, 0);
        on_capture_mouse(WM_XBUTTONDOWN, MAKELPARAM(0, XBUTTON1));
        on_capture_mouse(WM_XBUTTONUP, MAKELPARAM(0, XBUTTON1));
        net_send_state(HIDWAY_MSG_STATE);
    }
    VERIFY(g.armed && g.events.count <= 4 && !g.buttons);

    reset_client();
    g.generation = 2;
    InterlockedExchange(&capture_generation, 1);
    capture_enqueue(NULL, WM_CAPTURE_READY, WM_CAPTURE_KEY, 'A', 0x1e);
    drain_capture();
    VERIFY(!hidway_bitmap_test(g.keys, 4));
    InterlockedExchange(&capture_generation, 2);
    capture_enqueue(NULL, WM_CAPTURE_READY, WM_CAPTURE_KEY, 'A', 0x1e);
    drain_capture();
    VERIFY(hidway_bitmap_test(g.keys, 4));
    InterlockedExchange(&capture_fault, 1);
    net_tick();
    VERIFY(!g.armed && !g.keys[0]);

    reset_client();
    VERIFY(!capture_enqueue((HWND)(INT_PTR)-12345, WM_CAPTURE_READY, WM_CAPTURE_KEY, 'A', 0x1e));
    net_tick();
    VERIFY(!g.armed && output_count == 3 && output[2].type == HIDWAY_MSG_RELEASE);
    reset_client();
    for (unsigned i = 0; i < HIDWAY_CAPTURE_CAPACITY + 1; i++)
        capture_enqueue(NULL, WM_CAPTURE_READY, WM_CAPTURE_KEY, 'A', 0x1e);
    net_tick();
    VERIFY(!g.armed && !g.keys[0]);
    reset_client();
    for (unsigned i = 0; i < 100; i++) {
        on_capture_key('A', 0x1e);
        on_capture_key('A', 0x1e | KEYF_UP);
    }
    VERIFY(!g.armed && output_count == 3 && !g.events.count);
    reset_client();
    cap_mouse_ours = 0;
    cap_active = cap_mouse = 1;
    cap_sources = HIDWAY_SOURCE_VIRTUAL;
    cap_require_badge = 0;
    cap_consume_virtual = 1;
    cap_badge = NULL;
    MSLLHOOKSTRUCT mouse = {0};
    mouse.flags = LLMHF_INJECTED;
    capture_mouse_hook(HC_ACTION, WM_LBUTTONUP, (LPARAM)&mouse);
    VERIFY(!capture_events.count); /* release of a pre-ARM native hold must pass locally */
    VERIFY(capture_mouse_hook(HC_ACTION, WM_LBUTTONDOWN, (LPARAM)&mouse) == 1);
    VERIFY(cap_mouse_ours == BTN_L && capture_events.count == 1);
    VERIFY(capture_mouse_hook(HC_ACTION, WM_LBUTTONUP, (LPARAM)&mouse) == 1);
    VERIFY(!cap_mouse_ours && capture_events.count == 2);
    cap_active = 0;
    capture_queue_clear();

    reset_client();cap_active=cap_mouse=1;cap_sources=HIDWAY_SOURCE_VIRTUAL;
    cap_require_badge=0;cap_consume_virtual=1;cap_route_motion=0;
    mouse.flags=LLMHF_INJECTED;mouse.pt.x=100;mouse.pt.y=100;
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEMOVE,(LPARAM)&mouse)==0);
    VERIFY(!capture_events.count); /* native pointer must never also move Pico */
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEWHEEL,(LPARAM)&mouse)==1);
    VERIFY(capture_events.count==1); /* wheel still goes through Pico */
    capture_queue_clear();cap_route_motion=1;
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEMOVE,(LPARAM)&mouse)==1);
    VERIFY(capture_events.count==1);
    cap_active=0;capture_queue_clear();

    reset_client();
    g.cfg.loopback_mode = 1;
    on_capture_key('A', 0x1e);
    VERIFY(!hidway_bitmap_test(g.keys, 4));
    on_capture_key('A', 0x1e | KEYF_VIRTUAL);
    VERIFY(hidway_bitmap_test(g.keys, 4));
    on_capture_key('A', KEYF_UP | KEYF_VIRTUAL);
    on_capture_key(VK_RCONTROL, KEYF_VIRTUAL);
    VERIFY(g.modifiers == 0x10);
    on_capture_key(VK_RCONTROL, KEYF_VIRTUAL | KEYF_UP);
    VERIFY(!g.modifiers);
    on_capture_key(VK_LEFT, KEYF_VIRTUAL);
    VERIFY(hidway_bitmap_test(g.keys, 0x50));
    on_capture_key(VK_LEFT, KEYF_VIRTUAL | KEYF_UP);
    VERIFY(!hidway_bitmap_test(g.keys, 0x50));
    on_capture_key(VK_NUMPAD4, KEYF_VIRTUAL);
    VERIFY(hidway_bitmap_test(g.keys, 0x5c));
    on_capture_key(VK_NUMPAD4, KEYF_VIRTUAL | KEYF_UP);
    VERIFY(!hidway_bitmap_test(g.keys, 0x5c));

    reset_client();
    cap_active=cap_keyboard=1;cap_sources=HIDWAY_SOURCE_VIRTUAL;cap_require_badge=0;
    memset(cap_ours,0,sizeof cap_ours);
    KBDLLHOOKSTRUCT key={0};key.vkCode='A';key.scanCode=0x1e;key.flags=LLKHF_INJECTED;
    VERIFY(capture_hook(HC_ACTION,WM_KEYDOWN,(LPARAM)&key)==1);
    key.vkCode='Q';key.flags|=LLKHF_UP; /* layout changed during the hold */
    VERIFY(capture_hook(HC_ACTION,WM_KEYUP,(LPARAM)&key)==1);
    VERIFY(capture_events.count==2&&capture_events.events[0].w=='A'&&capture_events.events[1].w=='A');
    cap_active=0;capture_queue_clear();

    /* ---- Gaming PC, physical through the Pico (loopback_physical) ---- */
    /* Off (the default): the arm guard is not consulted, hardware keys and
     * buttons pass the hooks, physical keys are ignored, no credits. */
    reset_client();g.armed=0;g.cfg.loopback_mode=1;g.cfg.source_physical=1; /* a leftover Remote PC choice */
    fixture_pico=0;fixture_pico_calls=0;
    set_armed(1);
    VERIFY(g.armed&&!g.loop_phys&&fixture_pico_calls==0);
    on_capture_key('A',0x1e);
    net_send_state(HIDWAY_MSG_STATE);
    VERIFY(!hidway_bitmap_test(g.keys,4)&&!hidway_echo_live(&cap_echo,4,false,timeGetTime())&&!cap_echo.sent.keys[0]);
    on_capture_key('B',0x30|KEYF_VIRTUAL);
    net_send_state(HIDWAY_MSG_STATE);
    VERIFY(hidway_bitmap_test(g.keys,5)&&!hidway_echo_live(&cap_echo,5,false,timeGetTime())); /* no grants */
    set_armed(0);
    cap_active=cap_keyboard=cap_mouse=1;cap_sources=HIDWAY_SOURCE_VIRTUAL;cap_require_badge=0;cap_consume_virtual=1;
    cap_loop_phys=0;cap_badge=NULL;memset(cap_ours,0,sizeof cap_ours);capture_queue_clear();
    key=(KBDLLHOOKSTRUCT){0};key.vkCode='A';key.scanCode=0x1e;
    VERIFY(capture_hook(HC_ACTION,WM_KEYDOWN,(LPARAM)&key)==0&&!capture_events.count);
    mouse=(MSLLHOOKSTRUCT){0};
    VERIFY(capture_mouse_hook(HC_ACTION,WM_LBUTTONDOWN,(LPARAM)&mouse)==0&&!capture_events.count);
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEWHEEL,(LPARAM)&mouse)==0&&!capture_events.count);
    cap_active=0;
    /* On: ARM is refused without the Pico on this PC, in the window. */
    reset_client();g.armed=0;g.cfg.loopback_mode=1;g.cfg.loopback_physical=1;
    fixture_pico=HIDWAY_PICO_KEYBOARD;fixture_pico_calls=0;
    set_armed(1);
    VERIFY(!g.armed&&!g.loop_phys&&fixture_pico_calls==1&&output_count==0&&strstr(g.local_pico_notice,"not on this PC"));
    fixture_pico=HIDWAY_PICO_READY;
    set_armed(1);
    VERIFY(g.armed&&g.loop_phys&&output_count==1&&!g.local_pico_notice[0]);
    /* this PC's keys go through the existing physical path; every state
     * actually sent grants one credit per edge */
    on_capture_key('A',0x1e);
    g.events.shown_at-=HIDWAY_KEY_DWELL_MS;
    net_send_state(HIDWAY_MSG_STATE);
    VERIFY(hidway_bitmap_test(g.keys,4)&&hidway_bitmap_test(output[output_count-1].keys,4));
    VERIFY(hidway_echo_live(&cap_echo,4,false,timeGetTime())==1&&cap_echo.pico_down[4]);
    on_capture_key('A',0x1e|KEYF_UP);
    g.events.shown_at-=HIDWAY_KEY_DWELL_MS;
    net_send_state(HIDWAY_MSG_STATE);
    VERIFY(!hidway_bitmap_test(output[output_count-1].keys,4)&&hidway_echo_live(&cap_echo,4,true,timeGetTime())==1);
    /* this PC's buttons and wheel, never Raw Input for them */
    on_capture_physical_mouse(WM_RBUTTONDOWN,0);
    VERIFY(g.buttons==BTN_R);
    on_capture_physical_mouse(WM_RBUTTONUP,0);
    on_capture_physical_mouse(WM_MOUSEWHEEL,MAKELPARAM(0,120));
    on_capture_physical_mouse(WM_MOUSEMOVE,MAKELPARAM(50,50));
    VERIFY(!g.buttons&&g.cum_wheel==1&&!g.cum_x);
    RAWMOUSE raw={0};raw.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;raw.lLastX=40;
    on_raw_mouse((HANDLE)1,&raw,HIDWAY_SOURCE_PHYSICAL);
    VERIFY(!g.buttons&&!g.cum_x);
    /* ARM / PANIC: this PC's PANIC chord is consumed and seen by HIDway
     * itself, exactly as on the Remote PC route */
    g.cfg.panic.vk=VK_END;g.cfg.panic.mods=MOD_CONTROL|MOD_ALT|MOD_SHIFT;
    g.cfg.toggle.vk=VK_F12;g.cfg.toggle.mods=MOD_CONTROL|MOD_SHIFT;
    on_capture_key(VK_LCONTROL,0x1d);on_capture_key(VK_LMENU,0x38);on_capture_key(VK_LSHIFT,0x2a);
    VERIFY(g.armed&&g.modifiers==7);
    on_capture_key(VK_END,0x4f|KEYF_E0);
    VERIFY(!g.armed&&!g.loop_phys&&output[output_count-1].type==HIDWAY_MSG_RELEASE&&!g.modifiers);
    VERIFY(!hidway_echo_live(&cap_echo,4,true,timeGetTime())&&!cap_echo.pico_down[0xE0]); /* cleared */
    set_armed(1);VERIFY(g.armed&&g.loop_phys);
    on_capture_key(VK_LCONTROL,0x1d);on_capture_key(VK_LSHIFT,0x2a);on_capture_key(VK_F12,0x58);
    VERIFY(!g.armed&&!g.loop_phys);
    set_armed(1);wnd_proc(NULL,WM_HOTKEY,HK_PANIC,0);VERIFY(!g.armed&&!g.loop_phys);
    set_armed(1);wnd_proc(NULL,WM_HOTKEY,HK_TOGGLE,0);VERIFY(!g.armed&&!g.loop_phys);
    /* Guard: edges the Pico never answers disarm, with the reason in the window. */
    set_armed(1);g.armed_at_ms=timeGetTime();g.local_pico_notice[0]=0;
    {
        uint8_t three[]={4,5,6};hidway_echo_frame_t f;memset(&f,0,sizeof f);
        for(int i=0;i<3;i++)hidway_bitmap_set(f.keys,three[i]);
        hidway_echo_frame_t empty;memset(&empty,0,sizeof empty);
        hidway_echo_grant(&cap_echo,&f,timeGetTime()-1000);hidway_echo_grant(&cap_echo,&empty,timeGetTime()-990);
    }
    net_tick();
    VERIFY(!g.armed&&!g.loop_phys&&strstr(g.local_pico_notice,"did not return"));
    /* Guard: an implausible rate of one physical key (an input loop). */
    set_armed(1);g.armed_at_ms=timeGetTime();
    cap_echo.loop_slot=5;
    net_tick();
    VERIFY(!g.armed&&strstr(g.local_pico_notice,"loop")&&!cap_echo.loop_slot);
    /* Hooks, mode on: hardware keys and buttons are classified by the credits;
     * pointer movement and the horizontal wheel stay native. */
    reset_client();hidway_echo_reset(&cap_echo,echo_cas);InterlockedExchange(&capture_generation,0);
    cap_active=cap_keyboard=cap_mouse=1;cap_sources=HIDWAY_SOURCE_VIRTUAL;cap_require_badge=0;cap_consume_virtual=1;
    cap_loop_phys=1;cap_badge=NULL;memset(cap_ours,0,sizeof cap_ours);memset(cap_key_vk,0,sizeof cap_key_vk);
    capture_queue_clear();
    key=(KBDLLHOOKSTRUCT){0};key.vkCode='A';key.scanCode=0x1e;
    VERIFY(capture_hook(HC_ACTION,WM_KEYDOWN,(LPARAM)&key)==1&&capture_events.count==1&&
           capture_events.events[0].kind==WM_CAPTURE_KEY&&!(capture_events.events[0].l&KEYF_VIRTUAL)&&cap_ours[0][0x1e]);
    {
        hidway_echo_frame_t f;memset(&f,0,sizeof f);hidway_bitmap_set(f.keys,4);f.buttons=BTN_L;f.wheel=1;
        hidway_echo_grant(&cap_echo,&f,timeGetTime());
    }
    VERIFY(capture_hook(HC_ACTION,WM_KEYDOWN,(LPARAM)&key)==0&&capture_events.count==1); /* the echo */
    key.flags=LLKHF_UP;
    VERIFY(capture_hook(HC_ACTION,WM_KEYUP,(LPARAM)&key)==1&&capture_events.count==2&&!cap_ours[0][0x1e]&&
           (capture_events.events[1].l&KEYF_UP));
    key.vkCode=VK_NUMLOCK;key.scanCode=0x45;key.flags=LLKHF_EXTENDED; /* hook codes off the Set-1 table */
    VERIFY(loop_key_slot(VK_NUMLOCK,0x45,1)==0x53&&loop_key_slot(VK_PAUSE,0x45,0)==0x48&&
           loop_key_slot(VK_RSHIFT,0x36,1)==0xE5&&loop_key_slot(VK_LCONTROL,0x1d,0)==0xE0&&loop_key_slot(0,0,0)==0);
    key.vkCode=0xFF;key.scanCode=0;key.flags=0; /* no usage: passes as before */
    VERIFY(capture_hook(HC_ACTION,WM_KEYDOWN,(LPARAM)&key)==0&&capture_events.count==2);
    key.vkCode='C';key.scanCode=0x2e;key.flags=LLKHF_INJECTED; /* virtual input: unchanged */
    VERIFY(capture_hook(HC_ACTION,WM_KEYDOWN,(LPARAM)&key)==1&&capture_events.count==3&&
           (capture_events.events[2].l&KEYF_VIRTUAL));
    mouse=(MSLLHOOKSTRUCT){0};
    VERIFY(capture_mouse_hook(HC_ACTION,WM_LBUTTONDOWN,(LPARAM)&mouse)==0&&capture_events.count==3); /* echo */
    VERIFY(capture_mouse_hook(HC_ACTION,WM_RBUTTONDOWN,(LPARAM)&mouse)==1&&capture_events.count==4&&
           capture_events.events[3].kind==WM_CAPTURE_PMOUSE);
    mouse.mouseData=(DWORD)MAKELONG(0,120);
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEWHEEL,(LPARAM)&mouse)==0&&capture_events.count==4); /* echo */
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEWHEEL,(LPARAM)&mouse)==1&&capture_events.count==5);
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEHWHEEL,(LPARAM)&mouse)==0&&capture_events.count==5);
    mouse.mouseData=0;mouse.pt.x=5;
    VERIFY(capture_mouse_hook(HC_ACTION,WM_MOUSEMOVE,(LPARAM)&mouse)==0&&capture_events.count==5);
    /* the consumed events reach the physical path */
    g.loop_phys=1;g.cfg.loopback_mode=1;g.cfg.loopback_physical=1;
    drain_capture();
    VERIFY(g.buttons==BTN_R&&g.cum_wheel==1&&hidway_bitmap_test(g.keys,6)&&!hidway_bitmap_test(g.keys,4));
    cap_active=0;cap_loop_phys=0;capture_queue_clear();

    /* The visible HUD button uses the same production preset and does not
     * ARM, create a capture hook, send packets, or change the relay address. */
    reset_client();g.preview=0;g.armed=0;g.dpi=96;g.inst=GetModuleHandleA(NULL);
    strcpy(g.cfg.target,"192.0.2.10");g.cfg.port=47800;
    g.cfg.source_touchpad=1;g.cfg.loopback_pointer_pico=1;g.cfg.repeat_enabled=1;
    hidway_config_t before=g.cfg;fixture_saves=0;fixture_peer=0;fixture_pico=HIDWAY_PICO_READY;
    fixture_save_ok=0;
    wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1&&!g.armed&&output_count==0);
    VERIFY(strstr(g.local_pico_notice,"Could not save")!=NULL);
    fixture_save_ok=1;fixture_peer=1;
    wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1&&strstr(g.local_pico_notice,"Pi relay"));
    fixture_peer=0;
    for(unsigned present=0;present<HIDWAY_PICO_READY;present++) {
        fixture_pico=present;
        wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
        VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1&&strstr(g.local_pico_notice,"USB devices"));
    }
    fixture_pico=HIDWAY_PICO_READY;
    /* Editing the address/port is distinct from selecting a saved route. */
    g.edit[0]=CreateWindowExA(0,"EDIT","192.0.2.20",0,0,0,120,24,NULL,NULL,g.inst,NULL);
    g.edit[1]=CreateWindowExA(0,"EDIT","47800",0,0,0,120,24,NULL,NULL,g.inst,NULL);
    VERIFY(g.edit[0]&&g.edit[1]);
    wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1&&strstr(g.local_pico_notice,"Connection tab first"));
    SetWindowTextA(g.edit[0],g.cfg.target);SetWindowTextA(g.edit[1],"47801");
    wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1);
    DestroyWindow(g.edit[0]);DestroyWindow(g.edit[1]);g.edit[0]=g.edit[1]=NULL;
    /* Even a queued click cannot stop an active input session or replay. */
    g.armed=1;
    wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(g.armed&&!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1&&output_count==0);
    g.armed=0;replay.active=1;
    wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(replay.active&&!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1);
    replay.active=0;
    wnd_proc(NULL,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(fixture_saves==2&&!g.armed&&output_count==0&&!cap_active);
    VERIFY(g.cfg.loopback_mode&&g.cfg.source_virtual&&!g.cfg.loopback_pointer_pico&&!g.cfg.repeat_enabled);
    /* the Remote PC route keeps its own choices (physical/touchpad, pointer, repeat) */
    VERIFY(input_source_mask()==HIDWAY_SOURCE_VIRTUAL&&!g.cfg.source_physical&&!g.cfg.source_touchpad&&
           profile_matches(&g.cfg.profiles[HIDWAY_PROFILE_REMOTE],&before));
    VERIFY(g.cfg.relay_keyboard&&g.cfg.relay_mouse&&!strcmp(g.cfg.target,before.target)&&g.cfg.port==before.port);
    VERIFY(!memcmp(&fixture_saved,&g.cfg,sizeof g.cfg)&&strstr(g.local_pico_notice,"Press ARM"));
    /* Visible button follows ARM state; the window stays hidden throughout. */
    g.local_pico_button=CreateWindowExA(0,"BUTTON","Pico on this PC",0,0,0,142,46,NULL,NULL,g.inst,NULL);
    VERIFY(g.local_pico_button!=NULL);
    g.preview=1;set_armed(1);VERIFY(!IsWindowEnabled(g.local_pico_button));
    set_armed(0);VERIFY(IsWindowEnabled(g.local_pico_button));
    DestroyWindow(g.local_pico_button);g.local_pico_button=NULL;
    VERIFY(hidway_net_peer_is_loopback()==-1);
    VERIFY(hidway_net_open("127.0.0.2",47801)==0&&hidway_net_peer_is_loopback()==1);
    hidway_net_close();
    VERIFY(hidway_net_open("::1",47801)==0&&hidway_net_peer_is_loopback()==1);
    hidway_net_close();
    /* Some Windows configurations use IPv6-only sockets and reject mapped
     * peers before connection; if connected, the guard must still reject it. */
    if(hidway_net_open("::ffff:127.0.0.1",47801)==0)
        VERIFY(hidway_net_peer_is_loopback()==1);
    hidway_net_close();
    VERIFY(hidway_net_open("192.0.2.10",47801)==0&&hidway_net_peer_is_loopback()==0);
    hidway_net_close();VERIFY(hidway_net_peer_is_loopback()==-1);
    reset_client();uint32_t old_session=g.session_id;
    set_armed(0);set_armed(1);
    VERIFY(g.armed&&g.session_id!=old_session&&g.seq==1&&output[3].session_id==g.session_id);
    set_armed(0);
    /* Replay ownership is per device and raw scan identity, independent of
     * live capture policy, aliases and hold-repeat settings. */
    static hidway_replay_state state;hw_rec_screen screen={1920,1080,3840,1080};
    hw_rec_event event={0,0,HW_REC_KEY_DOWN,30,0,0,0,'A'};
    VERIFY(hidway_replay_apply(&state,&screen,&event)&&hidway_bitmap_test(state.frame.keyboard.keys,4));
    event.device=1;VERIFY(hidway_replay_apply(&state,&screen,&event));
    event.device=0;event.type=HW_REC_KEY_UP;VERIFY(hidway_replay_apply(&state,&screen,&event)&&hidway_bitmap_test(state.frame.keyboard.keys,4));
    event.device=1;VERIFY(hidway_replay_apply(&state,&screen,&event)&&!hidway_bitmap_test(state.frame.keyboard.keys,4));
    event.type=HW_REC_KEY_DOWN;VERIFY(hidway_replay_apply(&state,&screen,&event));event.device=0;VERIFY(hidway_replay_apply(&state,&screen,&event));
    event=(hw_rec_event){0,0,HW_REC_RELEASE_DEVICE,0,0,0,0,0};VERIFY(hidway_replay_apply(&state,&screen,&event)&&hidway_bitmap_test(state.frame.keyboard.keys,4));
    event.device=1;VERIFY(hidway_replay_apply(&state,&screen,&event)&&!hidway_bitmap_test(state.frame.keyboard.keys,4));
    event=(hw_rec_event){0,2,HW_REC_WHEEL,0,0,60,0,0};VERIFY(hidway_replay_apply(&state,&screen,&event)&&state.frame.wheel==0);
    VERIFY(hidway_replay_apply(&state,&screen,&event)&&state.frame.wheel==1&&state.frame.buttons==0);
    event=(hw_rec_event){0,2,HW_REC_BUTTON_DOWN,2,0,0,0,0};VERIFY(hidway_replay_apply(&state,&screen,&event)&&state.frame.buttons==4);
    event.device=3;VERIFY(hidway_replay_apply(&state,&screen,&event));event.device=2;event.type=HW_REC_BUTTON_UP;
    VERIFY(hidway_replay_apply(&state,&screen,&event)&&state.frame.buttons==4);
    event.device=3;VERIFY(hidway_replay_apply(&state,&screen,&event)&&state.frame.buttons==0);
    event=(hw_rec_event){0,4,HW_REC_ABSOLUTE,0,0,100,200,0};VERIFY(hidway_replay_apply(&state,&screen,&event)&&state.frame.x==0);
    event.x=32768;VERIFY(hidway_replay_apply(&state,&screen,&event)&&state.frame.x>900&&state.frame.x<1000);
    uint16_t binding;event=(hw_rec_event){0,0,HW_REC_KEY_DOWN,0,0,0,0,VK_PACKET};VERIFY(!hidway_replay_binding(&event,&binding));
    event.code=0x03b1;VERIFY(!hidway_replay_apply(&state,&screen,&event));
    reset_client();g.cfg.repeat_enabled=1;
    replay.active=1;replay.file.events=calloc(1,sizeof(hw_rec_event));replay.file.count=1;replay.file.duration=1000;replay.file.screen=screen;
    replay.file.events[0]=(hw_rec_event){0,0,HW_REC_BUTTON_DOWN,0,0,0,0,0};replay.started=timeGetTime();replay.last_tick=replay.started;
    replay_tick();VERIFY(replay.next==1&&replay.state.frame.buttons==1);
    on_capture_mouse(WM_RBUTTONDOWN,0);on_capture_key('B',0x30);VERIFY(!g.held.buttons&&!g.modifiers&&!hidway_bitmap_test(g.keys,5));
    net_send_state(HIDWAY_MSG_STATE);VERIFY(output_count&&output[0].buttons==1);
    wnd_proc(NULL,WM_HOTKEY,HK_PANIC,0);VERIFY(!g.armed&&!replay.active&&!replay.file.events&&output[output_count-1].type==HIDWAY_MSG_RELEASE);
    reset_client();COPYDATASTRUCT invalid={RECORDING_IPC,3,(void *)L"x"};VERIFY(!wnd_proc(NULL,WM_COPYDATA,0,(LPARAM)&invalid));
    wchar_t loadpath[]=L"C:\\test.hidrec";COPYDATASTRUCT data={RECORDING_IPC,sizeof loadpath,loadpath};VERIFY(!wnd_proc(NULL,WM_COPYDATA,0,(LPARAM)&data));
    g.armed=0;VERIFY(wnd_proc(NULL,WM_COPYDATA,0,(LPARAM)&data)&&!wcscmp(replay.path,loadpath)&&!g.armed);

    /* ---- Tabbed main window: hidden fixture with the production controls,
     * no hooks, no ini writes (fixture save), no foreground activation. ---- */
    char text[64];
    reset_client();g.armed=0;g.dpi=96;g.inst=GetModuleHandleA(NULL);
    WNDCLASSA hud={0};hud.lpfnWndProc=fixture_proc;hud.hInstance=g.inst;hud.lpszClassName="HIDwayHudFixture";
    VERIFY(RegisterClassA(&hud)!=0);
    g.hwnd=CreateWindowExA(0,hud.lpszClassName,"HUD fixture",WIN_STYLE,0,0,200,200,NULL,NULL,g.inst,NULL);
    VERIFY(g.hwnd!=NULL&&!IsWindowVisible(g.hwnd));
    /* GDI+ is what draws the glyphs and rounded shapes: start it for the pixel checks */
    ULONG_PTR gdip_token=0;HW_GdiplusStartupInput gdip_input={1,NULL,FALSE,FALSE};
    VERIFY(GdiplusStartup(&gdip_token,&gdip_input,NULL)==0);
    create_fonts();compute_layout();create_controls(g.hwnd);
    ui_state();
    /* Existing command numbers are unchanged (tests and tools post them). */
    VERIFY(ID_HISTORY==1001&&ID_TARGET==1003&&ID_CB_KBD==1007&&ID_SAVE==1010&&ID_ARM==1011&&ID_PANIC==1012&&
           ID_OPTIONS==1016&&ID_MONITOR==1030&&ID_LOCAL_PICO==1032&&ID_UPDATES==1033&&ID_COMPACT==1034&&
           ID_TABS==1035&&ID_INPUT_APPLY==1042);
    VERIFY(GetDlgCtrlID(g.arm)==ID_ARM&&GetDlgCtrlID(g.panic)==ID_PANIC&&GetDlgCtrlID(g.local_pico_button)==ID_LOCAL_PICO&&
           GetDlgCtrlID(g.save)==ID_SAVE&&GetDlgCtrlID(g.edit[0])==ID_TARGET&&GetDlgCtrlID(g.cb[2])==ID_CB_HIST&&
           GetDlgCtrlID(g.repeat_cps)==ID_REPEAT_CPS&&GetDlgCtrlID(g.click_cps)==ID_CLICK_CPS&&GetDlgCtrlID(g.history)==ID_HISTORY);
    page_control page[]={
        {g.edit[0],TAB_CONNECTION},{g.edit[1],TAB_CONNECTION},{g.edit[2],TAB_CONNECTION},{g.edit[3],TAB_CONNECTION},
        {g.save,TAB_CONNECTION},
        {g.profile_button[0],TAB_INPUT},{g.profile_button[1],TAB_INPUT},{g.cb[0],TAB_INPUT},{g.cb[1],TAB_INPUT},
        {g.src[0],TAB_INPUT},{g.src[1],TAB_INPUT},{g.src[2],TAB_INPUT},{g.pointer_route,TAB_INPUT},{g.repeat,TAB_INPUT},
        {g.repeat_button,TAB_INPUT},{g.repeat_cps,TAB_INPUT},{g.repeat_source,TAB_INPUT},{g.click_cps,TAB_INPUT},
        {g.input_apply,TAB_INPUT},
        {g.monitor_button,TAB_TOOLS},{g.replay_button,TAB_TOOLS},{g.updates_button,TAB_TOOLS},{g.cb[2],TAB_TOOLS},
        {g.clear,TAB_TOOLS},{g.history,TAB_TOOLS},
    };
    HWND always[]={g.arm,g.local_pico_button,g.panic,g.tabs};
    const size_t n_page=sizeof page/sizeof page[0],n_always=sizeof always/sizeof always[0];
    VERIFY(g.tab==TAB_CONTROL&&visibility_errors(page,n_page,always,n_always)==0);

    /* Tab switching hides and restores controls with their values, never
     * disarms, never saves; the action row keeps its enable rules (ARMED). */
    SetWindowTextA(g.edit[0],"192.0.2.99");SetWindowTextA(g.edit[2],"500");
    SendMessageA(g.history,LB_ADDSTRING,0,(LPARAM)"12:00:00.000\x1fW\x1f" "d");
    set_armed(1);VERIFY(g.armed); /* preview: no hooks or sender thread */
    hidway_config_t settings=g.cfg;unsigned sent=output_count;
    LRESULT history_rows=SendMessageA(g.history,LB_GETCOUNT,0,0);
    for(int pass=0;pass<2;pass++)for(int t=0;t<TAB_COUNT;t++) {
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_TAB_CONTROL+t,BN_CLICKED),0);
        VERIFY(g.tab==t&&g.armed&&visibility_errors(page,n_page,always,n_always)==0);
        VERIFY(!IsWindowEnabled(g.local_pico_button)&&IsWindowEnabled(g.arm)&&IsWindowEnabled(g.panic));
    }
    VERIFY(!fixture_saves&&output_count==sent&&!memcmp(&settings,&g.cfg,sizeof settings));
    GetWindowTextA(g.edit[0],text,sizeof text);VERIFY(!strcmp(text,"192.0.2.99"));
    GetWindowTextA(g.edit[2],text,sizeof text);VERIFY(!strcmp(text,"500"));
    VERIFY(history_rows>=2&&SendMessageA(g.history,LB_GETCOUNT,0,0)==history_rows); /* W + "Armed" */
    /* Keyboard: the strip wants the arrows; Left/Right wrap, Home/End jump. */
    select_tab(TAB_CONTROL);
    VERIFY((SendMessageA(g.tabs,WM_GETDLGCODE,0,0)&DLGC_WANTARROWS)!=0&&(GetWindowLongPtrA(g.tabs,GWL_STYLE)&WS_TABSTOP));
    SendMessageA(g.tabs,WM_KEYDOWN,VK_RIGHT,0);VERIFY(g.tab==TAB_CONNECTION);
    SendMessageA(g.tabs,WM_KEYDOWN,VK_LEFT,0);SendMessageA(g.tabs,WM_KEYDOWN,VK_LEFT,0);VERIFY(g.tab==TAB_TOOLS);
    SendMessageA(g.tabs,WM_KEYDOWN,VK_HOME,0);VERIFY(g.tab==TAB_CONTROL);
    SendMessageA(g.tabs,WM_KEYDOWN,VK_END,0);VERIFY(g.tab==TAB_TOOLS&&g.armed&&visibility_errors(page,n_page,always,n_always)==0);
    /* PANIC works from any tab and the action row follows ARM state. */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PANIC,BN_CLICKED),(LPARAM)g.panic);
    VERIFY(!g.armed&&g.tab==TAB_TOOLS&&IsWindowEnabled(g.local_pico_button)&&IsWindowEnabled(g.arm));
    GetWindowTextA(g.edit[0],text,sizeof text);VERIFY(!strcmp(text,"192.0.2.99"));
    /* The posted number of the retired Input options button opens the Input tab. */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_OPTIONS,BN_CLICKED),0);VERIFY(g.tab==TAB_INPUT&&!g.armed);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_COMPACT,BN_CLICKED),0);VERIFY(g.tab==TAB_INPUT&&!fixture_saves);
    /* ---- The Rail window: 700 x 432, a rail on the left (state, facts, ARM,
     * Pico on this PC, PANIC) and the tab pane on the right. It fits a
     * 1366x768 screen at 150 % (910x512 logical) and 1920x1080 at 150 %. ---- */
    VERIFY(LAYOUT_W==700&&LAYOUT_H==432&&g.L.W==S(LAYOUT_W)&&g.L.H==S(LAYOUT_H));
    RECT frame96={0,0,S(LAYOUT_W),S(LAYOUT_H)};AdjustWindowRectExForDpi(&frame96,WIN_STYLE,FALSE,0,96);
    VERIFY(frame96.bottom-frame96.top<=728&&frame96.right-frame96.left<=1366);
    g.dpi=144;create_fonts();compute_layout();apply_layout();
    RECT frame144={0,0,g.L.W,g.L.H};AdjustWindowRectExForDpi(&frame144,WIN_STYLE,FALSE,0,144);
    VERIFY(g.L.W==1050&&g.L.H==648&&frame144.bottom-frame144.top<=720&&frame144.right-frame144.left<=1366);
    /* Every combination of DPI, tab, route, banner and message length: all
     * inside their areas, nothing overlapping, every text inside its box. */
    for(UINT dpi=96;dpi<=192;dpi+=48) {
        for(int armed=0;armed<2;armed++)for(int tab=0;tab<TAB_COUNT;tab++)for(int gaming=0;gaming<2;gaming++)
        for(int banner=0;banner<2;banner++)for(int two=0;two<2;two++) {
            layout_case(dpi,banner,two,tab,gaming,armed);
            unsigned errors=layout_errors();
            if(errors)printf("layout: dpi %u armed %d tab %d gaming %d banner %d two-line %d: %u\n",dpi,armed,tab,gaming,banner,two,errors);
            VERIFY(errors==0);
            VERIFY((!two||g.msg_lines==2)&&banner_visible()==(banner&&!armed));
            if(banner&&!armed) {
                /* the body starts 38 px lower while the banner shows */
                RECT with=g.L.body;g.update_offer=2;compute_layout();
                VERIFY(abs((with.top-g.L.body.top)-S(38))<=1);
                g.update_offer=1;compute_layout();
            }
        }
        layout_case(dpi,0,0,TAB_CONTROL,0,0);
        HDC dc=GetDC(g.hwnd);
        unsigned errors=text_errors(dc)+message_fit_errors(dc);
        if(errors)printf("text: dpi %u: %u\n",dpi,errors);
        VERIFY(errors==0);
        /* the strip's two-line size is exactly what a two-line message needs */
        VERIFY(text_lines(dc,g.f_fact,two_line_notice,msg_text_w())==2&&text_lines(dc,g.f_fact,"Ready. Nothing is sent until you press ARM.",msg_text_w())==1);
        ReleaseDC(g.hwnd,dc);
    }
    g.armed=0;g.update_offer=0;g.dpi=96;create_fonts();ui_state();banner_sync();compute_layout();apply_layout();msg_sync();
    VERIFY(g.L.H==S(432)&&!g.local_pico_notice[0]);

    /* Rail buttons and tab strip are in place and never hidden; hidden page
     * controls are not focusable and Tab follows the visual order: rail first
     * (ARM, Pico on this PC, PANIC), then the tab strip, the banner, the body. */
    for(int t=0;t<TAB_COUNT;t++) {
        g.update_offer=1;strcpy(g.update_tag,"v0.6.1");select_tab(t);banner_sync();
        HWND order[48];int n=0;
        HWND item=GetNextDlgTabItem(g.hwnd,NULL,FALSE);
        for(HWND first=item;item&&n<48;) {
            order[n++]=item;
            item=GetNextDlgTabItem(g.hwnd,item,FALSE);
            if(item==first)break;
        }
        VERIFY(n>=6&&order[0]==g.arm&&order[1]==g.local_pico_button&&order[2]==g.panic&&order[3]==g.tabs&&
               order[4]==g.update_now&&order[5]==g.update_later);
        page_control pc[25];size_t np=page_controls(pc);
        for(int i=6;i<n;i++) {
            int found=0;
            for(size_t k=0;k<np;k++)if(pc[k].h==order[i]){found=1;VERIFY(pc[k].tab==t&&shown(order[i]));}
            VERIFY(found);
        }
        if(t==TAB_INPUT)VERIFY(order[6]==g.profile_button[0]&&order[n-1]==g.input_apply);
        if(t==TAB_CONNECTION)VERIFY(order[6]==g.edit[0]&&order[n-1]==g.save);
        if(t==TAB_TOOLS)VERIFY(order[6]==g.monitor_button);
        if(t==TAB_CONTROL)VERIFY(n==6); /* the Control tab has no controls of its own */
        VERIFY(visibility_errors(page,n_page,always,n_always)==0);
    }
    g.update_offer=0;banner_sync();select_tab(TAB_CONTROL);

    /* ---- The one message strip: every message goes through it with its
     * severity; each stays until its writer clears it. ---- */
    ui_state();
    char strip[256];int sev;
    msg_current(strip,sizeof strip,&sev);
    VERIFY(!strcmp(strip,"Ready. Nothing is sent until you press ARM.")&&sev==SEV_INFO);
    strcpy(g.cfg.toggle.text,"ctrl+shift+f12");
    g.armed=1;msg_current(strip,sizeof strip,&sev);
    VERIFY(strstr(strip,"Armed. Toggle with Ctrl+Shift+F12")&&sev==SEV_OK);
    g.armed=0;
    /* the hotkey warning: on every tab, and on Tools above everything else */
    g.toggle_hk_ok=0;
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"Toggle hotkey Ctrl+Shift+F12 is in use")&&sev==SEV_WARN);
    connection_status("Settings not saved: port must be 1 to 65535.",1);
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"port must be")&&sev==SEV_ERR); /* latest message first */
    select_tab(TAB_TOOLS);msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"is in use")&&sev==SEV_WARN);
    select_tab(TAB_CONTROL);g.toggle_hk_ok=1;
    /* Connection status line: error severity, stays until Save works or the fields reload */
    select_tab(TAB_CONNECTION);
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"port must be")&&sev==SEV_ERR);
    msg_sync();
    connection_fields_load();
    msg_current(strip,sizeof strip,&sev);VERIFY(!strcmp(strip,"Ready. Nothing is sent until you press ARM.")&&sev==SEV_INFO);
    /* Pico notices: warnings, errors and the success line, each with its own severity */
    select_tab(TAB_CONTROL);
    pico_notice("DISARM before selecting Pico on this PC.",SEV_WARN);
    msg_current(strip,sizeof strip,&sev);VERIFY(!strcmp(strip,"DISARM before selecting Pico on this PC.")&&sev==SEV_WARN);
    pico_notice("Could not save hidway.ini. Settings have not changed.",SEV_ERR);
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"Could not save")&&sev==SEV_ERR);
    pico_notice("Pico on this PC: virtual keys.\r\nPointer stays native. Press ARM when ready.",SEV_OK);
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"Press ARM when ready")&&sev==SEV_OK);
    msg_sync();VERIFY(g.msg_lines==2&&g.L.msg.bottom-g.L.msg.top>=S(45)&&g.L.msg.top==S(374)&&g.L.body.bottom==S(364));
    select_tab(TAB_TOOLS);msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"Press ARM when ready")); /* still there on another tab */
    select_tab(TAB_CONTROL);
    /* the Pico notice stays as long as before: until ARM or a save clears it */
    g.local_pico_notice[0]=0;msg_sync();
    msg_current(strip,sizeof strip,&sev);VERIFY(sev==SEV_INFO&&g.msg_lines==1&&g.L.msg.top==S(386)&&g.L.body.bottom==S(376));
    /* Input tab: its status line (error / saved), then its hint, which turns into the unapplied warning */
    select_tab(TAB_INPUT);
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"Apply and save disarms first; changes not applied are dropped")&&sev==SEV_INFO);
    g.cfg.loopback_mode=1;input_tab_load();
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"Gaming PC: virtual input only")&&sev==SEV_INFO);
    g.pending.loopback_physical=1;msg_current(strip,sizeof strip,&sev);
    VERIFY(strstr(strip,"Programs that read Raw Input")&&strstr(strip,"The pointer stays native")&&sev==SEV_INFO);
    g.input_dirty=1;msg_current(strip,sizeof strip,&sev);
    VERIFY(strstr(strip,"Changes not applied yet")&&strstr(strip,"Raw Input")&&sev==SEV_WARN);
    g.pending.loopback_physical=0;msg_current(strip,sizeof strip,&sev);
    VERIFY(!strcmp(strip,"Changes not applied yet. Apply and save disarms first, then saves.")&&sev==SEV_WARN);
    /* the Gaming PC hint needs three lines: it ends in an ellipsis in the strip and
     * its whole text is the strip's tooltip; messages that fit have no tooltip */
    {
        char tip[256];
        g.cfg.loopback_mode=1;input_tab_load();
        VERIFY(msg_tip_text(tip,sizeof tip)&&strstr(tip,"Gaming PC: virtual input only")&&strstr(tip,"dropped when you leave this tab."));
        g.cfg.loopback_mode=0;input_tab_load();
        VERIFY(!msg_tip_text(tip,sizeof tip)&&!tip[0]);
        select_tab(TAB_CONTROL);
        VERIFY(!msg_tip_text(tip,sizeof tip));
        select_tab(TAB_INPUT);
    }
    g.cfg.loopback_mode=0;input_tab_load();
    input_status("Settings not saved: clicks/sec must be 1 to 25.",1);
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"clicks/sec")&&sev==SEV_ERR);
    input_status("Saved. Press ARM when ready.",0);
    msg_current(strip,sizeof strip,&sev);VERIFY(!strcmp(strip,"Saved. Press ARM when ready.")&&sev==SEV_OK);
    select_tab(TAB_CONTROL);select_tab(TAB_INPUT); /* entering the tab clears it, as before */
    msg_current(strip,sizeof strip,&sev);VERIFY(strstr(strip,"changes not applied are dropped"));
    select_tab(TAB_CONTROL);

    /* The strip paints each severity with its own glyph, bar and fill. */
    {
        static const struct { int sev; COLORREF bar, bg; } looks[]={
            {SEV_INFO,W_LINE2,W_SURF},{SEV_OK,W_GREEN,W_SURF},{SEV_WARN,W_AMBER,W_MSG_WARN_BG},{SEV_ERR,W_RED,W_MSG_ERR_BG}};
        for(UINT dpi=96;dpi<=192;dpi+=48)for(int i=0;i<4;i++) {
            g.dpi=dpi;create_fonts();compute_layout();apply_layout();
            if(looks[i].sev==SEV_INFO)g.local_pico_notice[0]=0;else pico_notice("A message.",looks[i].sev);
            select_tab(TAB_CONTROL);msg_sync();
            HDC mdc;HBITMAP bm=paint_bitmap(&mdc);
            int cy=(g.L.msg.top+g.L.msg.bottom)/2;
            VERIFY(GetPixel(mdc,g.L.msg.left+S(1),cy)==looks[i].bar);
            VERIFY(GetPixel(mdc,g.L.msg.right-S(4),cy)==looks[i].bg);
            /* the glyph is in the strip: some pixel of it differs from the fill */
            int glyph_px=0;
            for(int x=g.L.msg.left+S(11);x<g.L.msg.left+S(30);x++)for(int y=cy-S(7);y<cy+S(7);y++)
                if(GetPixel(mdc,x,y)!=looks[i].bg)glyph_px++;
            VERIFY(glyph_px>=S(8));
            free_bitmap(mdc,bm);
            if(dpi==96){char name[32];snprintf(name,sizeof name,"strip-%d-control-96",looks[i].sev);test_shot(name);}
        }
        g.local_pico_notice[0]=0;g.dpi=96;create_fonts();compute_layout();apply_layout();msg_sync();
    }

    /* The recent-input list gets exactly what it got before. */
    ui_state();g.preview=0;select_tab(TAB_CONNECTION);
    SendMessageA(g.history,LB_RESETCONTENT,0,0);
    SetWindowTextA(g.edit[2],"500");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save);
    {
        char row[160];
        VERIFY(SendMessageA(g.history,LB_GETCOUNT,0,0)==1&&SendMessageA(g.history,LB_GETTEXT,0,(LPARAM)row)>0&&strstr(row,"\x1f" "Settings saved\x1f" "i"));
        SetWindowTextA(g.edit[2],"5");
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save); /* refused: no history line */
        VERIFY(SendMessageA(g.history,LB_GETCOUNT,0,0)==1&&g.conn_error);
        test_shot("err-connection-96");
        select_tab(TAB_INPUT);
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_CB_MOUSE,BN_CLICKED),(LPARAM)g.cb[1]);
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
        VERIFY(SendMessageA(g.history,LB_GETCOUNT,0,0)==2&&SendMessageA(g.history,LB_GETTEXT,0,(LPARAM)row)>0&&
               strstr(row,"\x1f" "Input settings saved - arm when ready\x1f" "i"));
    }
    g.preview=1;g.conn_status[0]=0;g.input_status[0]=0;

    /* State block: DISARMED (1 px border) and ARMED (2 px green border, green fill),
     * the timer drawn in it; and the Input tab's amber dot while edits are not applied. */
    for(UINT dpi=96;dpi<=192;dpi+=48) {
        g.dpi=dpi;create_fonts();compute_layout();apply_layout();ui_state();
        RECT s=g.L.state;int mid=(s.top+s.bottom)/2;
        HDC mdc;HBITMAP bm=paint_bitmap(&mdc);
        VERIFY(GetPixel(mdc,s.left,mid)==W_LINE2&&GetPixel(mdc,s.left+S(60),s.top+S(4))==W_SURF);
        free_bitmap(mdc,bm);
        g.armed=1;g.armed_at_ms=timeGetTime()-83000;bm=paint_bitmap(&mdc);
        VERIFY(GetPixel(mdc,s.left,mid)==W_GREEN&&GetPixel(mdc,s.left+S(1),mid)==W_GREEN&&
               GetPixel(mdc,s.left+S(60),s.top+S(4))==W_GREEN_F);
        free_bitmap(mdc,bm);
        g.armed=0;
        select_tab(TAB_INPUT);
        RECT rc[TAB_COUNT];POINT dp;HDC tdc=GetDC(g.tabs);
        g.input_dirty=1;tabs_layout(tdc,rc,&dp);ReleaseDC(g.tabs,tdc);
        for(int dirty=1;dirty>=0;dirty--) {
            g.input_dirty=dirty;
            HDC sdc=GetDC(NULL),mdc2=CreateCompatibleDC(sdc);
            RECT cr;GetClientRect(g.tabs,&cr);
            HBITMAP tb=CreateCompatibleBitmap(sdc,cr.right,cr.bottom);HGDIOBJ old=SelectObject(mdc2,tb);
            SendMessageA(g.tabs,WM_PRINTCLIENT,(WPARAM)mdc2,PRF_CLIENT);
            VERIFY((GetPixel(mdc2,dp.x,dp.y)==W_AMBER)==(dirty!=0));
            if(dirty) { /* it widens the Input tab by its own width */
                HDC mdc3=GetDC(g.tabs);RECT r1[TAB_COUNT],r0[TAB_COUNT];POINT d2;
                tabs_layout(mdc3,r1,&d2);g.input_dirty=0;tabs_layout(mdc3,r0,&d2);g.input_dirty=1;ReleaseDC(g.tabs,mdc3);
                VERIFY(r1[TAB_INPUT].right-r0[TAB_INPUT].right==S(12)&&r1[TAB_INPUT].left==r0[TAB_INPUT].left);
            }
            SelectObject(mdc2,old);DeleteObject(tb);DeleteDC(mdc2);ReleaseDC(NULL,sdc);
        }
        g.input_dirty=0;
        /* a dirty flag from a click shows on the strip within a timer tick */
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_CB_MOUSE,BN_CLICKED),(LPARAM)g.cb[1]);
        VERIFY(g.input_dirty);
        select_tab(TAB_CONTROL);
    }
    g.dpi=96;create_fonts();ui_state();compute_layout();apply_layout();
    test_shot("control-96");
    g.armed=1;g.armed_at_ms=timeGetTime()-83000;g.status_seen=1;test_shot("control-armed-96");g.armed=0;g.status_seen=0;
    /* the old geometry tests' WAKE: the tile that offers it is wide enough */
    VERIFY(g.L.tiles[3].right-g.L.tiles[3].left>=S(140)&&g.L.tiles[3].right<=g.L.body.right);

    /* ---- Input tab: greying, unapplied edits, validation, apply ---- */
    ui_state();g.cfg.repeat_enabled=0;g.cfg.repeat_button=2;g.cfg.repeat_cps=12;g.cfg.repeat_sources=4;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_TAB_INPUT,BN_CLICKED),0);
    VERIFY(!IsWindowEnabled(g.repeat_button)&&!IsWindowEnabled(g.repeat_source)&&!IsWindowEnabled(g.repeat_cps));
    VERIFY(IsWindowEnabled(g.click_cps)&&!IsWindowEnabled(g.pointer_route)); /* Remote PC: Pico motion only */
    VERIFY(IsWindowEnabled(g.src[0])&&IsWindowEnabled(g.src[1])&&IsWindowEnabled(g.src[2])&&!g.input_dirty);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    VERIFY(g.pending.repeat_enabled&&!g.cfg.repeat_enabled&&g.input_dirty);
    VERIFY(IsWindowEnabled(g.repeat_button)&&IsWindowEnabled(g.repeat_source)&&IsWindowEnabled(g.repeat_cps));
    VERIFY(SendMessageA(g.repeat_button,CB_GETCURSEL,0,0)==2&&SendMessageA(g.repeat_source,CB_GETCURSEL,0,0)==2);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    VERIFY(!g.pending.repeat_enabled&&!IsWindowEnabled(g.repeat_button)&&!IsWindowEnabled(g.repeat_source)&&!IsWindowEnabled(g.repeat_cps));
    GetWindowTextA(g.repeat_cps,text,sizeof text);
    VERIFY(!strcmp(text,"12")&&SendMessageA(g.repeat_button,CB_GETCURSEL,0,0)==2&&SendMessageA(g.repeat_source,CB_GETCURSEL,0,0)==2);
    /* Viewing and editing never disarm; leaving the tab drops what was not
     * applied (reset from the saved values when the tab is entered again). */
    set_armed(1);sent=output_count;before=g.cfg;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_CB_MOUSE,BN_CLICKED),(LPARAM)g.cb[1]);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_TOUCHPAD,BN_CLICKED),(LPARAM)g.src[1]);
    SetWindowTextA(g.click_cps,"3");
    VERIFY(g.armed&&output_count==sent&&!g.pending.relay_mouse&&g.pending.source_touchpad&&cb_checked(ID_SRC_TOUCHPAD)&&
           g.input_dirty&&!memcmp(&before,&g.cfg,sizeof before));
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_TAB_TOOLS,BN_CLICKED),0);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_TAB_INPUT,BN_CLICKED),0);
    GetWindowTextA(g.click_cps,text,sizeof text);
    VERIFY(g.armed&&!fixture_saves&&!memcmp(&before,&g.cfg,sizeof before)&&!g.pending.repeat_enabled&&
           g.pending.relay_mouse&&!g.pending.source_touchpad&&!g.input_dirty&&!strcmp(text,"10"));
    /* Validation (1..25 per speed): nothing applied, nothing disarmed. */
    SetWindowTextA(g.repeat_cps,"0");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(g.armed&&g.input_error&&strstr(g.input_status,"1 to 25")&&!memcmp(&before,&g.cfg,sizeof before)&&!fixture_saves);
    SetWindowTextA(g.repeat_cps,"15");SetWindowTextA(g.click_cps,"26");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(g.armed&&g.input_error&&!memcmp(&before,&g.cfg,sizeof before));
    /* Apply and save: disarms first (three RELEASE), then uses the values. */
    SetWindowTextA(g.click_cps,"7");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_TOUCHPAD,BN_CLICKED),(LPARAM)g.src[1]);
    SendMessageA(g.repeat_button,CB_SETCURSEL,1,0);
    /* Hold to repeat cannot name a source that is not ticked (Virtual is not):
     * refused like a bad speed, nothing applied, nothing disarmed. */
    output_count=0;
    static const int unticked_sources[]={2,4,5}; /* Virtual, Physical + virtual, Touchpad + virtual */
    for(size_t i=0;i<sizeof unticked_sources/sizeof unticked_sources[0];i++) {
        SendMessageA(g.repeat_source,CB_SETCURSEL,(WPARAM)unticked_sources[i],0);
        g.input_error=0;
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
        VERIFY(g.armed&&output_count==0&&g.input_error&&strstr(g.input_status,"not ticked")&&
               !memcmp(&before,&g.cfg,sizeof before)&&!fixture_saves&&g.input_dirty);
    }
    SendMessageA(g.repeat_source,CB_SETCURSEL,3,0); /* Physical + touchpad: both ticked */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(!g.armed&&output_count==3&&output[2].type==HIDWAY_MSG_RELEASE&&!g.input_error&&!g.input_dirty);
    VERIFY(g.cfg.repeat_enabled&&g.cfg.repeat_cps==15&&g.cfg.virtual_click_cps==7&&g.cfg.source_touchpad&&
           g.cfg.repeat_button==1&&g.cfg.repeat_sources==3&&!g.cfg.loopback_mode&&g.cfg.relay_mouse);
    /* "All selected sources" always fits, whatever is ticked. */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_TOUCHPAD,BN_CLICKED),(LPARAM)g.src[1]);
    SendMessageA(g.repeat_source,CB_SETCURSEL,6,0);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(!g.input_error&&g.cfg.repeat_sources==7&&!g.cfg.source_touchpad);
    /* A source that is not ticked is fine while Hold to repeat is off. */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    SendMessageA(g.repeat_source,CB_SETCURSEL,1,0);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(!g.input_error&&!g.cfg.repeat_enabled&&g.cfg.repeat_sources==2);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_TOUCHPAD,BN_CLICKED),(LPARAM)g.src[1]);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(!g.input_error&&g.cfg.repeat_enabled&&g.cfg.source_touchpad&&g.cfg.repeat_sources==2);
    /* Outside preview the same Apply writes hidway.ini... */
    g.preview=0;SetWindowTextA(g.click_cps,"8");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(fixture_saves==1&&g.cfg.virtual_click_cps==8&&!memcmp(&fixture_saved,&g.cfg,sizeof g.cfg));
    VERIFY(profile_matches(&g.cfg.profiles[HIDWAY_PROFILE_REMOTE],&g.cfg));
    /* ...and a failed save changes nothing and says so. */
    fixture_save_ok=0;before=g.cfg;SetWindowTextA(g.click_cps,"9");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(fixture_saves==2&&!memcmp(&before,&g.cfg,sizeof before)&&g.input_error&&strstr(g.input_status,"Could not save"));
    fixture_save_ok=1;

    /* ---- Connection tab Save: an invalid field is refused in the window
     * (old value kept, nothing saved, not disarmed, no "Saved" flash). ---- */
    ui_state();select_tab(TAB_CONNECTION);set_armed(1);VERIFY(g.armed);
    before=g.cfg;output_count=0;
    static const struct { int field; const char *text, *message; } bad_fields[]={
        {0,"","relay address"},{0,"   ","relay address"},
        {1,"0","port"},{1,"65536","port"},{1,"","port"},{1,"47x","port"},{1,"99999999999","port"},{1,"-5","port"},
        {2,"0","rate"},{2,"9","rate"},{2,"1001","rate"},{2,"","rate"},
        {3,"-1","disarm"},{3,"60001","disarm"},{3,"","disarm"},{3,"3s","disarm"},
    };
    for(size_t i=0;i<sizeof bad_fields/sizeof bad_fields[0];i++) {
        connection_fields_load();VERIFY(!g.conn_status[0]&&!g.conn_error);
        SetWindowTextA(g.edit[bad_fields[i].field],bad_fields[i].text);
        g.saved_flash=0;
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save);
        if(!strstr(g.conn_status,bad_fields[i].message))printf("connection field %d \"%s\": \"%s\"\n",
            bad_fields[i].field,bad_fields[i].text,g.conn_status);
        VERIFY(g.armed&&output_count==0&&g.conn_error&&strstr(g.conn_status,"not saved")&&
               strstr(g.conn_status,bad_fields[i].message)&&!memcmp(&before,&g.cfg,sizeof before)&&
               !fixture_saves&&!g.saved_flash);
    }
    /* Range ends are accepted (spaces around a number too); Save disarms. */
    connection_fields_load();
    SetWindowTextA(g.edit[1]," 65535 ");SetWindowTextA(g.edit[2],"1000");SetWindowTextA(g.edit[3],"0");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save);
    VERIFY(!g.armed&&g.cfg.port==65535&&g.cfg.send_rate_hz==1000&&g.cfg.auto_disarm_ms==0&&!g.conn_status[0]&&
           g.saved_flash&&g.cfg.profiles[HIDWAY_PROFILE_REMOTE].port==65535);
    SetWindowTextA(g.edit[1],"1");SetWindowTextA(g.edit[2],"10");SetWindowTextA(g.edit[3],"60000");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save);
    VERIFY(g.cfg.port==1&&g.cfg.send_rate_hz==10&&g.cfg.auto_disarm_ms==60000&&!g.conn_error);
    /* A failed hidway.ini write changes nothing and says so in the window. */
    ui_state();g.preview=0;select_tab(TAB_CONNECTION);fixture_save_ok=0;before=g.cfg;
    SetWindowTextA(g.edit[2],"500");g.saved_flash=0;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save);
    VERIFY(fixture_saves==1&&!memcmp(&before,&g.cfg,sizeof before)&&g.conn_error&&
           strstr(g.conn_status,"Could not save")&&!g.saved_flash);
    fixture_save_ok=1;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save);
    VERIFY(fixture_saves==2&&g.cfg.send_rate_hz==500&&fixture_saved.send_rate_hz==500&&!g.conn_status[0]&&
           !g.conn_error&&g.saved_flash&&!strcmp(fixture_saved.target,"192.0.2.10")&&fixture_saved.port==47800);
    /* the status line is painted on the Connection tab in place of the hint */
    connection_status("Settings not saved: port must be 1 to 65535.",1);
    {
        HDC sdc=GetDC(NULL),mdc=CreateCompatibleDC(sdc);
        HBITMAP bm=CreateCompatibleBitmap(sdc,g.L.W,g.L.H);HGDIOBJ ob=SelectObject(mdc,bm);
        RECT all={0,0,g.L.W,g.L.H};paint_content(mdc,all);
        SelectObject(mdc,ob);DeleteObject(bm);DeleteDC(mdc);ReleaseDC(NULL,sdc);
    }
    connection_fields_load();VERIFY(!g.conn_status[0]); /* reloaded fields: hint again */

    /* ---- Route profiles: switching stores the route left, restores the
     * other route's own values, applies and saves. ---- */
    ui_state();g.preview=0;
    g.cfg.relay_mouse=0;g.cfg.source_touchpad=1;g.cfg.repeat_enabled=1;g.cfg.repeat_button=2;
    g.cfg.repeat_cps=12;g.cfg.repeat_sources=3;g.cfg.virtual_click_cps=9;
    hidway_config_t remote=g.cfg;
    select_tab(TAB_INPUT);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_REMOTE,BN_CLICKED),(LPARAM)g.profile_button[0]);
    VERIFY(!fixture_saves&&!g.cfg.loopback_mode); /* the route in use: nothing to do */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_GAMING,BN_CLICKED),(LPARAM)g.profile_button[1]);
    VERIFY(fixture_saves==1&&g.cfg.loopback_mode==1&&!memcmp(&fixture_saved,&g.cfg,sizeof g.cfg)&&!g.input_error);
    /* first use: the "Pico on this PC" defaults, same relay, same speeds */
    VERIFY(!strcmp(g.cfg.target,"192.0.2.10")&&g.cfg.port==47800&&g.cfg.relay_keyboard&&g.cfg.relay_mouse);
    VERIFY(g.cfg.source_virtual&&!g.cfg.source_physical&&!g.cfg.source_touchpad&&!g.cfg.loopback_pointer_pico);
    VERIFY(!g.cfg.repeat_enabled&&g.cfg.repeat_sources==HIDWAY_SOURCE_VIRTUAL&&g.cfg.repeat_cps==12&&g.cfg.virtual_click_cps==9);
    VERIFY(profile_matches(&g.cfg.profiles[HIDWAY_PROFILE_REMOTE],&remote));
    /* Physical stays available there: it is loopback_physical (off), never source_physical */
    VERIFY(input_source_mask()==HIDWAY_SOURCE_VIRTUAL&&IsWindowEnabled(g.src[0])&&!IsWindowEnabled(g.src[1])&&
           !IsWindowEnabled(g.src[2])&&IsWindowEnabled(g.pointer_route)&&cb_checked(ID_SRC_VIRTUAL)&&!cb_checked(ID_SRC_PHYSICAL)&&
           !g.cfg.loopback_physical);
    /* Gaming PC: Virtual cannot be switched off, even by a queued click. */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_VIRTUAL,BN_CLICKED),(LPARAM)g.src[2]);
    VERIFY(g.pending.source_virtual&&!g.input_dirty);
    /* Its own pointer, repeat, speeds, outputs and port. */
    SendMessageA(g.pointer_route,CB_SETCURSEL,1,0);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_CB_KBD,BN_CLICKED),(LPARAM)g.cb[0]);
    SetWindowTextA(g.repeat_cps,"20");SetWindowTextA(g.click_cps,"5");
    VERIFY(!IsWindowEnabled(g.repeat_source)&&SendMessageA(g.repeat_source,CB_GETCURSEL,0,0)==2);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(fixture_saves==2&&g.cfg.loopback_pointer_pico&&g.cfg.repeat_enabled&&g.cfg.repeat_cps==20&&
           g.cfg.virtual_click_cps==5&&!g.cfg.relay_keyboard&&g.cfg.repeat_sources==HIDWAY_SOURCE_VIRTUAL&&g.cfg.source_virtual);
    select_tab(TAB_CONNECTION);SetWindowTextA(g.edit[1],"47801");
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),(LPARAM)g.save); /* reopens UDP, sends nothing */
    VERIFY(fixture_saves==3&&g.cfg.port==47801&&g.cfg.profiles[HIDWAY_PROFILE_GAMING].port==47801);
    hidway_config_t gaming=g.cfg;
    /* Back to Remote PC: every Remote value returns, Gaming values are kept. */
    select_tab(TAB_INPUT);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_REMOTE,BN_CLICKED),(LPARAM)g.profile_button[0]);
    VERIFY(fixture_saves==4&&!g.cfg.loopback_mode&&route_values_equal(&remote,&g.cfg)&&
           profile_matches(&g.cfg.profiles[HIDWAY_PROFILE_GAMING],&gaming)&&!memcmp(&fixture_saved,&g.cfg,sizeof g.cfg));
    GetWindowTextA(g.edit[1],text,sizeof text);VERIFY(!strcmp(text,"47800"));
    VERIFY(IsWindowEnabled(g.src[0])&&!IsWindowEnabled(g.pointer_route)&&g.pending.source_touchpad);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_GAMING,BN_CLICKED),(LPARAM)g.profile_button[1]);
    VERIFY(fixture_saves==5&&g.cfg.loopback_mode&&route_values_equal(&gaming,&g.cfg)&&
           profile_matches(&g.cfg.profiles[HIDWAY_PROFILE_REMOTE],&remote));
    /* Switching while ARMED disarms first; a failed save changes nothing. */
    g.preview=1;set_armed(1);output_count=0;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_REMOTE,BN_CLICKED),(LPARAM)g.profile_button[0]);
    VERIFY(!g.armed&&output_count==3&&output[2].type==HIDWAY_MSG_RELEASE&&!g.cfg.loopback_mode);
    g.preview=0;fixture_save_ok=0;before=g.cfg;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_GAMING,BN_CLICKED),(LPARAM)g.profile_button[1]);
    VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&g.input_error&&strstr(g.input_status,"Could not save"));
    fixture_save_ok=1;hidway_net_close();

    /* ---- Physical on the Gaming PC route: bound to loopback_physical, never
     * to source_physical; applied only with Apply and save. ---- */
    ui_state();g.preview=0;g.cfg.source_physical=1;
    select_tab(TAB_INPUT);
    VERIFY(cb_checked(ID_SRC_PHYSICAL)&&!g.cfg.loopback_physical); /* Remote PC: source_physical */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_GAMING,BN_CLICKED),(LPARAM)g.profile_button[1]);
    VERIFY(g.cfg.loopback_mode&&!g.cfg.loopback_physical&&!cb_checked(ID_SRC_PHYSICAL)&&IsWindowEnabled(g.src[0])&&
           !IsWindowEnabled(g.src[1])&&!cb_checked(ID_SRC_TOUCHPAD)&&g.cfg.profiles[HIDWAY_PROFILE_REMOTE].source_physical);
    int saves=fixture_saves;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_PHYSICAL,BN_CLICKED),(LPARAM)g.src[0]);
    VERIFY(g.pending.loopback_physical&&!g.cfg.loopback_physical&&g.input_dirty&&cb_checked(ID_SRC_PHYSICAL)&&
           g.pending.source_physical==g.cfg.source_physical&&fixture_saves==saves);
    { /* the note is painted on the Input tab */
        HDC sdc=GetDC(NULL),mdc=CreateCompatibleDC(sdc);
        HBITMAP bm=CreateCompatibleBitmap(sdc,g.L.W,g.L.H);HGDIOBJ ob=SelectObject(mdc,bm);
        RECT all={0,0,g.L.W,g.L.H};paint_content(mdc,all);
        SelectObject(mdc,ob);DeleteObject(bm);DeleteDC(mdc);ReleaseDC(NULL,sdc);
    }
    /* leaving the tab drops it, like every other Input-tab choice */
    select_tab(TAB_TOOLS);select_tab(TAB_INPUT);
    VERIFY(!g.pending.loopback_physical&&!cb_checked(ID_SRC_PHYSICAL)&&!g.cfg.loopback_physical);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_PHYSICAL,BN_CLICKED),(LPARAM)g.src[0]);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(g.cfg.loopback_physical&&fixture_saved.loopback_physical&&!g.cfg.source_physical&&!g.input_error&&
           g.cfg.source_virtual&&input_source_mask()==HIDWAY_SOURCE_VIRTUAL);
    /* Remote PC shows its own source_physical; the Gaming PC value is kept */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_REMOTE,BN_CLICKED),(LPARAM)g.profile_button[0]);
    VERIFY(!g.cfg.loopback_mode&&g.cfg.source_physical&&cb_checked(ID_SRC_PHYSICAL)&&g.cfg.loopback_physical&&
           fixture_saved.loopback_physical);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_PHYSICAL,BN_CLICKED),(LPARAM)g.src[0]);
    VERIFY(!g.pending.source_physical&&g.pending.loopback_physical&&!cb_checked(ID_SRC_PHYSICAL));
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_PHYSICAL,BN_CLICKED),(LPARAM)g.src[0]); /* back on */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PROFILE_GAMING,BN_CLICKED),(LPARAM)g.profile_button[1]);
    VERIFY(g.cfg.loopback_mode&&cb_checked(ID_SRC_PHYSICAL)&&g.cfg.profiles[HIDWAY_PROFILE_REMOTE].source_physical);
    /* untick and apply on the Gaming PC route */
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_SRC_PHYSICAL,BN_CLICKED),(LPARAM)g.src[0]);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_INPUT_APPLY,BN_CLICKED),(LPARAM)g.input_apply);
    VERIFY(!g.cfg.loopback_physical&&!fixture_saved.loopback_physical&&!cb_checked(ID_SRC_PHYSICAL));
    hidway_net_close();

    /* ---- Pico on this PC selects the Gaming PC route with its own saved
     * values, shows the result on Control and never ARMs. ---- */
    ui_state();g.preview=0;fixture_pico=HIDWAY_PICO_READY;
    hidway_config_t routes=g.cfg;
    hidway_profile_select(&routes,HIDWAY_PROFILE_GAMING);routes.loopback_pointer_pico=1;routes.repeat_enabled=1;
    hidway_profile_select(&routes,HIDWAY_PROFILE_REMOTE);g.cfg=routes;
    select_tab(TAB_INPUT);
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),(LPARAM)g.local_pico_button);
    VERIFY(g.tab==TAB_CONTROL&&fixture_saves==1&&g.cfg.loopback_mode&&g.cfg.loopback_pointer_pico&&g.cfg.repeat_enabled);
    VERIFY(g.cfg.source_virtual&&g.cfg.repeat_sources==HIDWAY_SOURCE_VIRTUAL&&!g.armed&&output_count==0&&
           strstr(g.local_pico_notice,"through Pico"));
    /* Pico on this PC never switches physical-through-Pico on or off. */
    for(int on=0;on<2;on++) {
        ui_state();g.preview=0;fixture_pico=HIDWAY_PICO_READY;g.cfg.loopback_physical=on;
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),(LPARAM)g.local_pico_button);
        VERIFY(fixture_saves==1&&g.cfg.loopback_mode&&g.cfg.loopback_physical==on&&fixture_saved.loopback_physical==on&&!g.armed);
    }
    /* A Gaming PC route with another relay address is refused, not rewritten. */
    ui_state();g.preview=0;routes=g.cfg;
    hidway_profile_select(&routes,HIDWAY_PROFILE_GAMING);strcpy(routes.target,"127.0.0.1");
    hidway_profile_select(&routes,HIDWAY_PROFILE_REMOTE);g.cfg=routes;
    before=g.cfg;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),(LPARAM)g.local_pico_button);
    VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&!fixture_saves&&strstr(g.local_pico_notice,"127.0.0.1")&&!g.armed);

    /* ---- Pico status: read-only refresh from the GUI timer, indication only. ---- */
    COLORREF color;
    g.pico_known=0;fixture_pico_calls=0;
    VERIFY(!strcmp(pico_text(&color),"Checking..."));
    static const struct { unsigned state; const char *text; } pico_states[]={
        {0,"Not on this PC"},{HIDWAY_PICO_KEYBOARD,"Partly detected"},{HIDWAY_PICO_MOUSE,"Partly detected"},
        {HIDWAY_PICO_READY,"On this PC"},{0,"Not on this PC"}};
    for(size_t i=0;i<sizeof pico_states/sizeof pico_states[0];i++) {
        fixture_pico=pico_states[i].state;pico_refresh();
        VERIFY(!strcmp(pico_text(&color),pico_states[i].text)&&!g.armed&&!fixture_saves);
    }
    VERIFY(fixture_pico_calls==5);
    g.pico_pending=1;pico_refresh();VERIFY(fixture_pico_calls==5);g.pico_pending=0; /* never while one runs */
    VERIFY(TIMER_PICO!=TIMER_UI&&PICO_REFRESH_MS>=1000);
    g.preview=1;set_armed(1);VERIFY(g.armed);set_armed(0); /* "Not on this PC" gates nothing */

    /* ---- Start update check: the strict parser of the one result line. ---- */
    {
        static const struct { const char *out; int result; const char *tag; } lines[] = {
            {"HIDWAY-UPDATE state=Available tag=v0.6.1\r\n", HW_UPDATE_AVAILABLE, "v0.6.1"},
            {"HIDWAY-UPDATE state=Available tag=v10.20.300\n", HW_UPDATE_AVAILABLE, "v10.20.300"},
            {"HIDWAY-UPDATE state=Available tag=v999999.999999.999999", HW_UPDATE_AVAILABLE, "v999999.999999.999999"},
            {"HIDWAY-UPDATE state=Current tag=v0.6.0\r\n", HW_UPDATE_SILENT, ""},
            {"HIDWAY-UPDATE state=None tag=\r\n", HW_UPDATE_SILENT, ""},
            {"HIDWAY-UPDATE state=Older tag=v0.5.0\r\n", HW_UPDATE_SILENT, ""},
            {"HIDWAY-UPDATE state=Preview tag=v0.6.1\r\n", HW_UPDATE_SILENT, ""},
            {"HIDWAY-UPDATE state=Error tag=\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=RateLimited tag=\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=0.6.1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1.2\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1-rc1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=V0.6.1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v1234567.0.0\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1 \r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1 extra=1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1\t\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v\xef\xbc\x90.6.1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1\r\n\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1\r\nHIDWAY-UPDATE state=Available tag=v0.6.2\r\n", HW_UPDATE_FAILED, ""},
            {"WARNING: x\r\nHIDWAY-UPDATE state=Available tag=v0.6.1\r\n", HW_UPDATE_FAILED, ""},
            {"hidway-update state=Available tag=v0.6.1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE  state=Available tag=v0.6.1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=available tag=v0.6.1\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available tag=v0.6.1tag=v0.6.2\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Available\r\n", HW_UPDATE_FAILED, ""},
            {"HIDWAY-UPDATE state=Current tag=garbage\r\n", HW_UPDATE_FAILED, ""},
            {"", HW_UPDATE_FAILED, ""},
        };
        char tag[HW_UPDATE_TAG_MAX];
        for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
            strcpy(tag, "stale");
            int result = hw_update_parse(lines[i].out, strlen(lines[i].out), tag, sizeof tag);
            if (result != lines[i].result || strcmp(tag, lines[i].tag))
                printf("update line %u\n", (unsigned)i);
            VERIFY(result == lines[i].result && !strcmp(tag, lines[i].tag));
        }
        static const char nul_inside[] = "HIDWAY-UPDATE state=Available tag=v0.6.1\0\r\n";
        VERIFY(hw_update_parse(nul_inside, sizeof nul_inside - 1, tag, sizeof tag) == HW_UPDATE_FAILED && !tag[0]);
        static char overlong[HW_UPDATE_OUTPUT_MAX + 8];
        memset(overlong, 'A', sizeof overlong - 1);
        memcpy(overlong, "HIDWAY-UPDATE state=Available tag=v0.6.1", 40);
        VERIFY(hw_update_parse(overlong, strlen(overlong), tag, sizeof tag) == HW_UPDATE_FAILED && !tag[0]);
        char tiny[4];
        VERIFY(hw_update_parse(lines[0].out, strlen(lines[0].out), tiny, sizeof tiny) == HW_UPDATE_FAILED);
        VERIFY(hw_update_parse(NULL, 0, tag, sizeof tag) == HW_UPDATE_FAILED);
        VERIFY(hw_update_tag_valid("v0.6.1") && !hw_update_tag_valid("v0.6.") && !hw_update_tag_valid("") &&
               !hw_update_tag_valid(NULL) && !hw_update_tag_valid("v.6.1") && !hw_update_tag_valid("v0..1"));
    }

    /* ---- Update banner: shown only for Available, on every tab, hidden
     * while armed or replaying, Later until the next start, Update now
     * only while disarmed. Fixtures only: no PowerShell, updater or network. ---- */
    {
        MSG pending;
        ui_state();fixture_launches=0;fixture_launch_result=HW_COMPANION_OK;
        g.update_offer=0;g.update_tag[0]=g.update_notice[0]=0;banner_sync();
        VERIFY(!banner_visible()&&!shown(g.update_now)&&!shown(g.update_later));
        int listed=(int)SendMessageA(g.history,LB_GETCOUNT,0,0);
        update_result(HW_UPDATE_SILENT,"");
        VERIFY(!g.update_offer&&!shown(g.update_now)&&SendMessageA(g.history,LB_GETCOUNT,0,0)==listed);
        update_result(HW_UPDATE_FAILED,""); /* one quiet line, no banner, no dialog */
        VERIFY(!g.update_offer&&!shown(g.update_now)&&SendMessageA(g.history,LB_GETCOUNT,0,0)==listed+1);
        update_result(HW_UPDATE_AVAILABLE,"v0.6");
        update_result(HW_UPDATE_AVAILABLE,"v0.6.1 now");
        VERIFY(!g.update_offer&&!shown(g.update_now));
        wnd_proc(g.hwnd,WM_UPDATE_RESULT,HW_UPDATE_AVAILABLE,0); /* the worker left no valid tag */
        VERIFY(!g.update_offer);
        update_result(HW_UPDATE_AVAILABLE,"v0.6.1");
        VERIFY(g.update_offer==1&&banner_visible()&&shown(g.update_now)&&shown(g.update_later)&&
               !strcmp(g.update_tag,"v0.6.1")&&!g.armed&&!fixture_launches);
        for(int t=0;t<TAB_COUNT;t++){select_tab(t);VERIFY(shown(g.update_now)&&shown(g.update_later)&&shown(g.arm));}
        select_tab(TAB_CONTROL);
        /* armed: hidden (the timer's banner_sync), and Update now does nothing */
        set_armed(1);VERIFY(g.armed&&!banner_visible());
        banner_sync();VERIFY(!shown(g.update_now)&&!shown(g.update_later));
        g.preview=0;
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_UPDATE_NOW,BN_CLICKED),(LPARAM)g.update_now);
        VERIFY(g.armed&&!fixture_launches&&g.update_offer==1&&!PeekMessageA(&pending,g.hwnd,WM_CLOSE,WM_CLOSE,PM_REMOVE));
        g.preview=1;set_armed(0);banner_sync();
        VERIFY(!g.armed&&banner_visible()&&shown(g.update_now)&&shown(g.update_later));
        /* a replay in progress: hidden and refused as well */
        replay.active=1;g.preview=0;
        VERIFY(!banner_visible());
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_UPDATE_NOW,BN_CLICKED),(LPARAM)g.update_now);
        VERIFY(!fixture_launches&&g.update_offer==1);
        replay.active=0;banner_sync();VERIFY(shown(g.update_now));
        /* the launcher cannot be started: HIDway stays open and says so */
        fixture_launch_result=HW_COMPANION_MISSING;
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_UPDATE_NOW,BN_CLICKED),(LPARAM)g.update_now);
        VERIFY(fixture_launches==1&&fixture_launch_target==HW_COMPANION_UPDATE_NOW&&g.update_offer==1&&
               strstr(g.update_notice,"did not start")&&shown(g.update_now)&&
               !PeekMessageA(&pending,g.hwnd,WM_CLOSE,WM_CLOSE,PM_REMOVE));
        /* started: the banner goes and the window closes the normal way (WM_CLOSE) */
        fixture_launch_result=HW_COMPANION_OK;
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_UPDATE_NOW,BN_CLICKED),(LPARAM)g.update_now);
        VERIFY(fixture_launches==2&&fixture_launch_target==HW_COMPANION_UPDATE_NOW&&g.update_offer==2&&
               !shown(g.update_now)&&!shown(g.update_later)&&!g.armed&&
               PeekMessageA(&pending,g.hwnd,WM_CLOSE,WM_CLOSE,PM_REMOVE));
        g.preview=1;
        /* Later: hidden until the next start, also after arm/disarm and another result */
        g.update_offer=0;g.update_notice[0]=0;update_result(HW_UPDATE_AVAILABLE,"v0.6.1");VERIFY(shown(g.update_later));
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_UPDATE_LATER,BN_CLICKED),(LPARAM)g.update_later);
        VERIFY(g.update_offer==2&&!banner_visible()&&!shown(g.update_now)&&!shown(g.update_later));
        set_armed(1);set_armed(0);banner_sync();update_result(HW_UPDATE_AVAILABLE,"v0.6.2");
        VERIFY(g.update_offer==2&&!shown(g.update_now)&&!strcmp(g.update_tag,"v0.6.1"));
        wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_UPDATE_NOW,BN_CLICKED),(LPARAM)g.update_now);
        VERIFY(fixture_launches==2); /* a dismissed banner starts nothing */

        /* Banner geometry at 96/144/192 DPI (the combinations are covered by the
         * layout cases above): the blue bar under the tab strip, clear of the rail,
         * the tab strip and the body, its text and button labels inside it. */
        HDC dc=GetDC(g.hwnd);
        g.update_offer=1;
        for(UINT dpi=96;dpi<=192;dpi+=48) {
            g.dpi=dpi;create_fonts();compute_layout();
            RECT x;
            VERIFY(banner_visible()&&g.L.banner.left>=g.L.page.left&&g.L.banner.right<=g.L.page.right&&g.L.banner.bottom<=g.L.body.top);
            VERIFY(!IntersectRect(&x,&g.L.banner,&g.L.tabs)&&!IntersectRect(&x,&g.L.banner,&g.L.rail)&&
                   !IntersectRect(&x,&g.L.banner,&g.L.body)&&!IntersectRect(&x,&g.L.banner,&g.L.msg));
            VERIFY(g.L.banner_now.left>g.L.banner.left&&g.L.banner_now.right<g.L.banner_later.left&&
                   g.L.banner_later.right<=g.L.banner.right&&g.L.banner_now.top==g.L.banner_later.top);
            VERIFY(text_w(dc,g.f_btn,"HIDway v0.6.1 is available")<=g.L.banner_now.left-S(8)-(g.L.banner.left+S(35)));
            VERIFY(text_w(dc,g.f_btn,"The updater did not start")<=g.L.banner_now.left-S(8)-(g.L.banner.left+S(35)));
            VERIFY(text_w(dc,g.f_chip,"Update now")+S(10)<=g.L.banner_now.right-g.L.banner_now.left&&
                   text_w(dc,g.f_chip,"Later")+S(10)<=g.L.banner_later.right-g.L.banner_later.left);
            VERIFY(font_h(dc,g.f_chip)<=g.L.banner_now.bottom-g.L.banner_now.top);
        }
        g.update_offer=2;
        ReleaseDC(g.hwnd,dc);
        g.dpi=96;create_fonts();compute_layout();apply_layout();

        /* One check per start; update_check = 0, previews and recording starts start none. */
        g.update_check_started=0;fixture_checks=0;g.shot_path[0]=0;replay.path[0]=0;g.preview=0;
        g.cfg.update_check=0;update_check_start();VERIFY(!fixture_checks);
        g.cfg.update_check=1;update_check_start();update_check_start();VERIFY(fixture_checks==1);
        g.update_check_started=0;g.preview=1;update_check_start();VERIFY(fixture_checks==1);
        g.preview=0;wcscpy(replay.path,L"C:\\fixture.hidrec");update_check_start();VERIFY(fixture_checks==1);
        replay.path[0]=0;g.preview=1;
        /* An ini from before update_check keeps the check on; 0 turns it off. */
        char folder[MAX_PATH],ini[MAX_PATH];hidway_config_t loaded;
        GetTempPathA(MAX_PATH,folder);snprintf(ini,sizeof ini,"%shidway-client-test-%lu.ini",folder,GetCurrentProcessId());
        FILE *f=fopen(ini,"w");
        if(f){fputs("target = 192.0.2.1\nport = 47800\nbadge = 0\n",f);fclose(f);}
        VERIFY(hidway_config_load(ini,&loaded)&&loaded.update_check==1);
        f=fopen(ini,"w");
        if(f){fputs("target = 192.0.2.1\nupdate_check = 0\n",f);fclose(f);}
        VERIFY(hidway_config_load(ini,&loaded)&&loaded.update_check==0);
        DeleteFileA(ini);
        g.update_offer=0;banner_sync();
    }

    DestroyWindow(g.hwnd);g.hwnd=NULL;destroy_fonts();
    GdiplusShutdown(gdip_token);
    printf("Client regression tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
