#!/usr/bin/env python3

# Luatt CLI
#
# Loads Lua files onto the microcontroller over USB.
# Provides a Lua REPL running on the microcontroller.
#
# Usage:
#   Connect to the microcontroller via USB. Listen on a unix socket for
#   downstream luatt.py processes.
#
#   $ python3 luatt.py /dev/cu.usbserial --mqtt=192.168.1.1:1883
#
#
#   Connect to another luatt.py process using a unix domain socket. The
#   upstream process will forward our commands to the microcontroller.
#
#   $ python3 luatt.py /tmp/luatt.cu.usbserial
#
#   The idea between these two modes is that usually luatt.py will load Lua
#   files onto the micro, connect the MQTT broker, and do some logging, but
#   generally not show a REPL.
#
#   Later if you decide you do need to poke around with the REPL, you can
#   start a downstream luatt.py process to do it. That way you don't have
#   to kill and restart the main luatt.py (e.g. to enable the REPL).
#
#
#
# Options:
#   --mqtt=ipaddr:port
#       Connect to MQTT broker and act as a proxy so the microcontroller
#       can publish and subscribe. Only valid if connected directly to the
#          microcontroller (i.e. not from a downstream luatt.py process).
#
#   -r  Reset microcontroller Lua state. Does not do a hardware reset, but
#       does set hardware peripherals to their initial state. Clears all
#       Lua variables, objects, frees memory, etc.
#
#   filename.lua    Load Lua file onto microcontroller and run it.
#
#   Loader.cmd      Text file with a list of .lua files to load. Loads
#                   and runs the lua files in the order given.
#
#   archive.luaz    Zip file with a Loader.cmd and some .lua files inside.
#
#
# The REPL:
#   lua> print(2+3*4)
#   14
#   lua> a = 55
#   lua> print(a*10)
#   550
#
# REPL meta commands, they work like the command line options.
#   !reset
#   !load file.lua


import ctypes
import datetime
import errno
import fcntl
import json
import logging
import logging.handlers
import math
import os
import queue
import re
import readline
import secrets
import select
import shlex
import signal
import socket
import socketserver
import stat
import sys
import termios
import threading
import time
import zipfile

# Use systemd for service management and logging, if available.
try:
    import systemd
    import systemd.daemon
    import systemd.journal
except ImportError:
    # no systemd on Mac OS or OpenWrt
    systemd = None

try:
    import paho.mqtt.client as paho_client
except ImportError:
    # paho mqtt not installed
    paho_client = None

LogName = f'luatt.{os.getpid()}.log'
LogDir = '/tmp'
LogPath = os.path.join(LogDir, LogName)
logger = logging.getLogger(LogName)
logger.setLevel(logging.DEBUG)

Enable_REPL = sys.stdout.isatty()

Subscriptions = set()

QS = {}
ReplQ = queue.Queue(20)

Downstreams = {}

Quit = False
Force_Update = False

Conn = {}

Cleanup_Unlink = []

class REPLOutput:
    def write(msg):
        if Enable_REPL:
            sys.stdout.write("\033[2K\r" + msg)
            if Force_Update: readline.forced_update()
        else:
            sys.stdout.write(msg)

    def flush():
        sys.stdout.flush()

def format_iso_time(record, datefmt=None):
    dt = datetime.datetime.fromtimestamp(record.created)
    return dt.astimezone().isoformat(timespec='microseconds')

def configure_logger():
    file_handler = logging.handlers.RotatingFileHandler(LogPath, 'w', 1000*1000, 3)
    file_handler.setLevel(logging.DEBUG)
    fmt = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    fmt.formatTime = format_iso_time
    file_handler.setFormatter(fmt)
    logger.addHandler(file_handler)

    repl_handler = logging.StreamHandler(REPLOutput)
    repl_handler.setLevel(logging.WARNING)
    logger.addHandler(repl_handler)

    if systemd:
        logger.addHandler(systemd.journal.JournalHandler(SYSLOG_IDENTIFIER=LogName))


