#!/usr/bin/env python3
"""ota_push.py -- host-side serial OTA push for the lap-timer firmware (spec §19.6).

Pushes a *signed* firmware image to a device over USB-UART, driving the device's `ota recv`
console command (components/drivers/export_serial/export_serial.c, dev-UX build only). The device
protocol (from components/app/ota/ota.c) is:

    host: ota recv <size_dec> <sha256_hex> <ver_str> <hwid_str><CR>
    dev : OTA-READY                      (ota_begin succeeded -> device now reads raw bytes)
    host: <size> raw image bytes         (streamed straight onto UART0, no framing)
    dev : OTA-END 0x0000                 (SHA + ECDSA verify OK -> supervisor reboots in ~2 s)
      or  OTA-ERR 0x<code>               (begin/data/end failure; see the E_OTA_* table below)
      or  OTA-ERR timeout|read|usage|badsize|badsha

The SHA-256 sent in the header is computed over the EXACT bytes streamed, so a `--corrupt` copy
gets a matching SHA (its byte-level integrity check passes) and fails later at the ECDSA signature
verification -- exercising the real §19.2 signing path rather than a trivial checksum mismatch.

E_OTA_* codes (components/app/include/app/lt_err.h):
    0x0801 E_OTA_PRECOND   low battery / already pending / bad size (charger or >=3800 mV needed)
    0x0802 E_OTA_HWID      target image hwid or project_name mismatch
    0x0803 E_OTA_WRITE     esp_ota_write or SHA-256 mismatch
    0x0804 E_OTA_SIG       ECDSA signature verification failed (esp_ota_end)

--------------------------------------------------------------------------------------------------
§19.6 OTA MATRIX -- the three flash-tests the operator runs over USB-UART
--------------------------------------------------------------------------------------------------
Images are signed with tools/sign_release.sh (the build does NOT auto-sign:
SECURE_BOOT_BUILD_SIGNED_BINARIES=n). Sign each env once, producing dist/laptimer-<ver>-<hwid>.bin:

    source tools/idf-env.sh                 # v5.3.2
    tools/sign_release.sh moto_sim          # -> dist/laptimer-<ver>-moto_sim_epaper.bin
    tools/sign_release.sh moto_neo6m        # -> dist/laptimer-<ver>-moto_neo6m_epaper.bin

The device under test is a moto_sim board. OTA_BEGIN enforces the battery precondition, so the DUT
must be on the charger or read >=3800 mV, else every case returns 0x0801 E_OTA_PRECOND.

  (1) GOOD -- the matching signed image applies + validates:
        python3 tools/ota_push.py --port /dev/tty.usbserial-XXXX \
            --image dist/laptimer-<ver>-moto_sim_epaper.bin
      expect: OTA-END 0x0000 -> PASS; device reboots into the new slot and self-validates (§19.4).

  (2) WRONG-HWID -- the moto_neo6m image pushed to a moto_sim device is rejected:
        python3 tools/ota_push.py --port /dev/tty.usbserial-XXXX \
            --image dist/laptimer-<ver>-moto_neo6m_epaper.bin
      expect: OTA-ERR 0x0802 (E_OTA_HWID). The header hwid ("moto_neo6m_epaper") mismatches the
      device hwid at ota_begin, so it is rejected before raw mode -- a clean, fast fail.

  (3) CORRUPT -- the matching image with one flipped byte fails signature verification:
        python3 tools/ota_push.py --port /dev/tty.usbserial-XXXX \
            --image dist/laptimer-<ver>-moto_sim_epaper.bin --corrupt
      expect: OTA-ERR 0x0804 (E_OTA_SIG). The flipped byte keeps the SHA self-consistent (SHA is
      taken over the corrupted bytes) but breaks the ECDSA signature -> rejected at ota_end; the
      running image is left untouched (§19.6).

Requires pyserial (tools/requirements.txt: pyserial==3.5). If missing:
    pip3 install pyserial        (or: source tools/idf-env.sh, which puts it on PATH)
"""
import argparse
import hashlib
import re
import sys
import time

# esp_app_desc_t sits at file offset 0x20; version[32] at 0x30, project_name[32] at 0x50; the §19.3
# custom hwid descriptor (24 B) is placed right after esp_app_desc at 0x120 (ota.c IMG_HWID_OFF).
DESC_OFF = 0x20
VER_OFF = 0x30
HWID_OFF = 0x120
HWID_LEN = 24
VER_FIELD = 16                 # OTA_BEGIN ver field width

CHUNK = 4096                   # host TX block size (matches the device read granularity)
READ_TIMEOUT_S = 5.0           # per-line serial read timeout
BAUD_BYTES_PER_S = 115200 / 10 # 8N1: ~1 start+8 data+1 stop bit per byte

E_OTA = {
    0x0000: "OK",
    0x0801: "E_OTA_PRECOND",
    0x0802: "E_OTA_HWID",
    0x0803: "E_OTA_WRITE",
    0x0804: "E_OTA_SIG",
    0x0805: "E_OTA_VALIDATED",
    0x0806: "E_OTA_ROLLBACK",
}


def _field_str(buf, off, length):
    """Extract an ASCII field of at most `length` bytes, truncated at the first NUL."""
    if off + length > len(buf):
        return ""
    raw = bytes(buf[off:off + length])
    nul = raw.find(0)
    if nul >= 0:
        raw = raw[:nul]
    return raw.decode("ascii", errors="replace").strip()


def extract_ver_hwid(buf):
    """Best-effort ver/hwid straight out of the image header (§19.3)."""
    ver = _field_str(buf, VER_OFF, VER_FIELD)
    hwid = _field_str(buf, HWID_OFF, HWID_LEN)
    return ver, hwid


