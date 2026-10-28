/* Regression coverage of production handlers. Hidden fixture controls only;
 * no global hooks, sockets or physical HID devices are opened by this test. */
#define hidway_net_send test_send
#define hidway_net_recv test_recv
#include "../client/src/main.c"
#undef hidway_net_send
#undef hidway_net_recv

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

    /* Hidden native controls exercise validation and apply without hooks,
     * sockets, file writes or foreground activation. */
    reset_client();
    g.dpi = 96;
    g.inst = GetModuleHandleA(NULL);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = options_proc;
    wc.hInstance = g.inst;
    wc.lpszClassName = "HIDwayOptionsFixture";
    VERIFY(RegisterClassA(&wc) != 0);
    HWND options = CreateWindowExA(0, wc.lpszClassName, "Options fixture", 0, 0, 0, 470, 470, NULL,
                                   NULL, g.inst, NULL);
    VERIFY(options != NULL);
    g.options = options;
    VERIFY(!IsWindowEnabled(g.pointer_route)); /* Another-PC always uses Pico motion */
    SetWindowTextA(g.repeat_cps, "0");
    options_proc(options, WM_COMMAND, MAKEWPARAM(ID_SAVE, BN_CLICKED), 0);
    VERIFY(IsWindow(options) && g.cfg.repeat_cps == 10);
    SetWindowTextA(g.repeat_cps, "15");
    SetWindowTextA(g.click_cps, "7");
    SendMessageA(g.route, CB_SETCURSEL, 1, 0);
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_ROUTE,CBN_SELCHANGE),0);
    VERIFY(!IsWindowEnabled(g.repeat_source)&&SendMessageA(g.repeat_source,CB_GETCURSEL,0,0)==2);
    VERIFY(IsWindowEnabled(g.pointer_route));
    SendMessageA(g.pointer_route,CB_SETCURSEL,1,0);
    g.options_repeat = 1;
    options_proc(options, WM_COMMAND, MAKEWPARAM(ID_SAVE, BN_CLICKED), 0);
    VERIFY(!IsWindow(options) && g.cfg.repeat_enabled && g.cfg.repeat_cps == 15 &&
           g.cfg.virtual_click_cps == 7 && g.cfg.loopback_mode&&g.cfg.repeat_sources==HIDWAY_SOURCE_VIRTUAL&&
           g.cfg.loopback_pointer_pico);
    reset_client();uint32_t old_session=g.session_id;
    set_armed(0);set_armed(1);
    VERIFY(g.armed&&g.session_id!=old_session&&g.seq==1&&output[3].session_id==g.session_id);
    set_armed(0);
    printf("Client regression tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
