#!/usr/bin/env python3
"""devkit.py -- host CLI for the dev-kit's `esp_console` REPL over USB (Plan 5.6 Task 11, spec
§5: docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md).

The dev-kit (devcontroller/) runs an esp_console REPL on its own USB UART0 (prompt "devkit> ",
115200 8N1). Log lines ("I (1234) tag: ...", ANSI colour codes possible) can interleave with
command output on the same port. Every command accepts a trailing --json and then prints exactly
ONE JSON object on ONE line; a failure prints `ERR <reason>` (human) or `{"err":"<reason>"}`
(--json), and the REPL itself then prints "Command returned non-zero error code: 0x1 (ERROR)".

This tool drives that REPL and parses the replies -- it never talks anything but the console
protocol below. `selftest all` replaces the ad-hoc bench scripts of the Plan 5.5 gates (spec §5.3).

--------------------------------------------------------------------------------------------------
CONSOLE COMMAND TABLE -- what this CLI sends, and where each subcommand is implemented device-side
--------------------------------------------------------------------------------------------------
    devkit.py subcommand        console line(s) sent                       device task (Plan 5.6)
    -----------------------     -----------------------------------------  --------------------
    status                      dc status --json                           T4 (cmd_dc.c)
    lt <cmd...>                 lt <cmd...> --json                         T5 (cmd_lt.c)
    trace on|off                link trace on|off --json                   T5 (cmd_lt.c)
    stream stats                stream stats --json                        T6 (cmd_stream.c)
    stream tap [type]           stream tap on [type]  ...  stream tap off  T6 (cmd_stream.c)
    selftest [what]             selftest <what> --json                     T9 (cmd_selftest.c)
    shell                       lt shell                                   T8 (cmd_shell.c)
    flash <image.bin>           flash stage <size> <sha> ...  flash push   T10 (cmd_flash.c)

`lt <cmd...>` is a raw relay: whatever is typed after `lt` (e.g. `status`, `config get`,
`config set {"k":1}`, `delete <id>`, `open <id> <fmt>`) is joined with spaces, "lt " is prepended
and " --json" appended, and the parsed reply is `{"cmd","rc","attempts","rt_ms",...,"fw":...}`
(spec §5.2/§5.3). `trace`/`stream stats`/`selftest` are likewise one-shot --json round trips.

`stream tap [type]` and `shell` are NOT one-shot: they hold the port open and relay/print until
the user stops them (Ctrl-C for tap; the "~." escape or a device "bridge closed" for shell).

--------------------------------------------------------------------------------------------------
FLASHING THE LAP-TIMER OVER USB (spec §5.4) -- `devkit.py flash` = stage_image + push_and_wait
--------------------------------------------------------------------------------------------------
The staging protocol deliberately mirrors the lap-timer's own `ota recv` handshake
(tools/ota_push.py talks that one directly; this one talks the dev-kit's mirror of it), so the two
host tools share their shape (`extract_ver_hwid`/`wait_for_token`/`stream_image` there have direct
analogues here):

    host:    flash stage <size> <sha256hex>\\r\\n
    dev-kit: STAGE-READY                  (ota_stage erased for size; console now in raw mode)
    host:    <size> raw bytes
    dev-kit: STAGE-END 0x0000             (SHA-256 matches; image parsed: ver/hwid via image_desc)
          |  STAGE-ERR <code>             (badsize | badsha | timeout | write)
    host:    flash push\\r\\n  ->  "pushing NN%" progress lines, then a final result:
    dev-kit: flash result 0x0000          (pushed + applied by the lap-timer's `ota recv`)
          |  flash result 0x<code>        (an E_OTA_* code -- same table as /api/flash's flash_err)
          |  flash result link -N         (a linkhost-level failure, e.g. LINKHOST_E_TIMEOUT)

This is cmd-OTA: it needs a signed image, a working lap-timer firmware, and the lap-timer's own
battery precondition (spec §19.5). The ROM-bootloader path (a bricked lap-timer) stays deferred.

--------------------------------------------------------------------------------------------------
EXIT CODES
--------------------------------------------------------------------------------------------------
    0   success / PASS
    1   failure / FAIL (any DevkitError: a device ERR/{"err":...}, a selftest FAIL, a non-0x0000
        flash result, or a protocol timeout) -- the message goes to stderr as "ERR <reason>"
    2   usage or port error (bad args, pyserial missing, no port found, port would not open)

Requires pyserial (tools/requirements.txt: pyserial==3.5). If missing:
    pip3 install pyserial        (or: source tools/idf-env.sh, which puts it on PATH)
"""
import argparse
import glob
import hashlib
import json
import re
import sys
import time

