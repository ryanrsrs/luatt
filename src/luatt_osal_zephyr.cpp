#ifdef __ZEPHYR__

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "luatt_osal.h"

uint32_t luatt_millis() {
    return k_uptime_get();
}

uint32_t luatt_micros() {
    uint64_t cyc = k_cycle_get_64();
    return k_cyc_to_us_near32(cyc);
}

void luatt_delay(uint32_t ms) {
    k_msleep(ms);
}

void luatt_yield() {
    k_yield();
}

void ring_write_block(const char* buf, size_t bytes);
int ring_read_available();
int ring_read_byte();
bool ring_is_connected();

int luatt_raw_write(const char* buf, size_t bytes) {
    ring_write_block(buf, bytes);
    return bytes;
}

bool luatt_is_connected() {
    return ring_is_connected();
}

int luatt_available() {
    return ring_read_available();
}

int luatt_read_char() {
    return ring_read_byte();
}

#endif
