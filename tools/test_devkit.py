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
import time
import unittest

# `python3 -m unittest tools/test_devkit.py` (run from the repo root, per the task's verification
# command) imports this file as `tools.test_devkit` without putting `tools/` itself on sys.path --
# so `import devkit` (its sibling, same directory) needs an explicit assist here, independent of
# how this file is invoked (module path, plain script, or `cd tools && python3 -m unittest ...`).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devkit import (  # noqa: E402
    DevkitError,
    _poll_err_line,
    _shell_close,
    _tap_start,
    classify_push_line,
    probe_is_devkit,
    push_and_wait,
    read_json,
    send_cmd,
    stage_image,
    trial_window_note,
)
from ota_push import (  # noqa: E402
    probe_laptimer,
    stream_image,
    trial_window_note as ota_trial_window_note,
)


class FakeSerial:
    """A minimal pyserial-like double. `rx` is the canned bytes the device "sends", consumed
    line-by-line by .readline() (pyserial semantics: returns what it has, up to and including the
    next b"\\n", or whatever's left with no trailing newline once the queue drains -- modelling a
    per-call read timeout) and byte-by-byte by .read(n). Every .write() is appended to `written`
    for exact-bytes assertions. No timers, no blocking: devkit.py's own deadline loops are what
    make a real timeout observable, not this double.

    `chunks`, when given instead of `rx`, scripts .readline() to return each element in order on
    successive calls regardless of newline content -- e.g. `[b'{"a":', b'1}\\n']` -- so a caller
    can exercise a JSON (or any other) line split across two reads by the real serial port, not
    just one already-whole line. Once the list is exhausted, .readline() returns b"" (a timeout)
    like the `rx` mode does once its buffer drains."""

    def __init__(self, rx=b"", chunks=None):
        self._rx = rx
        self._chunks = list(chunks) if chunks is not None else None
        self.written = bytearray()
        self.timeout = 1.0
        self.dtr = True
        self.rts = True

    @property
    def in_waiting(self):
        """pyserial's bytes-ready count. Modelled as "everything still queued", which is what lets
        the stage loop's non-blocking poll (devkit.py _poll_err_line) see a device line that has
        already arrived without ever adding a blocking read per chunk."""
        return len(self._rx) if self._chunks is None else (len(self._chunks[0]) if self._chunks else 0)

    def write(self, data):
        self.written.extend(data)
        return len(data)

    def flush(self):
        pass

    def readline(self):
        if self._chunks is not None:
            return self._chunks.pop(0) if self._chunks else b""
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

    def test_parses_a_line_fragmented_across_two_readline_calls(self):
        # A real serial port can hand back a JSON line in more than one readline() call (the
        # writer paused mid-line, or the read just landed on a buffer boundary); _readline must
        # accumulate across calls rather than treating the first partial chunk as the whole line.
        ser = FakeSerial(chunks=[b'{"a":', b'1}\n'])
        obj = read_json(ser, timeout=1.0)
        self.assertEqual(obj, {"a": 1})

    def test_skips_a_stream_tap_row_and_returns_the_command_reply(self):
        # `stream tap` prints bare {"t":"fused",...} (or "event"/"status") rows from another task,
        # which can land on the wire between a command and its own JSON reply. read_json must not
        # mistake one for the reply it's waiting for.
        rx = b'{"t":"fused","v":1}\n' b'{"test":"link","pass":true}\n'
        ser = FakeSerial(rx=rx)
        obj = read_json(ser, timeout=1.0)
        self.assertEqual(obj, {"test": "link", "pass": True})

    def test_accept_rows_returns_the_tap_row_itself(self):
        ser = FakeSerial(rx=b'{"t":"fused","v":1}\n')
        obj = read_json(ser, timeout=1.0, accept_rows=True)
        self.assertEqual(obj, {"t": "fused", "v": 1})


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

        self.assertEqual(bytes(ser.written), b"dc log info\r")
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
        expected = ("flash stage %d %s\r" % (len(data), sha_hex)).encode("ascii") + data
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
        self.assertEqual(bytes(ser.written), b"flash push\r")

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


