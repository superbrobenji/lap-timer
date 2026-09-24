#!/usr/bin/env python3
"""test_devkit.py -- unit tests for tools/devkit.py's pure, serial-like helpers (Plan 5.6 Task 11).

Every helper under test (`read_json`, `send_cmd`, `stage_image`, `push_and_wait`) takes a
serial-like object, so it is exercised here against `FakeSerial` -- a scripted byte queue plus a
`written` capture buffer -- with NO real port ever opened. Run from the repo root:

    python3 -m unittest tools/test_devkit.py
"""
import hashlib
import os
import sys
import tempfile
import unittest

# `python3 -m unittest tools/test_devkit.py` (run from the repo root, per the task's verification
# command) imports this file as `tools.test_devkit` without putting `tools/` itself on sys.path --
# so `import devkit` (its sibling, same directory) needs an explicit assist here, independent of
# how this file is invoked (module path, plain script, or `cd tools && python3 -m unittest ...`).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devkit import DevkitError, push_and_wait, read_json, send_cmd, stage_image  # noqa: E402


class FakeSerial:
    """A minimal pyserial-like double. `rx` is the canned bytes the device "sends", consumed
    line-by-line by .readline() (pyserial semantics: returns what it has, up to and including the
    next b"\\n", or whatever's left with no trailing newline once the queue drains -- modelling a
    per-call read timeout) and byte-by-byte by .read(n). Every .write() is appended to `written`
    for exact-bytes assertions. No timers, no blocking: devkit.py's own deadline loops are what
    make a real timeout observable, not this double."""

    def __init__(self, rx=b""):
        self._rx = rx
        self.written = bytearray()
        self.timeout = 1.0
        self.dtr = True
        self.rts = True

    def write(self, data):
        self.written.extend(data)
        return len(data)

    def flush(self):
        pass

    def readline(self):
        if not self._rx:
            return b""
        idx = self._rx.find(b"\n")
        if idx < 0:
            line, self._rx = self._rx, b""
            return line
        line, self._rx = self._rx[: idx + 1], self._rx[idx + 1 :]
        return line

    def read(self, n=1):
        data, self._rx = self._rx[:n], self._rx[n:]
        return data

    def reset_input_buffer(self):
        pass

    def reset_output_buffer(self):
        pass

    def close(self):
        pass


class ReadJsonTest(unittest.TestCase):
    def test_skips_log_noise_then_returns_the_object(self):
        ser = FakeSerial(rx=b"I (123) log: noise\r\n" b'{"foo":1}\r\n')
        obj = read_json(ser, timeout=1.0)
        self.assertEqual(obj, {"foo": 1})

    def test_raises_on_err_object(self):
        ser = FakeSerial(rx=b'{"err":"busy"}\r\n')
        with self.assertRaises(DevkitError) as ctx:
            read_json(ser, timeout=1.0)
        self.assertEqual(str(ctx.exception), "busy")

    def test_raises_devkiterror_on_timeout(self):
        ser = FakeSerial(rx=b"I (123) log: noise\r\n")   # never a '{' line
        with self.assertRaises(DevkitError) as ctx:
            read_json(ser, timeout=0.05)
        self.assertIn("timeout", str(ctx.exception))


class SendCmdTest(unittest.TestCase):
    def test_strips_the_echo_and_ansi(self):
        rx = (
            b"dc log info\r\n"
            b"\x1b[0;32mI (123) console: ok\x1b[0m\r\n"
            b"OK info\r\n"
            b"devkit> "
        )
        ser = FakeSerial(rx=rx)
        reply = send_cmd(ser, "dc log info", timeout=1.0)

        self.assertEqual(bytes(ser.written), b"dc log info\r\n")
        self.assertNotIn("\x1b", reply)
        self.assertNotIn("dc log info", reply)
        self.assertEqual(reply, "I (123) console: ok\r\nOK info")


class StageImageTest(unittest.TestCase):
    def setUp(self):
        fd, self.path = tempfile.mkstemp(prefix="devkit_test_")
        os.close(fd)
        with open(self.path, "wb") as f:
            f.write(b"hello world")

    def tearDown(self):
        os.remove(self.path)

    def test_happy_path_returns_0x0000_and_wrote_the_exact_handshake_bytes(self):
        data = b"hello world"
        sha_hex = hashlib.sha256(data).hexdigest()
        rx = b"STAGE-READY\r\n" b"STAGE-END 0x0000\r\n"
        ser = FakeSerial(rx=rx)

        code = stage_image(ser, self.path)

        self.assertEqual(code, "0x0000")
        expected = ("flash stage %d %s\r\n" % (len(data), sha_hex)).encode("ascii") + data
        self.assertEqual(bytes(ser.written), expected)

    def test_stage_err_badsha_raises_devkiterror(self):
        rx = b"STAGE-READY\r\n" b"STAGE-ERR badsha\r\n"
        ser = FakeSerial(rx=rx)

        with self.assertRaises(DevkitError) as ctx:
            stage_image(ser, self.path)
        self.assertIn("badsha", str(ctx.exception))
        self.assertIn("STAGE-ERR", str(ctx.exception))


class PushAndWaitTest(unittest.TestCase):
    def test_returns_0x0000_after_progress_lines(self):
        rx = b"pushing 50%\r\n" b"pushing 100%\r\n" b"flash result 0x0000\r\n"
        ser = FakeSerial(rx=rx)

        result = push_and_wait(ser, timeout=1.0)

        self.assertEqual(result, "0x0000")
        self.assertEqual(bytes(ser.written), b"flash push\r\n")

    def test_raises_on_nonzero_result(self):
        ser = FakeSerial(rx=b"flash result 0x0102\r\n")
        with self.assertRaises(DevkitError) as ctx:
            push_and_wait(ser, timeout=1.0)
        self.assertIn("0x0102", str(ctx.exception))

    def test_raises_on_link_result(self):
        ser = FakeSerial(rx=b"flash result link -4\r\n")
        with self.assertRaises(DevkitError) as ctx:
            push_and_wait(ser, timeout=1.0)
        self.assertIn("link -4", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
