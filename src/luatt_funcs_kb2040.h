#ifndef LUATT_FUNCS_KB2040_H
#define LUATT_FUNCS_KB2040_H

#ifdef ARDUINO_RASPBERRY_PI_PICO

// Register Lua functions for Adafruit KB2040 built-in hardware.
// https://www.adafruit.com/product/5302

struct lua_State;

class Adafruit_NeoPixel;
void luatt_setup_funcs_neopixel(lua_State* L, Adafruit_NeoPixel* neopix, bool implicit_show=true);

#endif

#endif
