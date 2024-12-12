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

static int lf_time_uptime(lua_State* L) {
    int ms = millis();
    uint64_t secs = State_rollovers;
    secs <<= 32;
    secs += ms;
    secs /= 1000;
    lua_pushinteger(L, (int)secs);
    return 1;
}

static int lf_time_delay(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);
    delay(ms);
    return 0;
}

static int lf_time_yield(lua_State* L) {
    yield();
    return 0;
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

static int lf_print_hex(struct lua_State* L) {
    size_t len;
    const char* data = luaL_checklstring(L, 1, &len);
    size_t i;
    for (i = 0; i < len; i++) {
        if (i == 0) {
            // pass
        }
        else if ((i & 15) == 0) {
            Serial.print("\n");
        }
        else if ((i & 3) == 0) {
            Serial.print(" ");
        }
        Serial.printf("%02x", data[i]);
    }
    Serial.print("\n");
    return 0;
}

static int lf_set_cb_sched_loop(struct lua_State* L) {
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "sched_loop callback must be a function");
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "luatt_sched_loop");
    return 0;
}

static int lf_set_cb_on_msg(struct lua_State* L) {
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "on_msg callback must be a function");
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "luatt_on_msg");
    return 0;
}

void luatt_setfuncs(lua_State* L) {
    // Luatt root table
    lua_getfield(L, LUA_REGISTRYINDEX, "luatt_root");

    static const struct luaL_Reg luatt_table[] = {
        { "set_cb_sched_loop", lf_set_cb_sched_loop },
        { "set_cb_on_msg",     lf_set_cb_on_msg },
        { "get_mux_token",  lf_get_mux_token },
        { "set_mux_token",  lf_set_mux_token },
        { 0, 0 }
    };
    luaL_setfuncs(L, luatt_table, 0);

    // Luatt.time
    lua_newtable(L);
    static const struct luaL_Reg time_table[] = {
        { "millis",    lf_time_millis },
        { "micros",    lf_time_micros },
        { "rollovers", lf_time_rollovers },
        { "uptime",    lf_time_uptime },
        { "delay",     lf_time_delay },
        { "yield",     lf_time_yield },
        { 0, 0 }
    };
    luaL_setfuncs(L, time_table, 0);
    lua_setfield(L, -2, "time");

    lua_pop(L, 1);


    lua_pushcfunction(L, lf_meminfo);
    lua_setglobal(L, "meminfo");

    lua_pushcfunction(L, lf_print_hex);
    lua_setglobal(L, "print_hex");
}
