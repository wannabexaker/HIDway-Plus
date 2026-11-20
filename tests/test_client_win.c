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
int test_peer_is_loopback(void) { return fixture_peer; }
unsigned test_local_pico_interfaces(const char *token) { (void)token; return fixture_pico; }
int test_config_save(const char *path,const hidway_config_t *cfg)
{
    (void)path;fixture_saves++;fixture_saved=*cfg;return fixture_save_ok;
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

    /* The one-click preset never rewrites the Pi address, arms, or writes
     * settings when either required USB collection or relay peer is missing. */
    reset_client();g.preview=0;g.armed=0;g.dpi=96;g.inst=GetModuleHandleA(NULL);
    strcpy(g.cfg.target,"192.0.2.10");g.cfg.port=47800;
    g.cfg.source_touchpad=1;g.cfg.repeat_enabled=1;
    g.cfg.loopback_pointer_pico=1;
    options=CreateWindowExA(0,wc.lpszClassName,"Preset fixture",0,0,0,470,520,NULL,NULL,g.inst,NULL);
    g.options=options;VERIFY(options!=NULL);
    fixture_pico=HIDWAY_PICO_READY;fixture_peer=1;fixture_saves=0;
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(IsWindow(options)&&!g.cfg.loopback_mode&&!fixture_saves&&!g.armed);
    fixture_peer=-1;
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(IsWindow(options)&&!g.cfg.loopback_mode&&!fixture_saves);
    fixture_peer=0;
    for(unsigned present=0;present<HIDWAY_PICO_READY;present++) {
        fixture_pico=present;
        options_proc(options,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
        VERIFY(IsWindow(options)&&!g.cfg.loopback_mode&&!fixture_saves);
    }
    fixture_pico=HIDWAY_PICO_READY;fixture_save_ok=0;
    hidway_config_t before=g.cfg;
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(IsWindow(options)&&!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1&&!g.armed);
    /* Ordinary Apply also keeps runtime config unchanged on save failure. */
    SendMessageA(g.route,CB_SETCURSEL,1,0);
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_SAVE,BN_CLICKED),0);
    VERIFY(IsWindow(options)&&!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==2);
    fixture_save_ok=1;
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_LOCAL_PICO,BN_CLICKED),0);
    VERIFY(!IsWindow(options)&&fixture_saves==3&&!g.armed);
    VERIFY(g.cfg.loopback_mode&&g.cfg.source_virtual&&!g.cfg.loopback_pointer_pico&&!g.cfg.repeat_enabled);
    VERIFY(g.cfg.source_physical&&g.cfg.source_touchpad&&input_source_mask()==HIDWAY_SOURCE_VIRTUAL);
    VERIFY(g.cfg.relay_keyboard&&g.cfg.relay_mouse&&g.cfg.repeat_sources==HIDWAY_SOURCE_VIRTUAL);
    VERIFY(!strcmp(g.cfg.target,"192.0.2.10")&&g.cfg.port==47800&&!memcmp(&fixture_saved,&g.cfg,sizeof g.cfg));

    /* The visible HUD button uses the same production preset and does not
     * ARM, create a capture hook, send packets, or change the relay address. */
    reset_client();g.preview=0;g.armed=0;g.dpi=96;g.inst=GetModuleHandleA(NULL);
    strcpy(g.cfg.target,"192.0.2.10");g.cfg.port=47800;
    g.cfg.source_touchpad=1;g.cfg.loopback_pointer_pico=1;g.cfg.repeat_enabled=1;
    before=g.cfg;fixture_saves=0;fixture_peer=0;fixture_pico=HIDWAY_PICO_READY;
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
    VERIFY(!memcmp(&before,&g.cfg,sizeof before)&&fixture_saves==1&&strstr(g.local_pico_notice,"Settings first"));
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
    VERIFY(input_source_mask()==HIDWAY_SOURCE_VIRTUAL&&g.cfg.source_physical&&g.cfg.source_touchpad);
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

    /* Hold to repeat greys out its button, source and speed (values kept),
     * from the first open on; Cancel closes like the title-bar close. */
    char text[64];
    reset_client();g.preview=0;g.armed=0;g.dpi=96;g.inst=GetModuleHandleA(NULL);
    g.cfg.repeat_enabled=0;g.cfg.repeat_button=2;g.cfg.repeat_cps=12;g.cfg.repeat_sources=4;
    options=CreateWindowExA(0,wc.lpszClassName,"Repeat fixture",0,0,0,470,540,NULL,NULL,g.inst,NULL);
    g.options=options;VERIFY(options!=NULL);
    VERIFY(!IsWindowEnabled(g.repeat_button)&&!IsWindowEnabled(g.repeat_source)&&!IsWindowEnabled(g.repeat_cps));
    VERIFY(IsWindowEnabled(g.click_cps)&&IsWindowEnabled(g.route)&&!IsWindowEnabled(g.pointer_route));
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    VERIFY(g.options_repeat&&IsWindowEnabled(g.repeat_button)&&IsWindowEnabled(g.repeat_source)&&IsWindowEnabled(g.repeat_cps));
    VERIFY(SendMessageA(g.repeat_button,CB_GETCURSEL,0,0)==2&&SendMessageA(g.repeat_source,CB_GETCURSEL,0,0)==2);
    SendMessageA(g.route,CB_SETCURSEL,1,0);
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_ROUTE,CBN_SELCHANGE),(LPARAM)g.route);
    VERIFY(!IsWindowEnabled(g.repeat_source)&&IsWindowEnabled(g.repeat_button)&&IsWindowEnabled(g.repeat_cps));
    SendMessageA(g.route,CB_SETCURSEL,0,0);
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_ROUTE,CBN_SELCHANGE),(LPARAM)g.route);
    VERIFY(IsWindowEnabled(g.repeat_source));
    options_proc(options,WM_COMMAND,MAKEWPARAM(ID_REPEAT,BN_CLICKED),(LPARAM)g.repeat);
    VERIFY(!g.options_repeat&&!IsWindowEnabled(g.repeat_button)&&!IsWindowEnabled(g.repeat_source)&&!IsWindowEnabled(g.repeat_cps));
    GetWindowTextA(g.repeat_cps,text,sizeof text);
    VERIFY(!strcmp(text,"12")&&SendMessageA(g.repeat_button,CB_GETCURSEL,0,0)==2&&SendMessageA(g.repeat_source,CB_GETCURSEL,0,0)==2);
    before=g.cfg;fixture_saves=0;
    options_proc(options,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),0);
    VERIFY(!IsWindow(options)&&!g.options&&!fixture_saves&&!memcmp(&before,&g.cfg,sizeof before));
    g.cfg.repeat_enabled=1;
    options=CreateWindowExA(0,wc.lpszClassName,"Repeat fixture",0,0,0,470,540,NULL,NULL,g.inst,NULL);
    g.options=options;VERIFY(options!=NULL&&g.options_repeat);
    VERIFY(IsWindowEnabled(g.repeat_button)&&IsWindowEnabled(g.repeat_source)&&IsWindowEnabled(g.repeat_cps));
    before=g.cfg;
    options_proc(options,WM_CLOSE,0,0);
    VERIFY(!IsWindow(options)&&!fixture_saves&&!memcmp(&before,&g.cfg,sizeof before));

    /* Compact view hides RECENT INPUT and the settings fields without touching
     * their values, keeps ARM / PANIC / Pico on this PC visible with their own
     * enable rules (also while ARMED), never disarms, and saves only the choice. */
    reset_client();g.preview=0;g.armed=0;g.dpi=96;g.inst=GetModuleHandleA(NULL);
    strcpy(g.cfg.target,"192.0.2.10");g.cfg.port=47800;
    WNDCLASSA hud={0};hud.lpfnWndProc=DefWindowProcA;hud.hInstance=g.inst;hud.lpszClassName="HIDwayHudFixture";
    VERIFY(RegisterClassA(&hud)!=0);
    g.hwnd=CreateWindowExA(0,hud.lpszClassName,"HUD fixture",WIN_STYLE,0,0,200,200,NULL,NULL,g.inst,NULL);
    VERIFY(g.hwnd!=NULL&&!IsWindowVisible(g.hwnd));
    create_fonts();compute_layout();create_controls(g.hwnd);
    HWND hidden[]={g.history,g.clear,g.save,g.edit[0],g.edit[1],g.edit[2],g.edit[3],
                   g.cb[0],g.cb[1],g.cb[2],g.src[0],g.src[1],g.src[2]};
    HWND kept[]={g.arm,g.panic,g.local_pico_button,g.options_button,g.monitor_button,g.replay_button,
                 g.compact_button,g.updates_button};
    const size_t n_hidden=sizeof hidden/sizeof hidden[0],n_kept=sizeof kept/sizeof kept[0];
    for(size_t i=0;i<n_hidden;i++)VERIFY(shown(hidden[i]));
    for(size_t i=0;i<n_kept;i++)VERIFY(shown(kept[i]));
    VERIFY(!g.compact&&g.L.H==S(LAYOUT_FULL_H)&&GetDlgCtrlID(g.compact_button)==ID_COMPACT);
    GetWindowTextA(g.options_button,text,sizeof text);VERIFY(!strcmp(text,"Remote PC..."));
    GetWindowTextA(g.compact_button,text,sizeof text);VERIFY(!strcmp(text,"Less"));
    SetWindowTextA(g.edit[0],"192.0.2.99");SetWindowTextA(g.edit[2],"500");
    SendMessageA(g.history,LB_ADDSTRING,0,(LPARAM)"12:00:00.000\x1fW\x1f" "d");
    hidway_config_t settings=g.cfg;fixture_saves=0;
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_COMPACT,BN_CLICKED),(LPARAM)g.compact_button);
    VERIFY(g.compact&&g.cfg.compact&&fixture_saves==1&&fixture_saved.compact==1);
    settings.compact=1;VERIFY(!memcmp(&settings,&g.cfg,sizeof settings)); /* edits are not applied */
    for(size_t i=0;i<n_hidden;i++)VERIFY(!shown(hidden[i]));
    for(size_t i=0;i<n_kept;i++)VERIFY(shown(kept[i]));
    VERIFY(IsWindowEnabled(g.arm)&&IsWindowEnabled(g.panic)&&IsWindowEnabled(g.local_pico_button));
    VERIFY(g.L.H==S(LAYOUT_COMPACT_H)&&g.L.arm.bottom<g.L.H&&g.L.panic.bottom<g.L.H&&g.L.local_pico.bottom<g.L.H);
    VERIFY(g.L.settings.bottom<=g.L.arm.top&&g.L.live.bottom<=g.L.settings.top);
    GetWindowTextA(g.compact_button,text,sizeof text);VERIFY(!strcmp(text,"More"));
    GetWindowTextA(g.edit[0],text,sizeof text);VERIFY(!strcmp(text,"192.0.2.99"));
    GetWindowTextA(g.edit[2],text,sizeof text);VERIFY(!strcmp(text,"500"));
    VERIFY(SendMessageA(g.history,LB_GETCOUNT,0,0)==1&&g.cfg.relay_keyboard&&g.cfg.source_virtual);
    g.preview=1; /* ARMED in the harness: no hooks or sender thread */
    set_armed(1);VERIFY(g.armed&&!IsWindowEnabled(g.local_pico_button)&&IsWindowEnabled(g.arm)&&IsWindowEnabled(g.panic));
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_COMPACT,BN_CLICKED),(LPARAM)g.compact_button);
    VERIFY(g.armed&&!g.compact&&!g.cfg.compact);
    for(size_t i=0;i<n_hidden;i++)VERIFY(shown(hidden[i]));
    for(size_t i=0;i<n_kept;i++)VERIFY(shown(kept[i]));
    VERIFY(!IsWindowEnabled(g.local_pico_button)&&IsWindowEnabled(g.arm)&&IsWindowEnabled(g.panic));
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_COMPACT,BN_CLICKED),(LPARAM)g.compact_button);
    VERIFY(g.armed&&g.compact);
    for(size_t i=0;i<n_kept;i++)VERIFY(shown(kept[i]));
    VERIFY(!IsWindowEnabled(g.local_pico_button)&&IsWindowEnabled(g.arm)&&IsWindowEnabled(g.panic));
    wnd_proc(g.hwnd,WM_COMMAND,MAKEWPARAM(ID_PANIC,BN_CLICKED),(LPARAM)g.panic);
    VERIFY(!g.armed&&g.compact&&IsWindowEnabled(g.local_pico_button)&&IsWindowEnabled(g.arm));
    GetWindowTextA(g.edit[0],text,sizeof text);VERIFY(!strcmp(text,"192.0.2.99"));
    /* Start-up fallback: compact when the full window does not fit, without
     * changing (or saving) the saved preference. */
    g.cfg.compact=0;fixture_saves=0;
    HMONITOR monitor=MonitorFromWindow(g.hwnd,MONITOR_DEFAULTTONEAREST);
    g.preview_work_h=300;VERIFY(want_compact(monitor));
    g.preview_work_h=5000;VERIFY(!want_compact(monitor));
    g.cfg.compact=1;VERIFY(want_compact(monitor));g.cfg.compact=0;
    g.preview_work_h=0;
    set_compact(0,0);VERIFY(!g.compact&&shown(g.save));
    set_compact(1,0);VERIFY(g.compact&&!g.cfg.compact&&!fixture_saves&&!shown(g.save));
    DestroyWindow(g.hwnd);g.hwnd=NULL;destroy_fonts();
    printf("Client regression tests: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