class TapStartTest(unittest.TestCase):
    """_tap_start(ser, on_line) is the piece _cmd_stream_tap calls to enable the tap and recover
    any rows that raced ahead of the "devkit> " prompt send_cmd waits for (cmd_stream.c sets
    s_tap_on = true before printing the "OK tap on ..." ack, so a row can land on the wire between
    the ack and the prompt -- inside send_cmd's own reply text -- rather than arriving later
    through the normal row loop)."""

    def test_returns_a_row_that_raced_ahead_of_the_prompt(self):
        # The reviewer's exact repro: one tap row lands between the ack and the next prompt.
        rx = b'stream tap on fused\r\nOK tap on fused\r\n{"t":"fused","v":1}\r\ndevkit> '
        ser = FakeSerial(rx=rx)

        rows = _tap_start(ser, "stream tap on fused")

        self.assertEqual(rows, ['{"t":"fused","v":1}'])

    def test_raises_on_err_ack(self):
        rx = b"stream tap on\r\nERR busy\r\ndevkit> "
        ser = FakeSerial(rx=rx)

        with self.assertRaises(DevkitError) as ctx:
            _tap_start(ser, "stream tap on")
        self.assertIn("busy", str(ctx.exception))


class ShellCloseTest(unittest.TestCase):
    """_shell_close(ser) is the piece _cmd_shell calls on Ctrl-C / any other exception out of its
    relay loop, so the device's `lt shell` bridge is told to close (rather than sitting open until
    its own 10 min cap) even when the host side exits ungracefully."""

    def test_sends_cr_escape_and_returns_true_on_bridge_closed(self):
        # The leading '\r' guarantees at_line_start on the device (cmd_shell.c honours '~' only
        # at the start of a line) even when _shell_close fires mid-line, e.g. Ctrl-C between
        # keystrokes after some non-newline bytes have already been relayed.
        ser = FakeSerial(rx=b"\r\nbridge closed\r\ndevkit> ")

        result = _shell_close(ser, timeout=1.0)

        self.assertTrue(result)
        self.assertEqual(bytes(ser.written), b"\r~.")

    def test_returns_false_on_timeout_when_bridge_closed_never_arrives(self):
        ser = FakeSerial(rx=b"")   # device never replies

        result = _shell_close(ser, timeout=0.05)

        self.assertFalse(result)
        self.assertEqual(bytes(ser.written), b"\r~.")   # the escape is still sent regardless


class NoStrayLineFeedTest(unittest.TestCase):
    """C1/M4: the REPL UART is ESP_LINE_ENDINGS_CR -- ENTER is a bare '\\r'; a following '\\n' is
    left as a stray byte in the UART driver's ring and (for `flash stage`) becomes image byte 0.
    Every command line devkit.py writes must therefore end in '\\r' alone, never '\\r\\n' -- this
    exercises send_cmd, stage_image and push_and_wait together and asserts no write, anywhere,
    ever contains a '\\n' byte."""

    def test_no_write_contains_a_line_feed(self):
        fd, path = tempfile.mkstemp(prefix="devkit_test_")
        os.close(fd)
        try:
            data = b"hello world"
            with open(path, "wb") as f:
                f.write(data)

            ser = FakeSerial(rx=(
                b"OK info\r\n" b"devkit> "
                b"STAGE-READY\r\n" b"STAGE-END 0x0000\r\n"
                b"flash result 0x0000\r\n"
            ))
            send_cmd(ser, "dc log info", timeout=1.0)
            stage_image(ser, path)
            push_and_wait(ser, timeout=1.0)

            self.assertNotIn(b"\n", bytes(ser.written))
        finally:
            os.remove(path)


class PollErrLineTest(unittest.TestCase):
    """_poll_err_line(ser) is the non-blocking peek `stage_image`'s upload loop uses between
    chunks (dk:M8). It must never block: it returns None at once when nothing is waiting, rather
    than paying a read timeout, and -- when bytes ARE already waiting -- returns the line
    ANSI-stripped and trimmed, exactly like every other line reader in this module."""

    def test_returns_none_when_idle_else_the_ansi_stripped_trimmed_line(self):
        self.assertIsNone(_poll_err_line(FakeSerial(rx=b"")))

        ser = FakeSerial(rx=b"\x1b[0;31mSTAGE-ERR write\x1b[0m\r\n")
        self.assertEqual(_poll_err_line(ser), "STAGE-ERR write")


