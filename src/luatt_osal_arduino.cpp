#ifdef ARDUINO

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include <stdint.h>

#include "luatt_osal.h"

uint32_t luatt_millis() {
    return millis();
}

uint32_t luatt_micros() {
    return micros();
}

void luatt_delay(uint32_t ms) {
    delay(ms);
}

void luatt_yield() {
    yield();
}

int luatt_raw_write(const char* buf, size_t bytes) {
    return Serial.write(buf, bytes);
}

bool luatt_is_connected() {
    return (bool)Serial;
}

int luatt_available() {
    return Serial.available();
}

int luatt_read_char() {
    return Serial.read();
}

#endif
