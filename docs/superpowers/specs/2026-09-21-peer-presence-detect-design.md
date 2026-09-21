# Lap-timer ↔ dev-controller peer presence + stream gating (2026-09-21)

**Status:** design, approved in brainstorming 2026-09-21. Supersedes the interim
cmd-heartbeat presence gating.

**Scope:** cross-device (lap-timer `components/app/link` + dev-controller `devcontroller/`).
Architectural — it makes a hardware **detect line** part of the two-device interface contract.

## 1. Problem

The lap-timer emits its `0xFF` fused/event stream (spec §18) only while `link_peer_present()`
is true. Today, with no connector hardware, presence is asserted **only** by a `cmd` received
within a 3 s window (`LINK_PEER_TIMEOUT_MS`) — so the dev controller must **continuously poll** the
lap-timer (a "heartbeat") to keep the stream, and thus its black-box logging, alive.

This interim is a bandage and is failing in practice:
- The dev-kit's heartbeat was implemented as a **blocking** `linkhost_status()` call (waits for a
  framed reply, retries on a flaky link) → the effective interval balloons past 3 s → the peer
  lapses → **sporadic stream** and **dropped events** (rare EVENT records fall in the gaps while
  frequent FUSED samples don't).
- **Black-box logging only runs while a browser is open** (the SPA's status poll is the only
  heartbeat) — defeating the "capture now, offload later" purpose of the dev controller.

`link.c` already anticipates the real mechanism: a GPIO **detect line** (`LINK_DETECT_GPIO`,
currently `-1` = disabled, "Plan 6 connector hardware"). The fix is to enable it and remove the
dev-kit polling — the lap-timer should sense the dev-kit is attached and drive the stream itself.

## 2. Presence contract (hardware)

A dedicated **detect line** signals "a dev controller is attached":

- **Lap-timer pin:** `GPIO4` — free on the DevKit V1 (not in the board GPIO map), internal-pull-up
  capable (unlike input-only GPIO34–39), non-strapping, exposed on the 30-pin header. Configured
  as **input with internal pull-up**.
- **Active-low:** the pin **idles high** (internal pull-up) = **absent**; a connected dev controller
  pulls it to **GND = present**. This matches `link.c`'s existing `link_poll_detect()` comment and
  logic (`level == 0` → asserted).
- **Dev-controller side is passive:** the detect line is tied to the dev-kit's **GND** through a
  **dedicated** connector pin/wire — **separate** from the power/UART ground. No dev-kit firmware or
  GPIO is involved in presence.
- **Unplug = the wire physically separates** (jumper removed, or connector unmated), so the
  lap-timer pin floats back to its pull-up (high) = absent. Because the detect pin is a *dedicated*
  net (not the shared power ground), removing the detect wire alone changes the reading even if the
  power/UART ground remains.

**Wiring now (jumper bench):** add ONE jumper — lap-timer **GPIO4 → dev-controller GND** (any GND
pin, but treated as a separate detect wire). **Plan 6** formalizes this as the connector's detect
pin; this design is that pin pulled forward and is forward-compatible.

## 3. Lap-timer firmware changes (`components/app/link`)

The detect-line code path in `link.c` already exists behind `#if LINK_DETECT_GPIO >= 0`
(`link_poll_detect`, the `gpio_config` in `link_start`, and the `s_detect_asserted` branch of
`link_serial_present`). Enable and harden it:

1. **Assign the pin:** define `LINK_DETECT_GPIO` = `4` (via `build_config.h`/a board-scoped define,
   not a raw literal buried in `link.c`, so a future connector revision or a different board can
   override it). The existing `gpio_config` (input, pull-up, no interrupt) and 20 ms poll
   (`LINK_POLL_MS`, in the drain loop) then take effect unchanged.

2. **Debounce** the raw level so a bouncy jumper/connector doesn't flap the stream on/off:
   require the level to be **stable for N consecutive polls** (N = 3 → ~60 ms at 20 ms) before
   changing `s_detect_asserted`. Implement inside `link_poll_detect()` with a small run-length
   counter (drain-task-only state, no locking).

3. **Make the detect line definitive when a detect pin is configured.** `link_serial_present()`
   returns the detect line alone when `LINK_DETECT_GPIO >= 0`; the `cmd`-heartbeat path is the
   presence signal ONLY on boards with no detect pin (`LINK_DETECT_GPIO < 0`). Rationale (found at
   the 2026-09-21 flash gate, supersedes an earlier "keep the OR fallback" call): with the OR, an
   active console / dev-kit status poll keeps the cmd-heartbeat fresh and masks an unplugged detect
   line, so pulling the detect wire did not stop the stream. Detect-definitive makes presence purely
   LT-driven; the detect wire becomes required for streaming (it always is on the §6 connector).

No change to the stream framing, the ring, or `stream_push` — only what drives `s_detect_asserted`.

## 4. Dev-controller firmware changes (`devcontroller/`)

1. **Remove the `link_heartbeat` task** from `main/main.c` (the blocking-poll bandage added in
   commit `23cbac9`) and its `LINK_HEARTBEAT_MS` define. The lap-timer now drives presence; the
   dev-kit must not poll to keep the stream alive.

2. **No consumer change needed.** `stream_consumer` (drains `linkhost_stream_pop` → `logstore_append`
   + `webapi_stream_push`) already runs continuously; it simply now receives a continuous stream
   whenever the dev-kit is detected. Black-box logging becomes **autonomous** — it runs whenever the
   dev-kit is plugged in, with or without a browser open.

3. The SPA's 2 s `/api/status` poll stays (it's for the status header, not presence); it
   incidentally exercises the lap-timer's cmd-fallback but is no longer load-bearing for the stream.

