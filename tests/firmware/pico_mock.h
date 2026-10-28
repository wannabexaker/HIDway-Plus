#ifndef HIDWAY_PICO_MOCK_H
#define HIDWAY_PICO_MOCK_H
#include <stdbool.h>
#include <stdint.h>
typedef int64_t absolute_time_t;
typedef unsigned hid_report_type_t;
#define HID_REPORT_TYPE_OUTPUT 2u
#define HID_PROTOCOL_BOOT 0u
bool tud_hid_n_ready(uint8_t n);
bool tud_hid_n_keyboard_report(uint8_t n, uint8_t id, uint8_t mods, const uint8_t keys[6]);
bool tud_hid_n_report(uint8_t n, uint8_t id, const void *data, uint16_t len);
uint8_t tud_hid_n_get_protocol(uint8_t n);
bool tud_mounted(void);
bool tud_suspended(void);
bool tud_remote_wakeup(void);
absolute_time_t get_absolute_time(void);
bool time_reached(absolute_time_t t);
absolute_time_t make_timeout_time_us(uint32_t us);
#endif
