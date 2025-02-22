#include <Arduino.h>
#include "Adafruit_TinyUSB.h"

#include "luatt_context.h"
#include "luatt_loader.h"

Luatt_Loader::Buffer_t::Buffer_t(char* static_buf, size_t static_buf_size) {
    if (static_buf) {
        max_size = static_buf_size;
        size = static_buf_size;
        buf = static_buf;
        is_static = true;
    }
    else {
        // grows
        max_size = 24 * 1024;
        size = 1024;
        buf = (char*)malloc(size);
        is_static = false;
    }
    len = 0;
    overflow = false;
}

Luatt_Loader::Buffer_t::~Buffer_t() {
    if (!is_static) free(buf);
    buf = 0;
    size = 0;
}

int Luatt_Loader::Buffer_t::add(int ch) {
    if (overflow) {
        return -1;
    }
    if (len >= max_size) {
        Serial.printf("error|%s:%i,input buffer overflow.\n", __FILE__, __LINE__);
        overflow = true;
        return -1;
    }
    if (len == size && !is_static) {
        size_t inc = size;
        if (inc > 2048) inc = 2048;
        size += inc;
        if (size > max_size) {
            size = max_size;
        }
        char* new_buf = (char*) realloc(buf, size);
        if (new_buf == 0) {
            Serial.printf("error|%s:%i,realloc(%i) failed.\n", __FILE__, __LINE__, size);
            overflow = true;
            return -1;
        }
        buf = new_buf;
    }
    if (len == size) {
        Serial.printf("error|%s:%i,input buffer overflow2.\n", __FILE__, __LINE__);
        overflow = true;
        return -1;
    }
    buf[len++] = ch;
    return 0;
}

void Luatt_Loader::Buffer_t::reset() {
    len = 0;
    overflow = false;
}

Luatt_Loader::Luatt_Loader(char* static_buf, size_t static_buf_size)
    : Buffer(static_buf, static_buf_size)
{
    Reset_Input();
}

Luatt_Loader::~Luatt_Loader() {
}

void Luatt_Loader::Reset_Input() {
    Buffer.reset();
    Args_n = 0;
    Raw_n = 0;
    Raw_read = 0;
}

void Luatt_Loader::Run_Command() {
    if (Args_n < 2) return;

    const char* token = Buffer.buf + Args[0].off;
    Serial.set_mux_token(token);

    const char* cmd = Buffer.buf + Args[1].off;
    if      (!strcmp(cmd, "reset")) Command_Reset();
    else if (!strcmp(cmd,  "eval")) Command_Eval();
    else if (!strcmp(cmd,  "load")) Command_Load();
    else if (!strcmp(cmd,  "compile")) Command_Compile();
    else if (!strcmp(cmd,   "msg")) Command_Msg();
    else {
        // unrecognized command
        Serial.printf("error|%s:%i,bad command,%s\n", __FILE__, __LINE__, cmd);
        Serial.print("ret|fail\n");
    }
}

void Luatt_Loader::Command_Reset() {
    Lua_Reset();
    Serial.print("ret|ok\n");
    return;
}

void Luatt_Loader::Command_Eval() {
    if (Args_n != 3) {
        Serial.printf("error|%s:%i,eval requires 3 args, %i given.\n", __FILE__, __LINE__, Args_n);
        Serial.print("ret|fail\n");
        return;
    }

    int r = luaL_loadbufferx(LUA, Buffer.buf + Args[2].off, Args[2].len, "eval", "t");
    if (r != LUA_OK) {
        // lua error
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        Serial.print("ret|fail\n");
        return;
    }

    r = lua_pcall(LUA, 0, LUA_MULTRET, 0);
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        Serial.print("ret|fail\n");
        return;
    }

    int n = lua_gettop(LUA);
    if (n == 0) {
        // no results to print
    }
    else if (!lua_checkstack(LUA, 1) || lua_getglobal(LUA, "print") != LUA_TFUNCTION) {
        // can't find print function
        lua_pop(LUA, n);
    }
    else {
        // move print func to bottom of args
        lua_rotate(LUA, 1, 1);

        r = lua_pcall(LUA, n, 0, 0);
        if (r != LUA_OK) {
            // ignore erro
            lua_pop(LUA, 1);
        }
    }

    Serial.print("ret|ok\n");
}

