#define _GNU_SOURCE
#include <stdarg.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
static ssize_t queue_write(int fd, const void *data, size_t len);
static int queue_ioctl(int fd, unsigned long request, ...);
static int queue_flush(int fd, int selector);
#define write queue_write
#define ioctl queue_ioctl
#define tcflush queue_flush
#define main unused_relay_main
#include "../../relay/hidwayd.c"
#undef main
#undef write
#undef ioctl
#undef tcflush
static int busy = 1;
static unsigned failures, count;
static hidway_serial_state_t emitted[128];
static hidway_deframer_t decoder;
static ssize_t queue_write(int fd, const void *data, size_t len)
{
    (void)fd;
    const uint8_t *b = data;
    for (size_t i = 0; i < len; i++) {
        hidway_serial_state_t s;
        if (hidway_deframer_push(&decoder, b[i], &s) && count < 128)
            emitted[count++] = s;
    }
    return (ssize_t)len;
}
static int queue_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    (void)request;
    va_list ap;
    va_start(ap, request);
    int *n = va_arg(ap, int *);
    *n = busy;
    va_end(ap);
    return 0;
}
static int queue_flush(int fd, int selector)
{
    (void)fd;
    (void)selector;
    return 0;
}
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
int main(void)
{
    struct in_addr allowed[8],peer;unsigned count_allowed;
    CHECK(parse_allow_list("127.0.0.1,127.0.0.3",allowed,&count_allowed)&&count_allowed==2);
    CHECK(append_allow_list("127.0.0.3,127.0.0.4",allowed,&count_allowed)&&count_allowed==3);
    inet_pton(AF_INET,"127.0.0.3",&peer);CHECK(peer_allowed(peer,allowed,count_allowed));
    inet_pton(AF_INET,"127.0.0.2",&peer);CHECK(!peer_allowed(peer,allowed,count_allowed));
    CHECK(!parse_allow_list("127.0.0.1,",allowed,&count_allowed));
    CHECK(!parse_allow_list("127.0.0.1,bad",allowed,&count_allowed));
    serial_link_t l = {0};
    l.fd = 123;
    l.pico_fault = true;
    CHECK(status_flags(&l) & HIDWAY_STATUS_INPUT_FAULT);
    l.pico_fault = false;
    hidway_input_pkt_t p = {0};
    p.seq = 1;
    hidway_bitmap_set(p.keys, 4);
    serial_send_state(&l, &p);
    p.x = 10;
    serial_send_state(&l, &p);
    CHECK(l.have_pending && l.edge_count == 0 && l.replaced == 1);
    memset(p.keys, 0, sizeof p.keys);
    serial_send_state(&l, &p);
    p.buttons = 1;
    serial_send_state(&l, &p);
    CHECK(l.edge_count == 2);
    busy = 0;
    for (unsigned i = 0; i < 4; i++)
        serial_pump(&l);
    CHECK(count == 3 && hidway_bitmap_test(emitted[0].keys, 4) && emitted[0].x == 10);
    CHECK(!hidway_bitmap_test(emitted[1].keys, 4) && !emitted[1].buttons &&
          emitted[2].buttons == 1);
    busy = 1;
    count = 0;
    for (unsigned i = 0; i < 36; i++) {
        p.buttons = (uint8_t)(i % 2);
        serial_send_state(&l, &p);
    }
    CHECK(l.overloaded && !l.edge_count && l.pending.type == HIDWAY_SER_RELEASE);
    p.buttons = 1;
    serial_send_state(&l, &p);
    CHECK(l.pending.type == HIDWAY_SER_RELEASE);
    busy = 0;
    serial_pump(&l);
    CHECK(count == 1 && emitted[0].type == HIDWAY_SER_RELEASE);
    serial_send_release(&l, 99);
    CHECK(!l.overloaded);
    busy = 1;
    p.buttons = 1;
    serial_send_state(&l, &p);
    p.buttons = 0;
    serial_send_state(&l, &p);
    serial_send_release(&l, 100);
    CHECK(!l.edge_count && l.pending.type == HIDWAY_SER_RELEASE);
    busy = 0;
    serial_pump(&l);
    CHECK(emitted[count - 1].type == HIDWAY_SER_RELEASE);
    busy = 1;
    p.buttons = 1;
    serial_send_state(&l, &p);
    l.pending_at = now_ms() - 101;
    serial_pump(&l);
    CHECK(l.overloaded && l.pending.type == HIDWAY_SER_RELEASE && !l.edge_count);
    printf("Relay bounded serial edge queue: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
