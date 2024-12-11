#ifdef ARDUINO_NRF52840_ITSYBITSY

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_DotStar.h>

#include "luatt_context.h"
#include "luatt_funcs_itsybitsy.h"

///////////////////////////////////
// Dotstar LED (single).

static Adafruit_DotStar* get_dotstar_upvalue(lua_State *L) {
    if (!lua_islightuserdata(L, lua_upvalueindex(1))) {
        luaL_error(L, "BUG: upvalue 1 not an Adafruit_DotStar light userdata");
        return 0; // luaL_error doesn't return
    }
    Adafruit_DotStar* led = (Adafruit_DotStar*) lua_topointer(L, lua_upvalueindex(1));
    if (!led) {
        luaL_error(L, "BUG: upvalue is null");
        return 0; // luaL_error doesn't return
    }
    return led;
}

static int lf_dotstar_set_brightness(lua_State *L) {
    int x = 256 * luaL_checknumber(L, 1);
    if (x < 0) x = 0;
    else if (x > 255) x = 255;
    get_dotstar_upvalue(L)->setBrightness(x);
    return 0;
}

static int lf_dotstar_set_brightness_show(lua_State *L) {
    int r = lf_dotstar_set_brightness(L);
    get_dotstar_upvalue(L)->show();
    return r;
}

static int lf_dotstar_set_color(lua_State *L) {
    uint32_t x = luaL_checkinteger(L, 1);
    get_dotstar_upvalue(L)->setPixelColor(0, x);
    return 0;
}

static int lf_dotstar_set_color_show(lua_State *L) {
    int r = lf_dotstar_set_color(L);
    get_dotstar_upvalue(L)->show();
    return r;
}

static int lf_dotstar_set_hsv(lua_State *L) {
    uint16_t hue = luaL_checkinteger(L, 1);
    int ok;
    uint8_t sat = lua_tointegerx(L, 2, &ok);
    if (!ok) sat = 255;
    uint8_t val = lua_tointegerx(L, 3, &ok);
    if (!ok) val = 255;
    uint32_t rgb = Adafruit_DotStar::ColorHSV(hue, sat, val);
    get_dotstar_upvalue(L)->setPixelColor(0, rgb);
    return 0;
}

static int lf_dotstar_set_hsv_show(lua_State *L) {
    int r = lf_dotstar_set_hsv(L);
    get_dotstar_upvalue(L)->show();
    return r;
}

static int lf_dotstar_show(lua_State *L) {
    get_dotstar_upvalue(L)->show();
    return 0;
}

void luatt_setfuncs_dotstar(lua_State* L, Adafruit_DotStar* dotstar, bool implicit_show) {
    static const struct luaL_Reg dotstar_show_table[] = {
        { "set_brightness", lf_dotstar_set_brightness_show },
        { "set_color",      lf_dotstar_set_color_show },
        { "set_hsv",        lf_dotstar_set_hsv_show },
        { "show",           lf_dotstar_show },
        { 0, 0 }
    };

    static const struct luaL_Reg dotstar_table[] = {
        { "set_brightness", lf_dotstar_set_brightness },
        { "set_color",      lf_dotstar_set_color },
        { "set_hsv",        lf_dotstar_set_hsv },
        { "show",           lf_dotstar_show },
        { 0, 0 }
    };

    lua_pushlightuserdata(L, dotstar);
    if (implicit_show) {
        luaL_setfuncs(L, dotstar_show_table, 1);
    }
    else {
        luaL_setfuncs(L, dotstar_table, 1);
    }
}


///////////////////////////////////
// Red LED.

static int get_red_led_pin(lua_State *L) {
    if (!lua_isinteger(L, lua_upvalueindex(1))) {
        luaL_error(L, "BUG: upvalue 1 (pin) is not an integer");
        return 0; // luaL_error doesn't return
    }
    return lua_tointeger(L, lua_upvalueindex(1));
}

static bool get_red_led_active_low(lua_State *L) {
    if (!lua_isboolean(L, lua_upvalueindex(2))) {
        luaL_error(L, "BUG: upvalue 2 (active_low) is not a boolean");
        return 0; // luaL_error doesn't return
    }
    return lua_toboolean(L, lua_upvalueindex(2));
}

static int lf_set_red_led(lua_State *L) {
    int x = lua_toboolean(L, 1);
    if (get_red_led_active_low(L)) x = (x == 0);
    digitalWrite(get_red_led_pin(L), x != 0);
    return 0;
}

void luatt_setfuncs_red_led(lua_State* L, int led_pin, bool active_low) {
    lua_pushinteger(L, led_pin);
    lua_pushboolean(L, active_low != 0);
    lua_pushcclosure(L, lf_set_red_led, 2);
    lua_setfield(L, -2, "set_red_led");
}

#endif