static int Dump_I;

int dump_output(lua_State* L, const void* p, size_t sz, void* arg) {
    const char* name = (const char*)arg;
    const uint8_t* src = (const uint8_t*)p;
    while (sz > 0) {
        if (Dump_I >= 80) {
            Serial.printf("\n");
            Serial.printf("dump|%s|", name);
            Dump_I = 0;
        }
        Serial.printf("%02x", *src++);
        sz--;
        Dump_I++;
    }
    return 0;
}

void Luatt_Loader::CompileLua(const char* name, const char* lua, size_t lua_len) {
    int r = luaL_loadbufferx(LUA, lua, lua_len, name, "t");
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        Serial.print("ret|fail\n");
        return;
    }

    Serial.printf("dump|%s|", name);
    Dump_I = 0;
    lua_dump(LUA, dump_output, (void*)name, 0);
    Serial.printf("\n");

    lua_pop(LUA, 1);
    Serial.print("ret|ok\n");
    return;
}

void Luatt_Loader::LoadLua(const char* name, const char* lua, size_t lua_len) {
    int r = luaL_loadbufferx(LUA, lua, lua_len, name, "t");
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        Serial.print("ret|fail\n");
        return;
    }

    r = lua_pcall(LUA, 0, 1, 0);
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        Serial.print("ret|fail\n");
        return;
    }

    if (lua_isnil(LUA, -1)) {
        // Lua module returned nil.
        lua_pop(LUA, 1);
    }
    else {
        // Assign module result to Luatt.pkgs
        lua_getglobal(LUA, "Luatt");
        lua_getfield(LUA, -1, "pkgs");
        lua_remove(LUA, -2); // Luatt
        lua_rotate(LUA, -2, 1);
        lua_setfield(LUA, -2, name);
        lua_pop(LUA, 1);
    }
    lua_gc(LUA, LUA_GCCOLLECT);
    Serial.print("ret|ok\n");
}

void Luatt_Loader::LoadBin(const char* name, const char* bin, size_t bin_len) {
    int r = luaL_loadbufferx(LUA, bin, bin_len, name, "b");
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        Serial.print("ret|fail\n");
        return;
    }

    r = lua_pcall(LUA, 0, 1, 0);
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
        lua_pop(LUA, 1);
        Serial.print("ret|fail\n");
        return;
    }

    if (lua_isnil(LUA, -1)) {
        // Lua module returned nil.
        lua_pop(LUA, 1);
    }
    else {
        // Assign module result to Luatt.pkgs
        lua_getglobal(LUA, "Luatt");
        lua_getfield(LUA, -1, "pkgs");
        lua_remove(LUA, -2); // Luatt
        lua_rotate(LUA, -2, 1);
        lua_setfield(LUA, -2, name);
        lua_pop(LUA, 1);
    }
    lua_gc(LUA, LUA_GCCOLLECT);
    Serial.print("ret|ok\n");
}

void Luatt_Loader::Command_Load() {
    if (Args_n != 4) {
        Serial.printf("error|%s:%i,load requires 4 args, %i given.\n", __FILE__, __LINE__, Args_n);
        Serial.print("ret|fail\n");
        return;
    }
    LoadLua(Buffer.buf + Args[2].off, Buffer.buf + Args[3].off, Args[3].len);
}

void Luatt_Loader::Command_Compile() {
    if (Args_n != 4) {
        Serial.printf("error|%s:%i,compile requires 4 args, %i given.\n", __FILE__, __LINE__, Args_n);
        Serial.print("ret|fail\n");
        return;
    }
    CompileLua(Buffer.buf + Args[2].off, Buffer.buf + Args[3].off, Args[3].len);
}

void Luatt_Loader::Command_Msg() {
    if (Args_n != 4) {
        Serial.printf("error|%s:%i,msg requires 4 args, %i given.\n", __FILE__, __LINE__, Args_n);
        Serial.print("ret|fail\n");
        return;
    }

    // Lua function MQ.OnMessage(topic, payload)
    int r = lua_getfield(LUA, LUA_REGISTRYINDEX, "luatt_on_msg");
    if (r != LUA_TFUNCTION) {
        lua_pop(LUA, 1);
        return;
    }

    // topic
    lua_pushlstring(LUA, Buffer.buf + Args[2].off, Args[2].len);

    // payload
    lua_pushlstring(LUA, Buffer.buf + Args[3].off, Args[3].len);

    r = lua_pcall(LUA, 2, 0, 0);
    if (r != LUA_OK) {
        const char* err_str = lua_tostring(LUA, lua_gettop(LUA));
        Serial.printf("error|%s:%i,%i,%s\n", __FILE__, __LINE__, r, err_str);
    }
}