def wait_for_token(ser, deadline):
    """Read lines until one is an OTA-* token or the deadline passes. Returns the stripped line, or
    None on timeout. Echoed command text and the prompt are skipped. Bounded by the deadline."""
    max_lines = 100000                              # bound: never spin unboundedly on serial noise
    for _ in range(max_lines):
        if time.monotonic() >= deadline:
            return None
        raw = ser.readline()                        # returns b"" on the per-read timeout
        if not raw:
            continue
        line = raw.decode("latin-1", errors="replace").strip()
        if line.startswith("OTA-"):
            return line
    return None


def stream_image(ser, data):
    """Write the image in CHUNK blocks, flushing each so the OS drains to the UART at line rate
    (natural pacing for the device's small RX ring). Bounded by len(data)."""
    total = len(data)
    sent = 0
    while sent < total:
        end = min(sent + CHUNK, total)
        ser.write(data[sent:end])
        ser.flush()
        sent = end
        pct = 100 * sent // total
        sys.stdout.write("\r  streaming %d/%d B (%d%%)" % (sent, total, pct))
        sys.stdout.flush()
    sys.stdout.write("\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Serial OTA push for the lap-timer firmware (§19.6).")
    ap.add_argument("--port", required=True, help="serial device, e.g. /dev/tty.usbserial-XXXX")
    ap.add_argument("--image", required=True, help="signed image (dist/laptimer-<ver>-<hwid>.bin)")
    ap.add_argument("--corrupt", action="store_true",
                    help="flip one mid-image byte in a copy before sending (expect E_OTA_SIG)")
    ap.add_argument("--ver", help="override the ver string (default: read from the image header)")
    ap.add_argument("--hwid", help="override the hwid string (default: read from the image header)")
    ap.add_argument("--baud", type=int, default=115200, help="baud rate (default 115200)")
    args = ap.parse_args(argv)

    try:
        import serial                              # pyserial
    except ImportError:
        print("FAIL: pyserial not found. Install with 'pip3 install pyserial' "
              "or 'source tools/idf-env.sh'.", file=sys.stderr)
        return 2

    try:
        with open(args.image, "rb") as f:
            data = bytearray(f.read())
    except OSError as e:
        print("FAIL: cannot read image %s: %s" % (args.image, e), file=sys.stderr)
        return 2
    if len(data) == 0:
        print("FAIL: image is empty", file=sys.stderr)
        return 2

    if args.corrupt:
        # Flip a byte in the middle: past the 4 KB header (keeps hwid/project checks passing) and
        # before the appended signature block, so the failure lands on the ECDSA verify (E_OTA_SIG).
        pos = len(data) // 2
        data[pos] ^= 0xFF
        print("corrupt: flipped byte at offset 0x%x (0x%02x)" % (pos, data[pos]))

    sha_hex = hashlib.sha256(bytes(data)).hexdigest()
    ver, hwid = extract_ver_hwid(data)
    if args.ver:
        ver = args.ver
    if args.hwid:
        hwid = args.hwid
    if not ver:
        ver = "-"                                   # device ignores ver; keep the arg non-empty
    if not hwid:
        print("FAIL: could not read hwid from the image; pass --hwid explicitly", file=sys.stderr)
        return 2

    size = len(data)
    print("image : %s" % args.image)
    print("size  : %d B" % size)
    print("sha256: %s" % sha_hex)
    print("ver   : %s" % ver)
    print("hwid  : %s" % hwid)

    cmd = "ota recv %d %s %s %s\r" % (size, sha_hex, ver, hwid)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=READ_TIMEOUT_S)
    except serial.SerialException as e:
        print("FAIL: cannot open %s: %s" % (args.port, e), file=sys.stderr)
        return 2

    rc = 2
    try:
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        ser.write(cmd.encode("ascii"))
        ser.flush()

        # ota_begin runs before OTA-READY; give it a few seconds (battery/hwid checks are cheap).
        begin = wait_for_token(ser, time.monotonic() + 10.0)
        if begin is None:
            print("FAIL: no response to 'ota recv' (no OTA-READY/OTA-ERR within 10 s)")
            return 1
        if begin != "OTA-READY":
            print("result: %s" % begin)
            print("FAIL: device rejected ota_begin (%s)" % describe(begin))
            return 1
        print("OTA-READY -- streaming image")

        stream_image(ser, data)

        # End: SHA + ECDSA verify on the device, then the token. Budget the stream time + slack.
        deadline = time.monotonic() + (size / BAUD_BYTES_PER_S) + 30.0
        end = wait_for_token(ser, deadline)
        if end is None:
            print("FAIL: no OTA-END/OTA-ERR after streaming (timed out)")
            return 1
        print("result: %s" % end)
        code = parse_code(end)
        if end.startswith("OTA-END") and code == 0x0000:
            print("PASS: image applied + verified; the device reboots into the new slot (§19.4).")
            rc = 0
        else:
            print("FAIL: %s" % describe(end))
            rc = 1
    finally:
        ser.close()
    return rc


def parse_code(line):
    """Pull the 0x<hex> code out of an OTA-END/OTA-ERR line, or None if it carries no code."""
    m = re.search(r"0x([0-9a-fA-F]+)", line)
    return int(m.group(1), 16) if m else None


def describe(line):
    code = parse_code(line)
    if code is None:
        return line                                 # e.g. 'OTA-ERR timeout'
    return "%s (0x%04x %s)" % (line, code, E_OTA.get(code, "unknown"))


if __name__ == "__main__":
    sys.exit(main())