PROMPT = "devkit> "
DEFAULT_BAUD = 115200
CHUNK = 4096                    # host TX block size for `flash stage`'s raw upload

JSON_TIMEOUT_S = 5.0            # dc status / trace / stream stats: cheap, local reads
LT_TIMEOUT_S = 10.0             # lt <cmd>: a framed round trip to the lap-timer (up to 3 attempts)
SELFTEST_TIMEOUT_S = 30.0       # selftest [all]: `stream` alone samples for ~10 s by default
STAGE_READY_TIMEOUT_S = 5.0
PUSH_TIMEOUT_S = 120.0          # a 600 KB image is ~55 s at 115200 baud; generous slack on top
STREAM_TAP_ACK_TIMEOUT_S = 2.0  # stream tap on <type>: local, one round trip through the console
SHELL_CLOSE_TIMEOUT_S = 2.0     # lt shell: wait for the device's own "bridge closed" after '~.'
TAP_MAX_S = 3600.0              # stream tap's row loop: per-_readline bound, not a session cap
                                 # (the loop itself runs until Ctrl-C; this just keeps each single
                                 # wait finite so a dead link doesn't block forever between rows)

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")


class DevkitError(Exception):
    """Any device-side error (ERR / {"err":...}), a rejected flash handshake (STAGE-ERR / a
    non-0x0000 flash result), or a protocol timeout. Callers surface this as one line on stderr
    and exit 1 -- see main()."""


def _strip_ansi(s):
    """Remove ANSI CSI escape sequences (colourised log lines) from `s`."""
    return _ANSI_RE.sub("", s)


# ==================================================================================================
#  Low-level reads over a serial-like object (bounded loops only -- every wait has a deadline)
# ==================================================================================================

def _readline(ser, deadline, max_iters=100000):
    """Accumulate bytes from repeated ser.readline() calls until a full line (ending in '\\n')
    arrives or `deadline` (a time.monotonic() value) passes. Returns the line decoded (latin-1,
    matching tools/ota_push.py's wait_for_token) with the trailing CR/LF stripped, or None on
    timeout. Bounded by max_iters so a misbehaving serial-like object can never spin forever."""
    buf = b""
    for _ in range(max_iters):
        if time.monotonic() >= deadline:
            return None
        chunk = ser.readline()
        if not chunk:
            continue
        buf += chunk
        if buf.endswith(b"\n"):
            return buf.decode("latin-1", errors="replace").rstrip("\r\n")
    return None


def _wait_for_prefix(ser, prefixes, deadline, max_iters=100000):
    """Read lines (via _readline) until one, after stripping ANSI and surrounding whitespace,
    starts with one of `prefixes`; other lines (log noise, echoed input) are skipped. Returns the
    cleaned matching line, or None if `deadline` passes first."""
    for _ in range(max_iters):
        if time.monotonic() >= deadline:
            return None
        line = _readline(ser, deadline)
        if line is None:
            return None
        clean = _strip_ansi(line).strip()
        for p in prefixes:
            if clean.startswith(p):
                return clean
    return None


def _read_until(ser, target, deadline, max_iters=2000000):
    """Accumulate raw bytes one at a time (ser.read(1)) until `target` (bytes) appears in the
    buffer or `deadline` passes. Used only for the console PROMPT, which (unlike every other
    reply line) is never newline-terminated -- it is left on the line waiting for input. Returns
    the buffer up to and including `target`, or None on timeout."""
    buf = b""
    for _ in range(max_iters):
        idx = buf.find(target)
        if idx >= 0:
            return buf[: idx + len(target)]
        if time.monotonic() >= deadline:
            return None
        chunk = ser.read(1)
        if chunk:
            buf += chunk
    return None


# ==================================================================================================
#  Pure helpers (serial-like `ser` in, no globals) -- unit-tested against FakeSerial
# ==================================================================================================