# Add API readline.forced_update()
#
# We need this to scroll the output while the user is typing at the prompt.
# New output is inserted above the line you're editing, keeping it from
# getting mixed up.
def patch_readline():
    version = readline._READLINE_LIBRARY_VERSION
    if 'EditLine' in version:
        # libedit on MacOS
        logger.debug("Using C function libedit:rl_forced_update_display()")
        lib = ctypes.cdll.LoadLibrary('libedit.3.dylib')
    else:
        logger.debug("Using C function libreadline:rl_forced_update_display()")
        lib = ctypes.cdll.LoadLibrary('libreadline.so.' + version)
    readline.forced_update = lib.rl_forced_update_display

def bytes_or_encode(str):
    if type(str) == bytes:
        return str
    return str.encode('utf-8')

def decode_or_repr(bstr):
    try:
        return bstr.decode('utf-8')
    except UnicodeDecodeError:
        return repr(bstr)

def coerce_string(bstr):
    if type(bstr) == bytes:
        try:
            return bstr.decode('utf-8')
        except UnicodeDecodeError:
            return repr(bstr)[2:-1]
    return bstr

# Open USB serial connection to microcontroller.
# Use canonical mode to read line-at-a-time from the micro,
# not raw mode.
def open_serial(path):
    logger.info("Opening serial port %s", path)
    try:
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    except OSError as e:
        logger.error("open(%s) failed: %s", path, e.strerror)
        return None

    logger.debug("Setting serial port termios")

    # [iflag, oflag, cflag, lflag, ispeed, ospeed, cc]
    tt = termios.tcgetattr(fd)

    tt[0] = termios.IGNBRK | termios.IGNPAR
    tt[1] = 0
    tt[2] = termios.CS8 | termios.CREAD | termios.CLOCAL | termios.HUPCL
    tt[3] = termios.ICANON
    tt[4] = termios.B9600
    tt[5] = termios.B9600
    for i in range(termios.VEOL2+1):
        tt[6][i] = b'\0'

    termios.tcflush(fd, termios.TCIOFLUSH)
    termios.tcsetattr(fd, termios.TCSANOW, tt)
    return fd

# Open socket connection to upstream luatt.py process.
def open_socket(path):
    logger.info("Connecting to upstream socket %s", path)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(path)
    except OSError as e:
        logger.error("connect(%s) failed: %s", path, e.strerror)
        sock.close()
        return None
    return sock


def resubscribe_topics(client):
    for topic in Subscriptions:
        logger.info("mqtt resubscribe: %s", topic)
        client.subscribe(topic)

# The callback for when the client receives a CONNACK response from the server.
def on_connect_v2(client, userdata, flags, reason_code, properties):
    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    if reason_code.is_failure:
        logger.error("mqtt connect_v2 failed: %s", reason_code.getName())
        return
    logger.info("mqtt connect_v2 success: %s", reason_code.getName())
    resubscribe_topics(client)

def on_connect_v1(client, userdata, flags, rc):
    if rc:
        logger.error("mqtt connect_v1 failed: rc=%d", rc)
        return
    logger.info("mqtt connect_v1 success: rc=%d", rc)
    resubscribe_topics(client)


# The callback for when a message is received from the server.
def on_message(client, userdata, msg):
    logger.info("mqtt message: %s %s", msg.topic, str(msg.payload))
    write_command(Conn['fd'], "noret", "msg", msg.topic, msg.payload)

# Open device and determine if we're talking to a USB serial device or
# a unix socket.
def open_conn(path):
    Conn['path'] = path
    try:
        mode = os.stat(path).st_mode
    except OSError as e:
        logger.error("stat(%s) failed: %s", path, e.strerror)
        logger.error("Cannot open device or socket.")
        return False
    if stat.S_ISCHR(mode):
        Conn['fd'] = open_serial(path)
        Conn['is_socket'] = False
    elif stat.S_ISSOCK(mode):
        Conn['sock'] = open_socket(path)
        Conn['fd'] = Conn['sock'].fileno()
        Conn['is_socket'] = True
    return True

