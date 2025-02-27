#define _GNU_SOURCE
// for strlcpy

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "luatt_osal.h"

static struct Internal_t {
    char mux_token[52];
    size_t mux_token_len;
    bool mux_newline;

    char new_token[50];
    bool use_new_token;
} Internal = { "", 0, true, "", false };

void luatt_set_mux_token(const char* token) {
    if (token == 0) token = "";
    strlcpy(Internal.new_token, token, sizeof(Internal.new_token));
    Internal.use_new_token = true;
}

const char* luatt_get_mux_token() {
    return Internal.new_token;
}

int luatt_print(const char* s) {
    return luatt_write(s, strlen(s));
}

int luatt_printf(const char* fmt, ...) {
   static char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    va_end(ap);
    return luatt_write(buf, n);
}

int luatt_write(const char* buf, size_t bytes) {
    if (bytes == 0) return 0;

    if (Internal.use_new_token) {
        // mux token was changed since the last print

        // make sure it's really a different token string (could have
        // been changed, then changed back without an intervening print)
        size_t new_token_len = strlen(Internal.new_token);
        // mux_token has trailing '|', new_token does not
        if (Internal.mux_token_len != new_token_len + 1 || strncmp(Internal.new_token, Internal.mux_token, new_token_len)) {
            if (!Internal.mux_newline) {
                // previous print didn't end with a newline, so
                // force a newline before changing the mux token.
                luatt_raw_write("\n", 1);
                Internal.mux_newline = true;
            }
            // add the trailing '|'
            if (new_token_len == 0) Internal.mux_token[0] = 0;
            else snprintf(Internal.mux_token, sizeof(Internal.mux_token), "%s|", Internal.new_token);
            Internal.mux_token_len = strlen(Internal.mux_token);
        }
        Internal.use_new_token = false;
    }

    size_t remain = bytes;
    while (remain) {
        if (Internal.mux_newline) {
            // start of new line, so write out token first
            if (Internal.mux_token_len) {
                luatt_raw_write(Internal.mux_token, Internal.mux_token_len);
            }
            Internal.mux_newline = false;
        }
        // print only up to next newline. after the newline we'll need to print
        // the token again.
        const char* nl = (const char*) memchr(buf, '\n', remain);
        ssize_t chunk = nl ? nl - buf + 1 : remain; // output includes the '\n'
        luatt_raw_write(buf, chunk);
        if (nl) Internal.mux_newline = true;
        remain -= chunk;
        buf += chunk;
  }

  return bytes - remain;
}
