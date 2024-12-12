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

static int lf_neopix_set_hsv(lua_State *L) {
    if (State_neopix.neopix == 0) return 0;

    uint16_t hue = luaL_checkinteger(L, 1);
    int ok;
    uint8_t sat = lua_tointegerx(L, 2, &ok);
    if (!ok) sat = 255;
    uint8_t val = lua_tointegerx(L, 3, &ok);
    if (!ok) val = 255;
    uint32_t rgb = Adafruit_NeoPixel::ColorHSV(hue, sat, val);
    State_neopix.neopix->setPixelColor(0, rgb);
    return 0;
}

static int lf_neopix_show(lua_State *L) {
    if (State_neopix.neopix == 0) return 0;
    State_neopix.neopix->show();
    return 0;
}

void luatt_setfuncs_neopixel(lua_State* L, Adafruit_NeoPixel* neopix, bool implicit_show) {
    State_neopix.neopix = neopix;
    State_neopix.implicit_show = implicit_show;

    static const struct luaL_Reg neopix_table[] = {
        { "set_brightness", lf_neopix_set_brightness },
        { "set_color",      lf_neopix_set_color },
        { "set_hsv",        lf_neopix_set_hsv },
        { "show",           lf_neopix_show },
        { 0, 0 }
    };
    luaL_setfuncs(L, neopix_table, 0);
}

#endif