class ClassifyPushLineTest(unittest.TestCase):
    """dk:M9 -- cmd_flash.c answers `ERR nothing staged`, `ERR nothing staged (claim expired or
    taken by /api/flash)`, `ERR push (image discarded, re-stage)` and `ERR push timeout` through
    flash_err(). push_and_wait skipped every one of them as noise and blocked the full
    PUSH_TIMEOUT_S, then reported "timeout waiting for flash result" -- the wrong diagnosis, and
    the recovery guidance ("re-stage", not "retry push") was lost."""

    def test_classifies_each_shape(self):
        self.assertEqual(classify_push_line("pushing 42%"), ("progress", "pushing 42%"))
        self.assertEqual(classify_push_line("flash result 0x0000"), ("result", "0x0000"))
        self.assertEqual(classify_push_line("flash result link -4"), ("result", "link -4"))
        self.assertEqual(classify_push_line("ERR push (image discarded, re-stage)"),
                         ("err", "push (image discarded, re-stage)"))
        self.assertEqual(classify_push_line("I (123) tag: noise"), (None, None))


class PushErrTest(unittest.TestCase):
    def test_err_line_raises_at_once_with_the_device_message(self):
        ser = FakeSerial(rx=b"ERR nothing staged\r\n")
        with self.assertRaises(DevkitError) as ctx:
            push_and_wait(ser, timeout=5.0)
        self.assertIn("nothing staged", str(ctx.exception))
        self.assertNotIn("timeout", str(ctx.exception))


class StageAbortTest(unittest.TestCase):
    """dk:M8 -- on an early STAGE-ERR the host kept pushing the remaining ~55 s (600 KB) of image,
    whose tail then landed on the REPL line editor as command characters."""

    def test_stops_sending_on_an_early_stage_err(self):
        fd, path = tempfile.mkstemp(prefix="devkit_test_")
        os.close(fd)
        try:
            with open(path, "wb") as f:
                f.write(b"A" * (4 * 4096))                 # four CHUNKs
            ser = FakeSerial(rx=b"STAGE-READY\r\n" b"STAGE-ERR write\r\n")
            with self.assertRaises(DevkitError) as ctx:
                stage_image(ser, path)
            self.assertIn("STAGE-ERR write", str(ctx.exception))
            self.assertLess(len(ser.written), 2 * 4096)     # the command line + at most one chunk
        finally:
            os.remove(path)


class ProbeTest(unittest.TestCase):
    """drv:M9 -- auto-picking the first /dev/cu.usbserial-* can select the LAP-TIMER (the
    -0001/-3 assignment swaps between re-plugs), and opening that port inside the 35 s OTA trial
    window rolls the image back."""

    def test_accepts_a_devkit_status_object(self):
        ser = FakeSerial(rx=b'{"version":"1.2","uptime_s":5,"link":{"connected":true}}\r\n')
        self.assertTrue(probe_is_devkit(ser, timeout=1.0))
        self.assertEqual(bytes(ser.written), b"dc status --json\r")

    def test_rejects_the_lap_timer_console(self):
        ser = FakeSerial(rx=b"Unrecognized command\r\nlaptimer> ")
        self.assertFalse(probe_is_devkit(ser, timeout=0.2))

    def test_rejects_silence_and_a_foreign_json_object(self):
        self.assertFalse(probe_is_devkit(FakeSerial(rx=b""), timeout=0.05))
        self.assertFalse(probe_is_devkit(FakeSerial(rx=b'{"proto":1,"state":0}\r\n'), timeout=1.0))


class TrialWindowNoteTest(unittest.TestCase):
    def test_note_states_the_window_and_the_consequence(self):
        for note in (trial_window_note(), ota_trial_window_note()):
            self.assertIn("35", note)
            self.assertIn("0x0806", note)


class OtaPushTest(unittest.TestCase):
    """drv:M8/M9 -- ota_push.py's own helpers, driven by the same FakeSerial."""

    def test_probe_laptimer_sees_the_prompt_and_refuses_a_devkit(self):
        self.assertTrue(probe_laptimer(FakeSerial(rx=b"\r\nlaptimer> "), time.monotonic() + 1.0))
        self.assertFalse(probe_laptimer(FakeSerial(rx=b"\r\ndevkit> "), time.monotonic() + 0.2))

    def test_stream_image_writes_exactly_the_image_with_and_without_pacing(self):
        data = b"B" * (2 * 4096 + 7)
        ser = FakeSerial()
        stream_image(ser, data, pace_ms=0)
        self.assertEqual(bytes(ser.written), data)
        ser = FakeSerial()
        stream_image(ser, data, pace_ms=1)                  # 1 ms: two pauses, still exact bytes
        self.assertEqual(bytes(ser.written), data)


if __name__ == "__main__":
    unittest.main()
