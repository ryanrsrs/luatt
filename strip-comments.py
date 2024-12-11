#!/usr/bin/env python3

import re
import sys

not_newlines = re.compile('[^\\n]+')

def only_newlines(m):
    if m.group(1):
        # a long comment
        return not_newlines.sub('', m.group()) or ' '
    if m.group(3):
        # a short comment
        return not_newlines.sub('', m.group())
    if m.group(4) or m.group(5):
        # whitespace at start/end of line
        return ''
    if m.group(6):
        # whitespace mid-line
        return ' '
    else:
        # it's a string literal
        return m.group()

lua_comments = re.compile(
    "'(?:\\\\.|[^'])*'" +
    '|"(?:\\\\.|[^"])*"' +
    '|[ \\t]*(--)?\\[(=*)\\[(?s:.)*?\\]\\2\\][ \\t]*' +
    '|[ \\t]*(--)(?:\\[=*)?(?:[^[=\\n].*)?$' +
    '|^([ \\t]+)' +
    '|([ \\t]+)$' +
    '|([ \\t]{2,})',
    re.MULTILINE)

def strip_lua_comments(lua_code):
    return lua_comments.sub(only_newlines, lua_code)

sys.stdout.write(
    strip_lua_comments(sys.stdin.read())
)
