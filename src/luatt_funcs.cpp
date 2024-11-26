#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include <malloc.h>

#include "luatt_context.h"
#include "luatt_funcs.h"

// Wrapper functions exported to Lua.
//
// These interface our C++ APIs with Lua's calling conventions.

static uint32_t State_rollovers = 0;

static int lf_time_millis(lua_State *L) {
    static int32_t last_ms;
    int32_t ms = (int32_t) millis();
    if (ms < last_ms) State_rollovers++;
    last_ms = ms;
    lua_pushinteger(L, ms);
    return 1;
}

static int lf_time_micros(lua_State *L) {
    uint32_t us = micros();
    lua_pushinteger(L, us);
    return 1;
}

// A 'rollover' is when millis() 0x7fffffff -> 0x80000000
// i.e. it's the signed int overflow, not the unsigned overflow.
static int lf_time_rollovers(lua_State *L) {
    lua_pushinteger(L, State_rollovers);
    return 1;
}

static int lf_meminfo(lua_State *L) {
    // prints to stdout
#ifdef ARDUINO_NRF52840_ITSYBITSY
    dbgMemInfo();
#elif defined(ARDUINO_RASPBERRY_PI_PICO)
    Serial.printf("Heap used: %i\n", rp2040.getUsedHeap());
    Serial.printf("Heap free: %i\n", rp2040.getFreeHeap());
#else
    Serial.print("Error: dbgMemInfo() not supported.\n");
#endif
    return 0;
}

static int lf_get_mux_token(lua_State *L) {
    lua_pushstring(L, Serial.get_mux_token());
    return 1;
}

static int lf_set_mux_token(lua_State *L) {
    const char* token = luaL_checkstring(L, 1);
    Serial.set_mux_token(token);
    return 0;
}

void luatt_setup_funcs(lua_State* L) {
    static const struct luaL_Reg time_table[] = {
        { "millis",    lf_time_millis },
        { "micros",    lf_time_micros },
        { "rollovers", lf_time_rollovers },
        { 0, 0 }
    };
    lua_newtable(L);
    luaL_setfuncs(L, time_table, 0);
    lua_setglobal(L, "time");

    lua_pushcfunction(L, lf_meminfo);
    lua_setglobal(L, "meminfo");

    lua_pushcfunction(L, lf_get_mux_token);
    lua_setglobal(L, "get_mux_token");

    lua_pushcfunction(L, lf_set_mux_token);
    lua_setglobal(L, "set_mux_token");
}
