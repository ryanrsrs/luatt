#ifndef LUATT_FUNCS_H
#define LUATT_FUNCS_H

// Register Lua functions for some Arduino APIs.

struct lua_State;

void luatt_setfuncs(lua_State* L);

#endif