def setup_paho_client():
    global paho_client
    if not hasattr(paho_client, 'CallbackAPIVersion'):
        # paho-mqtt 1.x, default on OpenWrt
        paho_client = paho_client.Client()
        paho_client.on_connect = on_connect_v1
        paho_client.on_message = on_message
    else:
        # paho-mqtt 2+, preferred
        version = paho_client.CallbackAPIVersion.VERSION2
        paho_client = paho_client.Client(version)
        paho_client.on_connect = on_connect_v2
        paho_client.on_message = on_message


# We use a mostly-text protocol to talk to the microcontroller.
#
# The message format is a variable number of fields separated by '|'
# and terminated with a newline.
#   "field1|field2|field3\n"
#
# If a particular field contains control characters, newlines, '|',
# or starts with '&', then it cannot be included directly. Instead,
# those fields are encoded as "&NNN" where NNN is the size in bytes
# of the raw field. Each raw field is then be appended to the command,
# each followed by a newline.
#
# Example, encode the following 4 fields:
#   "Hello World!"
#   "123"
#   "bad\tchar"
#   "embedded\n123"
#
# Result:
#   "Hello World!|123|&8|&12\nbad\tchar\nembedded\n123\n"
#
# Encoding rationale:
# - Most short bits of text and json won't need to be escaped.
# - Operate the USB serial port in line mode.
# - Simple to handle both on the microcontroller and in python.

# Check if a field needs to be escaped.
def is_clean(b):
    if not b: return True
    if b[0] == b'&'[0]: return False
    for ch in b:
        if ch < 32 or ch > 126 or ch == b'|'[0]:
            return False
    return True

# Escape field only if needed.
# Returns (field, trailer)
#   "123"  => (b"123", None)
#   "1\t3" => (b"&3", b"1\t3")
def escape_arg(bstr):
    if type(bstr) == str:
        bstr = bstr.encode('utf-8')
    if is_clean(bstr):
        return (bstr, None)
    else:
        return (f'&{len(bstr)}'.encode('utf-8'), bstr)

# Each command sent to the microcontroller has a random token.
# When the microcontroller sends a reply, it will include the token.
# This tells luatt.py how to route the reply, for example, some
# replies will need to be forwarded to a downstream luatt.py over
# the unix sock, if that's where the request originated.
#
# Asynchronous output from background Lua threads is sent with the
# token "sched".
def new_token():
    return f"{os.getppid()}/{os.getpid()}/{secrets.token_hex(12)}"

# Encodes a command and sends it upstream, either to the microcontroller,
# or an upstream luatt.py process.
def write_command(fd, token, *args):
    out = [[bytes_or_encode(token)]]
    for arg, raw in map(escape_arg, args):
        out[0].append(arg)
        if raw is not None: out.append(raw)
    line = b'|'.join(out[0])
    out[0] = line
    out.append(b'')
    out = b'\n'.join(out)

    if Conn['fd'] == fd:
        if Conn['is_socket']:
            label = "upstream.write"
        else:
            label = "serial.write"
    else:
        label = "downstream.write"
    logger.debug("%s: %s", label, repr(line))

    # atomic write
    os.write(fd, out)
    return token

# Read input from the microcontroller or socket, and
# splits it into lines.
#
# fd        File descriptor to read.
# partial   Buffered input that has already been read.
#
# Returns (one_full_line, next_partial)
# Pass next_partial on the next call to read_packet_line or
# read_packet_bytes.
def read_packet_line(fd, partial):
    if b'\n' in partial:
        # we can satisfy request entirely from buffered input
        return partial.split(b'\n', 1)
    line = [partial]
    while not Quit:
        rd, wr, ex = select.select([fd], [], [], 1.0)
        if fd not in rd: continue
        buf = os.read(fd, 4096)
        if not buf: 
            logger.debug("read_packet_line: os.read() returned empty")
            break
        parts = buf.split(b'\n', 1)
        line.append(parts[0])
        if len(parts) == 1: continue
        return (b''.join(line), parts[1])
    # quitting...
    return None

