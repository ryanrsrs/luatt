#!/usr/bin/env python3


# lib/luatt/file_pack.py --h=include/packed_files.h --cpp=src/packed_files.cpp $(<Loader.cmd)

import os
import re
import string
import sys

# Escapes[ascii byte] = escape_sequence_string
# i.e. Escapes[10] = '\\n'
Escapes = {}

pat_not_name = re.compile('[^a-zA-Z0-9]+')
def path_to_c_name(path):
    name = os.path.split(path)[1]
    #name = os.path.splitext(name)[0]
    c_name = pat_not_name.sub('_', name)
    return c_name

def Fill_Escapes():
    global Escapes
    plain = string.ascii_letters + string.digits + " !#$%&()*+,-./:;<=>@[]^_`{|}~."
    for ch in plain:
        Escapes[ord(ch)] = ch
    Escapes[ord('"')] = '\\"'
    Escapes[ord("'")] = "\\'"
    Escapes[ord('?')] = '\\?'
    Escapes[ord('\\')] = '\\\\'
    Escapes[ord('\a')] = '\\a'
    Escapes[ord('\b')] = '\\b'
    Escapes[ord('\f')] = '\\f'
    Escapes[ord('\n')] = '\\n'
    Escapes[ord('\r')] = '\\r'
    Escapes[ord('\t')] = '\\t'
    Escapes[ord('\v')] = '\\v'

Fill_Escapes()

def escaped_chars(path):
    F = open(path, 'rb')
    for b in F.read():
        if b in Escapes:
            yield Escapes[b]
        else:
            yield f'\\{b:03o}'

def escape_string(s):
    out = []
    for ch in s:
        b = ord(ch)
        if b in Escapes:
            out.append(Escapes[b])
        else:
            out.append(f'\\x{b:02x}')
    return ''.join(out)

def match_prefix(s, prefix):
    n = len(prefix)
    if s[:n] == prefix:
        return s[n:]
    return None

OutputHeader = sys.stdout
OutputSource = sys.stdout

Files = []
for arg in sys.argv[1:]:
    m = match_prefix(arg, '--h=')
    if m:
        OutputHeader = open(m, 'wt')
        continue
    m = match_prefix(arg, '--cpp=')
    if m:
        OutputSource = open(m, 'wt')
        continue

    if not os.path.isfile(arg):
        print(f"Can't find file {arg}.")
        sys.exit(2)

    f = {}
    f['path'] = arg
    f['name'] = os.path.splitext(os.path.split(arg)[1])[0]
    f['c_name'] = path_to_c_name(arg)
    f['size'] = os.path.getsize(arg)
    Files.append(f)

def emit_header(out):
    out.write('#ifndef PACKED_FILES_H\n')
    out.write('#define PACKED_FILES_H\n')
    out.write('\n')
    out.write('#include <stddef.h>\n')
    out.write('\n')
    out.write('#ifdef __cplusplus\n')
    out.write('extern "C" {\n')
    out.write('#endif\n')
    out.write('\n')
    out.write('struct Packed_File_t {\n')
    out.write('    const char* path;\n')
    out.write('    const char* name;\n')
    out.write('    size_t size;\n')
    out.write('    const char* data;\n')
    out.write('};\n')
    out.write('\n')
    for f in Files:
        out.write('extern const struct Packed_File_t File_' + f['c_name'] + ';\n')
    out.write('\n')
    out.write('#ifdef __cplusplus\n')
    out.write('}\n')
    out.write('#endif\n')
    out.write('#endif\n')
    out.write('\n')

def emit_source(out):
    out.write('#include "packed_files.h"\n')
    out.write('\n')
    for f in Files:
        out.write('const struct Packed_File_t File_' + f['c_name'] + ' = {\n')
        out.write('    "' + escape_string(f['path']) + '", /* path */\n')
        out.write('    "' + escape_string(f['name']) + '", /* name */\n')
        out.write(f'    {f['size']}, /* size */\n')

        line = [ '    "' ]
        line_len = len(line[0])
        for s in escaped_chars(f['path']):
            line.append(s)
            line_len += len(s)
            if line_len >= 72:
                line = ''.join(line)
                out.write(f'{line}"\n')
                line = [ '    "' ]
                line_len = len(line[0])
        line = ''.join(line)
        out.write(f'{line}"\n')
        out.write('};\n\n')

emit_header(OutputHeader)
emit_source(OutputSource)