#ifdef ARDUINO_RASPBERRY_PI_PICO

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>

#include "luatt_context.h"
#include "luatt_funcs_kb2040.h"

///////////////////////////////////
// NeoPixel LED (single).

static struct {
    Adafruit_NeoPixel* neopix;
    bool implicit_show;
} State_neopix;

static int lf_neopix_set_brightness(lua_State *L) {
    if (State_neopix.neopix == 0) return 0;

    int x = 256 * luaL_checknumber(L, 1);
    if (x < 0) x = 0;
    else if (x > 255) x = 255;

    State_neopix.neopix->setBrightness(x);
    if (State_neopix.implicit_show) {
        State_neopix.neopix->show();
    }
    return 0;
}

static int lf_neopix_set_color(lua_State *L) {
    if (State_neopix.neopix == 0) return 0;

    uint32_t x = luaL_checkinteger(L, 1);

    State_neopix.neopix->setPixelColor(0, x);
    if (State_neopix.implicit_show) {
        State_neopix.neopix->show();
    }
    return 0;
}

static int lf_neopix_show(lua_State *L) {
    if (State_neopix.neopix == 0) return 0;
    State_neopix.neopix->show();
    return 0;
}

void luatt_setup_funcs_neopixel(lua_State* L, Adafruit_NeoPixel* neopix, bool implicit_show) {
    State_neopix.neopix = neopix;
    State_neopix.implicit_show = implicit_show;

    static const struct luaL_Reg neopix_table[] = {
        { "set_brightness", lf_neopix_set_brightness },
        { "set_color",      lf_neopix_set_color },
        { "show",           lf_neopix_show },
        { 0, 0 }
    };
    lua_newtable(L);
    luaL_setfuncs(L, neopix_table, 0);
    lua_setglobal(L, "rgb_led");
}

#endif