# Read a fixed number of bytes from a file descriptor, followed by newline.
def read_packet_bytes(fd, n, partial):
    if len(partial) >= n + 1:
        # skip newline
        return (partial[:n], partial[n+1:])
    line = [partial]
    n -= len(partial)
    while not Quit and n >= 0:
        rd, wr, ex = select.select([fd], [], [], 1.0)
        if fd not in rd: continue
        buf = os.read(fd, 4096)
        if not buf:
            logger.debug("read_packet_bytes: os.read() returned empty")
            return None
        line.append(buf)
        n -= len(buf)
    if Quit:
        return None
    line = b''.join(line)
    return (line[:n], line[n:][1:]) # skip newline

# Microcontroller publishes MQTT message.
def dev_cmd_pub(cmd):
    if len(cmd) != 4:
        logger.error("mqtt pub: 4 args required, %d given", len(cmd))
        return
    topic = coerce_string(cmd[2])
    payload = cmd[3]
    logger.info("mqtt pub: %s %s", topic, payload)
    if paho_client is None:
        logger.error("mqtt pub: paho.mqtt not installed")
    else:
        paho_client.publish(topic, payload)

# Microcontroller subscribes to MQTT topic.
def dev_cmd_sub(cmd):
    if len(cmd) != 3:
        logger.error("mqtt sub: 3 args required, %d given", len(cmd))
        return
    topic = coerce_string(cmd[2])
    logger.info("mqtt sub: %s", topic)
    Subscriptions.add(topic)
    if paho_client is None:
        logger.error("mqtt sub: paho.mqtt not installed")
    else:
        paho_client.subscribe(topic)

# Microcontroller unsubscribes.
def dev_cmd_unsub(cmd):
    if len(cmd) != 3:
        logger.error("mqtt unsub: 3 args required, %d given", len(cmd))
        return
    topic = coerce_string(cmd[2])
    logger.info("mqtt unsub: %s", topic)
    if paho_client is None:
        logger.error("mqtt unsub: paho.mqtt not installed")
    if topic == '*':
        for topic in Subscriptions:
            if paho_client: paho_client.unsubscribe(topic)
        Subscriptions.clear()
    else:
        if paho_client: paho_client.unsubscribe(topic)
        Subscriptions.discard(topic)

# Parse a command from the microcontroller.
def process_serial_packet(packet):
    packet = tuple(packet)
    logger.debug("packet: %s", repr(packet))
    if len(packet) < 2:
        # mostly log text output
        return
    token = coerce_string(packet[0])
    cmd = coerce_string(packet[1])

    # MQTT commands
    if cmd == 'pub':
        dev_cmd_pub(packet)
        return
    elif cmd == 'sub':
        dev_cmd_sub(packet)
        return
    elif cmd == 'unsub':
        dev_cmd_unsub(packet)
        return
    else:
        # check if token is ours
        q = QS.get(token)
        if q is not None:
            q.put(packet)
        #elif token.split('/')[0] in (str(os.getppid()), 'sched'):
        else:
            body = map(decode_or_repr, packet[1:])
            body = '|'.join(body)
            logger.info("%s: %s", token, body)
            if Enable_REPL:
                sys.stdout.write(f"\033[2K\r{body}\n")
                if Force_Update: readline.forced_update()
            else:
                sys.stdout.write(body + "\n")

    # broadcast to downstream luatt.py instances
    for ds in Downstreams:
        write_command(Downstreams[ds], *packet)

    return

# Read one packet from file descriptor.
def read_packet(fd, partial):
    try:
        v = read_packet_line(fd, partial)
        if v is None:
            logger.debug("read_packet: read_packet_line() returned None")
            return
    except OSError as e:
        logger.debug("read_packet OSError: %s", e.strerror)
        # don't log if we closed the fd during shutdown
        if not Quit or e.errno != errno.EBADF:
            logger.error("read_packet OSError: %s", e.strerror)
        return

    line, partial = v

    #logger.debug(coerce_string(line))

    fields = []
    for f in line.split(b'|'):
        if f[:1] == b'&':
            n = int(f[1:])
            v = read_packet_bytes(fd, n, partial)
            if v is None:
                logger.debug("read_packet: read_packet_bytes() returned None")
                return
            fields.append(v[0])
            partial = v[1]
        else:
            fields.append(f)
    return (fields, partial)