def read_json(ser, timeout=5.0):
    """Read lines from `ser` until one, after stripping ANSI escapes and surrounding whitespace,
    starts with '{'; parse it as JSON and return the dict. A line that is not valid JSON, or that
    parses to something other than an object, is skipped (log noise, an echoed command, ...), not
    raised on. Raises DevkitError("timeout waiting for json") if `timeout` s pass with no such
    line, or DevkitError(obj["err"]) if the parsed object carries the console's own error key."""
    deadline = time.monotonic() + timeout
    while True:
        line = _readline(ser, deadline)
        if line is None:
            raise DevkitError("timeout waiting for json")
        clean = _strip_ansi(line).strip()
        if not clean.startswith("{"):
            continue
        try:
            obj = json.loads(clean)
        except ValueError:
            continue
        if not isinstance(obj, dict):
            continue
        if "err" in obj:
            raise DevkitError(obj["err"])
        return obj


def send_cmd(ser, line, timeout=5.0):
    """Write `line` + CRLF, then read raw bytes until the next "devkit> " prompt (bounded by
    `timeout` s). Returns the reply text with the echoed command line (esp_console/linenoise
    echoes every typed character back) removed, ANSI escapes stripped, and the trailing
    prompt/newlines trimmed. Raises DevkitError("timeout waiting for prompt") on timeout. Used for
    the human (non-JSON) commands; JSON commands go through read_json instead (see _json_cmd)."""
    deadline = time.monotonic() + timeout
    ser.write((line + "\r\n").encode("ascii"))
    ser.flush()
    raw = _read_until(ser, PROMPT.encode("ascii"), deadline)
    if raw is None:
        raise DevkitError("timeout waiting for prompt")

    text = _strip_ansi(raw.decode("latin-1", errors="replace"))
    if text.endswith(PROMPT):
        text = text[: -len(PROMPT)]
    head, sep, rest = text.partition("\n")
    if sep and head.rstrip("\r") == line:
        text = rest
    return text.strip("\r\n")


def stage_image(ser, path, ready_timeout=STAGE_READY_TIMEOUT_S):
    """Run the `flash stage` handshake (spec §5.4) for the image at `path`: compute size +
    SHA-256, send `flash stage <size> <sha256hex>`, wait for STAGE-READY, stream the raw bytes in
    CHUNK-byte writes (each flushed), then wait for STAGE-END/STAGE-ERR -- bounded by 30 s plus
    size/4096 * 0.1 s (slack for a full 115200-baud upload). Returns the hex code string out of
    "STAGE-END 0x0000" (e.g. "0x0000"). Raises DevkitError -- message is the device's own
    STAGE-ERR line (e.g. "STAGE-ERR badsha") -- on a rejection or either wait timing out."""
    with open(path, "rb") as f:
        data = f.read()
    size = len(data)
    sha_hex = hashlib.sha256(data).hexdigest()

    ser.write(("flash stage %d %s\r\n" % (size, sha_hex)).encode("ascii"))
    ser.flush()

    ready_deadline = time.monotonic() + ready_timeout
    ready = _wait_for_prefix(ser, ("STAGE-READY", "STAGE-ERR"), ready_deadline)
    if ready is None:
        raise DevkitError("timeout waiting for STAGE-READY")
    if ready.startswith("STAGE-ERR"):
        raise DevkitError(ready)

    sent = 0
    while sent < size:
        end = min(sent + CHUNK, size)
        ser.write(data[sent:end])
        ser.flush()
        sent = end

    end_timeout = 30.0 + (size / 4096.0) * 0.1
    end_deadline = time.monotonic() + end_timeout
    result = _wait_for_prefix(ser, ("STAGE-END", "STAGE-ERR"), end_deadline)
    if result is None:
        raise DevkitError("timeout waiting for STAGE-END")
    if result.startswith("STAGE-ERR"):
        raise DevkitError(result)

    parts = result.split()
    return parts[1] if len(parts) > 1 else result