int Luatt_Loader::Parse_Line()
{
    Args_n = 0;
    Raw_n = 0;
    bool final_empty_arg = false;

    size_t p = 0;
    int i = 0;
    while (p < Buffer.len) {
        if (i >= LUATT_MAX_ARGS) {
            Serial.printf("error|%s:%i,too many args, limit %i.\n", __FILE__, __LINE__, LUATT_MAX_ARGS);
            return -1;
        }
        char* s = Buffer.buf + p;
        size_t n = Buffer.len - p;
        char* next = (char*) memchr(s, '|', n);
        if (next) {
            *next = 0;
            Args[i].off = p;
            Args[i].len = next - s;
            p = (next - Buffer.buf) + 1;
            if (p == Buffer.len) {
                // Tricky edge case: last character in line was '|'
                // This means there is a zero-length last arg, but the
                // while loop is going to exit.
                final_empty_arg = true;
            }
        }
        else {
            // last arg goes to end of line
            Args[i].off = p;
            Args[i].len = n;
            p = Buffer.len;
        }

        if (Args[i].len >= 2 && s[0] == '&') {
            char* end = s + 1;
            unsigned long bytes = strtoul(end, &end, 10);
            if (*end || bytes >= Buffer.max_size) {
                Serial.printf("error|%s:%i,invalid raw byte count '%s'\n", __FILE__, __LINE__, s);
                return -1;
            }
            Raw[Raw_n].arg_i = i;
            Raw[Raw_n].bytes = bytes;
            Raw_n++;
        }
        i++;
    }
    if (final_empty_arg) {
        if (i >= LUATT_MAX_ARGS) {
            Serial.printf("error|%s:%i,too many args, limit %i.\n", __FILE__, __LINE__, LUATT_MAX_ARGS);
            return -1;
        }
        Args[i].off = Buffer.len;
        Args[i].len = 0;
        i++;
    }
    Args_n = i;
    Raw_i = 0;
    Raw_read = 0;
    return 0;
}

void Luatt_Loader::Feed_Char(int ch)
{
    if (Buffer.overflow) {
        if (ch == '\n') {
            Reset_Input();
        }
        return;
    }

    if (Buffer.add(ch) < 0) {
        return;
    }

    if (Raw_n == 0) {
        // first line
        if (ch == '\n') {
            // strip newline
            Buffer.buf[--Buffer.len] = 0;
            if (Parse_Line() < 0) {
                Reset_Input();
                return;
            }
            if (Args_n > 0 && Raw_n == 0) {
                Run_Command();
                Reset_Input();
                return;
            }
        }
    }
    else {
        struct Raw& r = Raw[Raw_i];
        Raw_read++;
        if (Raw_read == r.bytes + 1) {
            if (ch != '\n') {
                Serial.printf("error|%s:%i,expected newline after raw block.\n", __FILE__, __LINE__);
                Buffer.overflow = true;
                return;
            }
            // strip newline
            Buffer.buf[--Buffer.len] = 0;

            Args[r.arg_i].off = Buffer.len - r.bytes;
            Args[r.arg_i].len = r.bytes;

            Raw_read = 0;

            if (++Raw_i == Raw_n) {
                Run_Command();
                Reset_Input();
            }
        }
    }
}

int Luatt_Loader::Loop()
{
    int ms = 50;
    if (!connected) {
        if (Serial) {
            delay(10);
            //digitalWrite(3, 1);
            connected = true;
            Reset_Input();
            printf("version|luatt,0.0.1\n");
            ms = 0;
        }
    }
    else if (!Serial) {
        //digitalWrite(3, 0);
        connected = false;
    }
    else while (Serial.available()) {
        int ch = Serial.read();
        if (ch == EOF) break;
        Feed_Char(ch);
        ms = 0;
    }
    return ms;
}