MainThread = threading.get_ident()

# Thread to read packets from the serial port or unix socket.
def read_serial():
    global Quit, Enable_REPL
    partial = b''
    while not Quit:
        v = read_packet(Conn['fd'], partial)
        if not v:
            logger.info("read_serial() quitting")
            Quit = True
            signal.pthread_kill(MainThread, signal.SIGINT)
            break

        packet, partial = v
        process_serial_packet(packet)

# Main thread calls this to wait for a reply after sending a command.
# Blocks until the read_serial() thread routes the reply to our queue.
def wait_for_ret(q, token):
    while not Quit:
        v = q.get()
        body = b'|'.join(v[1:])
        if coerce_string(v[0]) == token:
            sys.stdout.write(coerce_string(body) + "\n")
            if v[1] == b'ret':
                return v
    return None

# Main thread calls this after first connecting. We expect the
# microcontroller to send a version line automatically.
def wait_for_version(q):
    while not Quit:
        try:
            v = q.get(True, 10)
        except queue.Empty:
            # timeout
            sys.exit(3)
        if v[0] == b'sched' and v[1] == b'version':
            logger.info(coerce_string(b'|'.join(v)))
            return v
    return None


def create_log_symlink():
    # only setup symlink for primary luatt.py
    symlink_path = os.path.join(LogDir, 'luatt.log')
    if os.path.islink(symlink_path):
        os.unlink(symlink_path)
    if not os.path.exists(symlink_path):
        os.symlink(LogName, symlink_path)
        #Cleanup_Unlink.append(symlink_path)


# Socket server to handle unix domain connections from downstream luatt.py processes.
# Each downstream connected luatt.py gets a new thread to babysit it.
class SocketHandler(socketserver.BaseRequestHandler):
    def handle(self):
        last_token = None
        partial = b''
        while not Quit:
            v = read_packet(self.request.fileno(), partial)
            if not v: break
            packet, partial = v

            if last_token:
                del Downstreams[last_token]
                last_token = None
            token = coerce_string(packet[0])
            if token != '' and token != 'noret':
                Downstreams[token] = self.request.fileno()
                last_token = token
            write_command(Conn['fd'], *packet)
        if last_token:
            del Downstreams[last_token]

def create_socket_and_symlink():
    # Create unix socket for downstream luatt.py processes to connect to.
    # Only do this if we're talking directly to the USB serial device.
    #
    # Unix socket paths
    #
    # 508 ~/usb-io/lua$ ls -l /dev/cu.usbmodemFD114301 /tmp/luatt.*
    # crw-rw-rw-  1 root  wheel  0x16000005 Oct 15 12:12 /dev/cu.usbmodemFD114301
    # srwxr-xr-x  1 ryan  wheel           0 Oct 15 09:49 /tmp/luatt.97372
    # lrwxr-xr-x  1 ryan  wheel          11 Oct 15 09:49 /tmp/luatt.cu.usbmodemFD114301 -> luatt.97372
    sock_name = f"luatt.{os.getpid()}"
    link_name = f"luatt.{os.path.basename(Conn['path'])}"
    tmp_dir = '/tmp'
    sock_path = os.path.join(tmp_dir, sock_name)
    link_path = os.path.join(tmp_dir, link_name)

    server = socketserver.ThreadingUnixStreamServer(sock_path, SocketHandler)
    Cleanup_Unlink.append(sock_path)

    if os.path.islink(link_path):
        # remove existing symlink
        os.unlink(link_path)
    if not os.path.exists(link_path):
        os.symlink(sock_name, link_path)
        Cleanup_Unlink.append(link_path)
    return server