def push_and_wait(ser, timeout=PUSH_TIMEOUT_S):
    """Send `flash push`, then read lines until one reads `flash result <code>` -- "0x0000"..
    "0x<hex>" from the E_OTA_*/flash_err table, or "link -N" for a linkhost-level failure (spec
    §5.4's "same codes as /api/flash's flash_err"). `pushing NN%` progress lines are printed
    (stdout) as they arrive; other lines (log noise) are skipped. Returns the result string (e.g.
    "0x0000") on success; raises DevkitError on any other result or on a `timeout`-second wait
    with no result line at all."""
    deadline = time.monotonic() + timeout
    ser.write(b"flash push\r\n")
    ser.flush()
    while True:
        line = _readline(ser, deadline)
        if line is None:
            raise DevkitError("timeout waiting for flash result")
        clean = _strip_ansi(line).strip()
        if clean.startswith("pushing "):
            print(clean)
            sys.stdout.flush()
            continue
        if clean.startswith("flash result "):
            result = clean[len("flash result ") :].strip()
            if result != "0x0000":
                raise DevkitError("flash result %s" % result)
            return result
        # other console/log noise: skip


def _json_cmd(ser, line, timeout=JSON_TIMEOUT_S):
    """Send `line` with " --json" appended and return the parsed reply via read_json. Every
    one-shot JSON subcommand (status/lt/trace/stream stats/selftest) goes through this."""
    ser.write((line + " --json\r\n").encode("ascii"))
    ser.flush()
    return read_json(ser, timeout)


# ==================================================================================================
#  Port discovery + opening (never exercised by unit tests -- always a real port)
# ==================================================================================================

def _default_port():
    """First serial device matching /dev/cu.usbserial-* or /dev/ttyUSB* (the dev-kit's USB
    bridge on macOS / Linux): prefers pyserial's own port enumeration (serial.tools.list_ports)
    when importable, falls back to a direct glob of /dev otherwise. Returns None if nothing
    matches either way."""
    candidates = []
    try:
        from serial.tools import list_ports
        candidates = [p.device for p in list_ports.comports()]
    except ImportError:
        candidates = []
    if not candidates:
        candidates = sorted(glob.glob("/dev/cu.usbserial-*")) + sorted(glob.glob("/dev/ttyUSB*"))
    for dev in candidates:
        if re.search(r"usbserial|ttyUSB", dev):
            return dev
    return None


def open_serial(port, baud, prompt_timeout=3.0):
    """Open `port` at `baud` for the dev-kit console. Most ESP32 boards still reset on the
    DTR/RTS transition that opening a serial port itself triggers (their USB bridge wires
    DTR/RTS to EN/GPIO0), even with dsrdtr/rtscts disabled and both lines dropped immediately
    after open -- opening the port cannot be guaranteed not to reset the board, only "not on
    purpose". So this waits up to `prompt_timeout` s for the boot to settle and the first
    "devkit> " prompt to appear before returning; the caller can then send commands right away.
    Never call this from a test -- it always opens a real port."""
    import serial
    ser = serial.Serial(port, baud, timeout=1.0, dsrdtr=False, rtscts=False)
    ser.dtr = False
    ser.rts = False
    _read_until(ser, PROMPT.encode("ascii"), time.monotonic() + prompt_timeout)
    return ser


# ==================================================================================================
#  CLI subcommand handlers
# ==================================================================================================

def _cmd_status(ser):
    obj = _json_cmd(ser, "dc status")
    print(json.dumps(obj, indent=2))
    return 0 if obj.get("link", {}).get("connected") else 1


def _cmd_lt(ser, lt_args):
    if not lt_args:
        print("usage: devkit.py lt <cmd...>  (e.g. status | config get | config set <json> | "
              "delete <id> | open <id> <fmt>)", file=sys.stderr)
        return 2
    line = "lt " + " ".join(lt_args)
    obj = _json_cmd(ser, line, timeout=LT_TIMEOUT_S)
    print(json.dumps(obj, indent=2))
    return 0 if obj.get("rc") == 0 else 1


def _cmd_trace(ser, state):
    obj = _json_cmd(ser, "link trace %s" % state)
    print(json.dumps(obj, indent=2))
    return 0


def _cmd_stream_stats(ser):
    obj = _json_cmd(ser, "stream stats")
    print(json.dumps(obj, indent=2))
    return 0


