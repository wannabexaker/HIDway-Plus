#ifndef HIDWAY_STATUS_ORDER_H
#define HIDWAY_STATUS_ORDER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t last_echo_us;
    bool seen;
} hidway_status_order_t;

/* Echo timestamps come from this client's monotonic 32-bit clock. Accept
 * only newer echoes within the half-range, including across clock wrap.
 * Session/key validation remains the caller's responsibility. */
static inline bool hidway_status_order_accept(hidway_status_order_t *order, uint32_t echo_us)
{
    uint32_t distance = echo_us - order->last_echo_us;
    if (order->seen && (!distance || distance >= 0x80000000u))
        return false;
    order->last_echo_us = echo_us;
    order->seen = true;
    return true;
}

#endif