def find_loader_cmd(z):
    subdir_loader = None
    for info in z.infolist():
        if info.filename == "Loader.cmd":
            return info
        path, base = os.path.split(info.filename)
        if base != "Loader.cmd": continue
        if '/' in path: continue # only look one dir level
        if subdir_loader:
            logger.error("%s: multiple Loader.cmd files found", z.filename)
            return None
        subdir_loader = info
    return subdir_loader

def cmd_reset():
    token = new_token()
    QS[token] = ReplQ
    write_command(Conn['fd'], token, "reset")
    wait_for_ret(ReplQ, token)
    del QS[token]

def split_lua_name(s):
    eq = s.split('=', 1)
    if len(eq) == 2 and '/' not in eq[0]:
        name = eq[0]
        file = eq[1]
    else:
        name = os.path.splitext(os.path.basename(s))[0]
        file = s
    return (name, file)

Pat_not_newlines = re.compile('[^\\n]+')

def only_newlines(m):
    if m.group(1):
        # a long comment
        return Pat_not_newlines.sub('', m.group()) or ' '
    if m.group(3):
        # a short comment
        return Pat_not_newlines.sub('', m.group())
    if m.group(4) or m.group(5):
        # whitespace at start/end of line
        return ''
    if m.group(6):
        # whitespace mid-line
        return ' '
    else:
        # it's a string literal
        return m.group()

Pat_lua_comments = re.compile(
    "'(?:\\\\.|[^'])*'" +
    '|"(?:\\\\.|[^"])*"' +
    '|[ \\t]*(--)?\\[(=*)\\[(?s:.)*?\\]\\2\\][ \\t]*' +
    '|[ \\t]*(--)(?:\\[=*)?(?:[^[=\\n].*)?$' +
    '|^([ \\t]+)' +
    '|([ \\t]+)$' +
    '|([ \\t]{2,})',
    re.MULTILINE)

def strip_lua_comments(lua_code):
    return Pat_lua_comments.sub(only_newlines, lua_code)

def load_data(name, data, compile=False):
    token = new_token()
    QS[token] = ReplQ
    n = len(data)
    data = strip_lua_comments(data)
    if compile: cmd = "compile"
    else: cmd = "load"
    write_command(Conn['fd'], token, cmd, name, data)
    wait_for_ret(ReplQ, token)
    del QS[token]

def load_luaz(path, compile=False):
    with zipfile.ZipFile(path, metadata_encoding='utf-8') as z:
        loader = find_loader_cmd(z)
        if not loader:
            logger.error("%s: Loader.cmd not found", path)
            return None
        loader_dir = os.path.split(loader.filename)[0]
        for line in z.read(loader).splitlines():
            line = line.decode('utf-8')
            if not line.strip(): continue
            name, src = split_lua_name(line)
            src_path = os.path.join(loader_dir, src)
            data = z.read(src_path).decode('utf-8')
            load_data(name, data, compile)

def load_loader_cmd(path, compile=False):
    loader_dir = os.path.split(path)[0]
    for line in open(path).readlines():
        line = line.strip()
        if not line: continue
        name, src = split_lua_name(line)
        src_path = os.path.join(loader_dir, src)
        data = open(src_path).read()
        load_data(name, data, compile)

def cmd_load(cmd, compile=False):
    if len(cmd) < 2:
        logger.error("!load: no arguments given")
        return
    for arg in cmd[1:]:
        ext = os.path.splitext(arg)[1]
        if ext == '.zip' or ext == '.luaz':
            load_luaz(arg, compile)
            continue
        elif ext == '.cmd':
            load_loader_cmd(arg, compile)
            continue

        name, path = split_lua_name(arg)
        try:
            f = open(path, 'r', encoding='utf-8')
            data = f.read(100 * 1024)
        except OSError as e:
            logger.error("%s: %s", path, e.strerror)
            logger.error("Cannot load %s", path)
            return
        load_data(name, data, compile)

