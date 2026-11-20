/* Regression coverage of production handlers. Hidden fixture controls only;
 * no global hooks or physical HID devices are opened. Peer-address checks
 * connect UDP sockets without sending any packets. */
#define hidway_net_send test_send
#define hidway_net_recv test_recv
#define hidway_net_peer_is_loopback test_peer_is_loopback
#define hidway_local_pico_interfaces test_local_pico_interfaces
#define hidway_config_save test_config_save
#include "../client/src/main.c"
#undef hidway_net_send
#undef hidway_net_recv
#undef hidway_net_peer_is_loopback
#undef hidway_local_pico_interfaces
#undef hidway_config_save

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
    g.preview = 1; g.armed = 0; g.net_ok = 1; g.session_id = 456;
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
    /* One size for every tab; the action row is below every page. It fits a
     * 1366x768 work area at 100 % and a 1920x1080 one at 150 %. */
    VERIFY(LAYOUT_H<=520&&g.L.H==S(LAYOUT_H)&&g.L.arm.bottom<g.L.H&&g.L.panic.bottom<g.L.H&&g.L.local_pico.bottom<g.L.H);
    VERIFY(g.L.tabs.bottom<=g.L.page.top&&g.L.live.bottom<=g.L.arm.top&&g.L.settings.bottom<=g.L.arm.top&&
           g.L.input.bottom<=g.L.arm.top&&g.L.recent.bottom<=g.L.arm.top&&g.L.status.bottom<=g.L.input.bottom);
    RECT frame96={0,0,S(LAYOUT_W),S(LAYOUT_H)};AdjustWindowRectExForDpi(&frame96,WIN_STYLE,FALSE,0,96);
    VERIFY(frame96.bottom-frame96.top<=728&&frame96.right-frame96.left<=1366);
    g.dpi=144;create_fonts();compute_layout();apply_layout();
    RECT frame144={0,0,g.L.W,g.L.H};AdjustWindowRectExForDpi(&frame144,WIN_STYLE,FALSE,0,144);
    VERIFY(g.L.H==780&&frame144.bottom-frame144.top<=1032&&frame144.right-frame144.left<=1920);
    /* LIVE INPUT reaches down to just above the action row at every DPI, like
     * the cards of the other tabs (no empty band on Control). */
    for(UINT dpi=96;dpi<=192;dpi+=48) {
        g.dpi=dpi;compute_layout();
        VERIFY(g.L.live.bottom==g.L.recent.bottom&&g.L.live.bottom<g.L.arm.top&&g.L.arm.top-g.L.live.bottom<=S(14)&&
               g.L.live.top>g.L.stats.bottom);
    }
    g.dpi=144;compute_layout();
    g.dpi=96;create_fonts();compute_layout();apply_layout();
    /* WAKE sits on the value line of a GAMING PC tile wide enough for it. */
    VERIFY(g.L.tiles[3].right-g.L.tiles[3].left>=S(140)&&g.L.tiles[3].right<=g.L.stats.right-S(14));

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

    DestroyWindow(g.hwnd);g.hwnd=NULL;destroy_fonts();
    printf("Client regression tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