def _cmd_stream_tap(ser, rec_type):
    """`stream tap [type]`: sends `stream tap on [type]` (no --json -- its ack is human text, "OK
    tap on <type>" or "ERR <reason>"; the decoded rows the consumer task prints while tap is on
    are already one JSON object per line, unrelated to --json). The ack is read via send_cmd,
    which blocks (bounded, STREAM_TAP_ACK_TIMEOUT_S) until the prompt that follows it -- so the
    row loop below only starts once the ack (and nothing before it) has been consumed, and no live
    row is mistaken for the ack or vice versa. An "ERR ..." ack raises DevkitError. Prints every
    row until Ctrl-C, then always sends `stream tap off` so the dev-kit stops flooding the console
    after this tool exits."""
    on_line = "stream tap on" + ((" " + rec_type) if rec_type else "")
    ack = send_cmd(ser, on_line, timeout=STREAM_TAP_ACK_TIMEOUT_S)
    if ack.startswith("ERR"):
        raise DevkitError(ack)
    try:
        while True:
            line = _readline(ser, time.monotonic() + TAP_MAX_S)
            if line is None:
                continue
            clean = _strip_ansi(line).strip()
            if clean:
                print(clean)
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.write(b"stream tap off\r\n")
        ser.flush()
        _read_until(ser, PROMPT.encode("ascii"), time.monotonic() + 2.0)
    return 0


def _cmd_selftest(ser, what):
    obj = _json_cmd(ser, "selftest %s" % what, timeout=SELFTEST_TIMEOUT_S)
    print(json.dumps(obj, indent=2))
    return 0 if obj.get("pass") else 1


def _cmd_flash(ser, image_path):
    code = stage_image(ser, image_path)
    print("staged: %s" % code)
    result = push_and_wait(ser)   # raises DevkitError on any non-0x0000 result
    print("result: %s" % result)
    return 0


def _shell_close(ser, timeout=SHELL_CLOSE_TIMEOUT_S):
    """Best-effort clean shutdown of the device's `lt shell` bridge (cmd_shell.c, T8): send the
    "~." escape -- write errors are ignored, the port may already be in a bad state on this path --
    then wait up to `timeout` s (also ignored on expiry) for the device's own "bridge closed" line.
    Called from every non-graceful exit out of _cmd_shell's relay loop (Ctrl-C, any other
    exception) so the device's bridge does not sit open until its own 10 min cap merely because the
    host side gave up. The loop's own graceful exits (seeing "bridge closed", or the user typing
    "~." themselves) already send the escape / see the confirmation inline and do not call this.
    Returns True if "bridge closed" was actually seen, False on a write failure or a timeout."""
    try:
        ser.write(b"~.")
        ser.flush()
    except Exception:
        return False
    deadline = time.monotonic() + timeout
    return _read_until(ser, b"bridge closed", deadline) is not None


def _cmd_shell(ser):
    """`lt shell`: raw byte bridge to the lap-timer console (spec §5.2 / cmd_shell.c, T8), relayed
    1:1 between this terminal and the serial port. The local tty is put into cbreak mode so every
    keystroke goes straight through with no host-side line editing or echo -- the lap-timer's own
    console supplies both, relayed back to us by the dev-kit. Escape handling mirrors the device's
    own byte-at-a-time state machine exactly (cmd_shell.c: a lone '~' at the start of a line is
    held back pending the next byte; '.' closes the bridge, anything else releases the held '~'
    then that byte, both forwarded together): typing "~." sends those two bytes through -- so the
    device's OWN bridge closes too -- and this loop then exits without waiting for "bridge closed"
    (belt-and-braces: exiting on "bridge closed" arriving is the other, independent way out, e.g.
    if the device's 10 min cap fires first). On Ctrl-C, or any other exception out of the relay
    loop, _shell_close(ser) is called FIRST (send "~." + wait for "bridge closed", both
    best-effort) so the device's bridge is told to close too, before the tty is restored -- a bare
    Ctrl-C must not leave the device's bridge open. The tty is ALWAYS restored on exit (every path
    above) via the try/finally below, which runs after the except handlers per normal Python
    try/except/finally ordering."""
    import select
    import termios
    import tty

    ser.write(b"lt shell\r\n")
    ser.flush()

    fd = sys.stdin.fileno()
    old_attrs = termios.tcgetattr(fd)
    ser_fd = ser.fileno()
    at_bol = True
    pending_tilde = False
    tail = b""
    bridge_closed = b"bridge closed"
    try:
        tty.setcbreak(fd)
        while True:
            r, _, _ = select.select([ser_fd, fd], [], [], 0.2)
            if ser_fd in r:
                n = getattr(ser, "in_waiting", 0) or 1
                chunk = ser.read(n)
                if chunk:
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
                    tail = (tail + chunk)[-len(bridge_closed) :]
                    if bridge_closed in tail:
                        return 0
            if fd in r:
                b = sys.stdin.buffer.read(1)
                if not b:
                    continue
                if pending_tilde:
                    pending_tilde = False
                    if b == b".":
                        ser.write(b"~.")
                        ser.flush()
                        return 0
                    ser.write(b"~" + b)
                    ser.flush()
                    at_bol = b in (b"\r", b"\n")
                    continue
                if at_bol and b == b"~":
                    pending_tilde = True
                    continue
                ser.write(b)
                ser.flush()
                at_bol = b in (b"\r", b"\n")
    except KeyboardInterrupt:
        _shell_close(ser)
        print("bridge closed by Ctrl-C", file=sys.stderr)
        return 0
    except Exception:
        _shell_close(ser)
        raise
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_attrs)


