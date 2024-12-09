#include <Arduino.h>
#include "Adafruit_TinyUSB.h"

#include "luatt_context.h"
#include "luatt_funcs.h"

struct lua_State* LUA = 0;

static luatt_setup_callback State_setup_cb;

void Lua_Begin(luatt_setup_callback setup_cb) {
    State_setup_cb = setup_cb;
}

void Lua_Reset() {
    if (LUA) {
        lua_close(LUA);
    }
    LUA = luaL_newstate();

    lua_State* L = LUA;
    luaL_openlibs(L);

    // global Luatt table
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "luatt_root");

    // Luatt.pkgs
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "luatt_pkgs");
    lua_setfield(L, -2, "pkgs");

    // Luatt.periphs
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "luatt_periphs");
    lua_setfield(L, -2, "periphs");

    // Luatt.dbg
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "luatt_dbg");
    lua_setfield(L, -2, "dbg");

    lua_setglobal(L, "Luatt");

    luatt_setfuncs(L);

    if (State_setup_cb) State_setup_cb(L);
}

int Lua_Loop(uint32_t interrupt_flags) {
    int max_sleep = 5000;
    if (!LUA) return max_sleep;

    Serial.set_mux_token("sched");

    // Lua function scheduler.loop
    int r = lua_getfield(LUA, LUA_REGISTRYINDEX, "luatt_sched_loop");
    if (r != LUA_TFUNCTION) {
        lua_pop(LUA, 1);
        return max_sleep;
    }

    lua_pushinteger(LUA, interrupt_flags);

    r = lua_pcall(LUA, 1, 1, 0);
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        return max_sleep;
    }
    if (lua_gettop(LUA) > 0) {
        uint32_t ms = lua_tointeger(LUA, -1);
        lua_pop(LUA, 1);
        return ms;
    }
    return max_sleep;
}