def cmd_eval(line):
    token = new_token()
    QS[token] = ReplQ
    write_command(Conn['fd'], token, "eval", line)
    wait_for_ret(ReplQ, token)
    del QS[token]

def parse_line(line):
    if line[:1] != '!':
        cmd_eval(line)
        return True

    args = shlex.split(line)
    if args[0] == '!reset':
        cmd_reset()
        return True
    elif args[0] == '!exit' or args[0] == '!quit':
        return False
    elif args[0] == '!load':
        cmd_load(args)
        return True
    elif args[0] == '!compile':
        cmd_load(args, compile=True)
        return True
    elif args[0] == '!reload':
        pass
    else:
        logger.error("Bad command: %s", repr(line))
    return True

def main():
    global Quit, Force_Update
    configure_logger()
    patch_readline()
    if not open_conn(sys.argv[1]):
        logger.error("Failed to connect to %s, quitting.", sys.argv[1])
        sys.exit(5)

    if not Conn['is_socket']:
        # Only create when talking to serial device.
        create_log_symlink()
        if paho_client:
            setup_paho_client()

    th_serial = threading.Thread(target=read_serial, daemon=True)

    if not Conn['is_socket']:
        QS['sched'] = ReplQ
        th_serial.start()
        wait_for_version(ReplQ)
        del QS['sched']
        tt = time.time()
        tt_sec = math.floor(tt)
        tt_ms = math.floor(1e3 * (tt - tt_sec))
        write_command(Conn['fd'], "noret", "eval", f"Luatt.time.set_unix({tt_sec},{tt_ms})")

        Server = create_socket_and_symlink()
        Server_thread = threading.Thread(target=Server.serve_forever, daemon=True)
        Server_thread.start()
    else:
        th_serial.start()
        write_command(Conn['fd'], "noret", "reconnect", str(os.getppid()))

        Server = None
        Server_thread = None

    for arg in sys.argv[2:]:
        if arg[:7] == '--mqtt=':
            arg = arg.split("=", 1)[1]
            if Conn['is_socket']:
                logger.error("Cannot proxy MQTT from a downstream luatt.py")
                return 2

            if paho_client is None:
                logger.error("Module paho-mqtt not available.")
                return 2

            # Connect to MQTT Broker
            if ":" in arg:
                ip, port = arg.split(":", 1)
                port = int(port)
            else:
                ip, port = arg, 1883
            paho_client.connect(ip, port, 60)
            paho_client.loop_start()
            continue

        if arg == '-r':
            cmd_reset()
            continue

        if arg[:5] == 'eval:':
            cmd_eval(arg[5:])
            continue

        ext = os.path.splitext(arg)[1]
        if ext in ('.lua', '.luaz', '.zip', '.cmd'):
            cmd_load(['load', arg])
            continue

        else:
            print(f"Error: bad command line arg {repr(arg)}")

    if systemd and 'NOTIFY_SOCKET' in os.environ:
        systemd.daemon.notify('READY=1')

    try:
        if Enable_REPL:
            while not Quit:
                Force_Update = True
                s = input("lua> ")
                Force_Update = False
                if s and not parse_line(s):
                    break
        else:
            while not Quit:
                time.sleep(1)
    except EOFError:
        logger.debug("Got ctrl-D, quitting.")
        print()
        pass
    except KeyboardInterrupt:
        logger.debug("Got ctrl-C, quitting.")
        print()
        pass
    finally:
        Quit = True
        if Server:
            logger.debug("Shutting down socket server.")
            Server.shutdown()
            Server_thread.join()
        if Conn['is_socket']:
            logger.debug("Closing socket.")
            Conn['sock'].close()
        else:
            logger.debug("Closing serial fd.")
            try:
                os.close(Conn['fd'])
            except KeyboardInterrupt:
                pass
        logger.debug("Joining serial thread.")
        try:
            th_serial.join()
        except KeyboardInterrupt:
            pass
        logger.debug("Unlinking files.")
        for path in Cleanup_Unlink:
            os.unlink(path)
        logger.debug("Done.")

if __name__ == '__main__':
    sys.exit(main())