## 5. Behavior

- **Dev-kit plugged in** (detect low, debounced): lap-timer streams fused/events on UART0 → dev-kit
  demuxes → logstore (persist) + SSE (live monitor). Continuous, no gaps → events delivered.
- **Dev-kit unplugged** (detect high, debounced): lap-timer idles the stream (drain task sleeps,
  ring not fed) → no wasted UART/CPU/power, no console clutter. Dev-kit sees no data.
- **Hot-swap:** debounced transitions start/stop the stream cleanly on plug/unplug.

## 6. Edge cases

- **Connected but unpowered dev-kit:** the dev-kit is powered *from* the lap-timer, so a
  powered-lap-timer implies a powered dev-kit; a dead-but-connected dev-kit would still read as
  present (detect wire tied to GND) and the lap-timer would stream to a dead peer — harmless (a
  little wasted UART), not worth extra mechanism.
- **Shared ground:** the detect pin must be a *dedicated* net, not the shared power/UART ground, or
  it would always read low. The connector/jumper provides a separate detect pin (see §2).
- **Console coexistence:** while present, the stream shares UART0 with the console (unchanged;
  the M1 UART0-TX mutex keeps framed responses intact). Prod has no interactive console.

## 7. Testing

- **Host:** if the debounce is factored into a small pure helper, unit-test the state transitions
  (flapping input → stable output). Otherwise it is covered on-device.
- **On-device flash gate (both boards, user present):**
  1. Detect wire connected: stream flows; **without any browser open**, confirm the dev-kit's Logs
     grow (autonomous logging) — download + decode a log, confirm it contains records.
  2. Remove the detect wire: stream stops (dev-kit sees no new data) within the debounce window;
     reconnect: stream resumes.
  3. Confirm the live monitor is **steady** (no multi-second gaps) and that EVENT rows appear **iff**
     the lap-timer fires events (settles the separate "does the sim fire events" question via the
     log decode).

## 8. Out of scope / separate

- **Whether the sim fires events at all** — a separate diagnostic (decode a captured log for
  `0x09` records). This design guarantees *delivery*, not event *production*.
- **The `.log` format should be an industry standard** — tracked as **#67**, a later format change.
- **The Plan 6 connector hardware** — this design is the detect pin pulled forward onto jumpers;
  Plan 6 formalizes the ~7-pin connector (UART0 TX/RX, GPIO0, EN, power, GND, **detect**).

## 9. Roadmap

Supersedes the cmd-heartbeat interim for sub-project A (`link`) and removes the dev-kit heartbeat
bandage (sub-project B). Lands as a small cross-device change on `p5.5-dev-controller`; the detect
pin becomes part of the Plan 6 connector contract.