def _dispatch(ser, args):
    if args.cmd == "status":
        return _cmd_status(ser)
    if args.cmd == "lt":
        return _cmd_lt(ser, args.lt_args)
    if args.cmd == "trace":
        return _cmd_trace(ser, args.state)
    if args.cmd == "stream":
        if args.stream_cmd == "stats":
            return _cmd_stream_stats(ser)
        return _cmd_stream_tap(ser, args.type)
    if args.cmd == "selftest":
        return _cmd_selftest(ser, args.what)
    if args.cmd == "flash":
        return _cmd_flash(ser, args.image)
    if args.cmd == "shell":
        return _cmd_shell(ser)
    return 2   # unreachable: argparse's subparser 'required=True' already rejects anything else


# ==================================================================================================
#  argparse + main
# ==================================================================================================

def build_argparser():
    ap = argparse.ArgumentParser(
        prog="devkit.py",
        description="Host CLI for the dev-kit's esp_console REPL over USB (Plan 5.6, spec §5).")
    ap.add_argument("--port",
                     help="serial device (default: first /dev/cu.usbserial-* or /dev/ttyUSB*)")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                     help="baud rate (default %d)" % DEFAULT_BAUD)

    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", help="dc status --json (exit 1 if link not connected)")

    p_lt = sub.add_parser("lt", help="relay a lap-timer console command: lt <cmd...> --json")
    p_lt.add_argument("lt_args", nargs=argparse.REMAINDER,
                       help="status | list | config get | config set <json> | delete <id> | "
                            "open <id> <fmt>")

    p_trace = sub.add_parser("trace", help="link trace on|off --json")
    p_trace.add_argument("state", choices=["on", "off"])

    p_stream = sub.add_parser("stream", help="stream stats | stream tap [type]")
    stream_sub = p_stream.add_subparsers(dest="stream_cmd", required=True)
    stream_sub.add_parser("stats", help="stream stats --json")
    p_tap = stream_sub.add_parser("tap", help="stream tap on [type] ... until Ctrl-C ... tap off")
    p_tap.add_argument("type", nargs="?", choices=["fused", "event", "status"], default=None)

    p_selftest = sub.add_parser("selftest", help="selftest [link|stream|framing|all] --json")
    p_selftest.add_argument("what", nargs="?", choices=["link", "stream", "framing", "all"],
                             default="all")

    p_flash = sub.add_parser("flash", help="flash stage + flash push a signed image")
    p_flash.add_argument("image", help="path to the signed .bin image")

    sub.add_parser("shell", help="lt shell -- raw byte bridge to the lap-timer console")

    return ap


def main(argv=None):
    ap = build_argparser()
    args = ap.parse_args(argv)

    try:
        import serial          # noqa: F401 -- presence check only; see open_serial for real use
    except ImportError:
        print("FAIL: pyserial not found. Install with 'pip3 install pyserial' "
              "or 'source tools/idf-env.sh'.", file=sys.stderr)
        return 2

    port = args.port or _default_port()
    if not port:
        print("FAIL: no --port given and no /dev/cu.usbserial-*|/dev/ttyUSB* found",
              file=sys.stderr)
        return 2

    try:
        ser = open_serial(port, args.baud)
    except Exception as e:   # serial.SerialException / OSError / ...
        print("FAIL: cannot open %s: %s" % (port, e), file=sys.stderr)
        return 2

    try:
        return _dispatch(ser, args)
    except DevkitError as e:
        print("ERR %s" % e, file=sys.stderr)
        return 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
