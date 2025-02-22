#ifndef LUATT_FUNCS_ITSYBITSY_H
#define LUATT_FUNCS_ITSYBITSY_H

#ifdef ARDUINO_NRF52840_ITSYBITSY

// Register Lua functions for Adafruit ItsyBitsy nRF52840 built-in hardware.
// https://www.adafruit.com/product/4481

struct lua_State;

class Adafruit_DotStar;
void luatt_setfuncs_dotstar(lua_State* L, Adafruit_DotStar* dotstar, bool implicit_show=true);

void luatt_setfuncs_red_led(lua_State* L, int led_pin, bool active_low=false);

#endif

#endif
