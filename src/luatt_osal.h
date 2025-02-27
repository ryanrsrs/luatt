#ifndef LUATT_OSAL_H
#define LUATT_OSAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common output muxing */

void luatt_set_mux_token(const char* token);
const char* luatt_get_mux_token();

int luatt_print(const char* s);
int luatt_printf(const char* fmt, ...);
int luatt_write(const char* buf, size_t bytes);



/* Need to implement these for each OS */

uint32_t luatt_millis();
uint32_t luatt_micros();
void luatt_delay(uint32_t ms);
void luatt_yield();

int luatt_raw_write(const char* buf, size_t bytes);
bool luatt_is_connected();
int luatt_available();
int luatt_read_char();

#ifdef __cplusplus
}
#endif

#endif