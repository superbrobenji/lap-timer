# Dev Controller (sub-project B) — Architecture Design

**Status:** design, 2026-09-20 (rev 2, after a code-grounded spec review). Sub-project B of the
two-device connectivity architecture (`2026-09-20-connectivity-dev-controller-design.md`).
Sub-project A (lap-timer prod link) is merged (#55); this design turns A's shipped serial interface
into a usable dev controller. Its own implementation plan follows (writing-plans).

## 1. Goal

A separate, removable ESP32 "dev controller" that plugs into the lap-timer over a UART link and
gives the developer a browser-based tool — over the dev controller's own WiFi — to tune/change
settings, offload lap-session data, live-monitor + black-box-log the lap-timer while it runs (saved
for post-field-test analysis), and flash new firmware. Powered from the lap-timer, hot-swappable,
field- and bench-capable. It becomes the lap-timer's sole control/flash path only later, in
sub-project C (out of scope here).

## 2. Context and constraints

- **Depends on** sub-project A's shipped serial interface (merged, #55): the `export_serial` console
  (§18.4) — text commands, `---BEGIN <name> <size>---\n<body>\n---END <crc32hex>---` framed
  responses (Base64 body for binary formats, raw text for JSON), the unsolicited `0xFF` live stream,
  and `ota recv` for cmd-OTA flashing. **B drives this as an automated console client; it invents no
  new protocol** (except the small, explicitly-listed A-side robustness additions in §5a).
- **Hardware:** a classic ESP32 (owned; ~4 MB flash, ~520 KB SRAM, usable heap ~200–290 KB after the
  WiFi stack). No native USB — B is itself flashed/consoled over **its own UART0** via a USB-UART
  adapter. **The link to the lap-timer therefore uses a *second* UART on B (UART1)** — UART0 stays
  free for B's own console/flash and B's boot log must not leak into the lap-timer's REPL.
- **Powered from the lap-timer**, removable, hot-swappable, ~150–250 mA (Plan 6 sizes this as a
  separate bench/field mode). B's own draw pulls the shared battery down — relevant to the OTA
  precondition (§10).
- **Second firmware**, out of the lap-timer's strict Power-of-10 scope; holds a **pragmatic
  P10-inspired** standard (§8).

## 3. Architecture (Approach 1: static SPA + REST-over-cmd bridge)

Everything runs on the ESP32; the client is only a stock **web browser** (nothing installed, no
host tool). Layers:
1. **WiFi AP** — SoftAP, fixed IP (`192.168.4.1`), WPA2 with a set password. MVP is AP-only (STA is
   only for the deferred GitHub-fetch, §9).
2. **HTTP server** (`esp_http_server`) — serves the static SPA (HTML/JS/CSS from a read-only
   LittleFS partition) + a JSON REST API. **Long-lived handlers (SSE live monitor, `/api/flash`, a
   session download) MUST run as async/worker-offloaded requests** (`httpd_req_async_handler_begin`,
   IDF ≥5.1, plus a small worker task) so they do not block the httpd's single request task —
   otherwise an open SSE stream would freeze every other request. Bound the concurrent-request
   count.
3. **cmd-host (`linkhost`)** — the UART1 client to the lap-timer (§5).
4. **log store (`logstore`)** — persists the demuxed live stream to B's flash, bounded + rotating,
   downloadable (§6, §7).

The lap-timer already emits JSON for the query ops, so B is a **thin bridge** (mostly relays JSON to
the browser; the SPA renders) — keeping per-request RAM/CPU low on the constrained chip.

## 4. MVP feature set and deferrals

**MVP:** device status · config get/set (tuning + settings) · session list + download (offload) ·
live monitor + black-box logging · firmware upload → cmd-OTA flash.

**Deferred within B (own later sessions):** `esp-serial-flasher` + GPIO0/EN boot pins (ROM recovery
/ the C sole-path foundation) · GitHub-release fetch (STA + HTTPS/mbedTLS).

## 5. The lap-timer interface B drives (detail)

- **Request/response:** B writes `<cmd> <args>\r` on UART1 and reads the framed response
  (`---BEGIN <name> <size>---\n`, `<size>` body bytes — Base64-decode for binary formats — then
  `---END <crc32hex>---`), verifying the CRC over the decoded body. One request in flight (§18.1);
  timeout + bounded retry guards a stalled/absent lap-timer.
- **The wire also carries non-framed text the demux MUST tolerate/skip:** linenoise **echoes** each
  typed command character, the `laptimer> ` prompt, INFO-level `ESP_LOG` lines emitted between
  transfers, non-framed error lines (`ERR 0x%04x: <msg>`), the `ota recv` tokens
  (`OTA-READY` / `OTA-END 0x%04x` / `OTA-ERR <timeout|read|usage|badsize|badsha|0x%04x>`), and the
  **ROM/boot log** after an OTA reboot. `linkhost` filters these; it does not treat them as data.
- **Live stream:** between commands the lap-timer emits `0xFF | seq u16 | flags u8 | payload`, where
  payload = a `SES_T_*` type byte + a fixed-size record struct (fused-sample / event, ≤64 B). The
  frame is **not length-prefixed today**, so B must know each type's size. See §5a — this plan adds
  a length byte on the A side; until then B carries a type→size table sourced from
  `core/types.h`/`event.h`/`ses.h`. **The demux is length-aware, never a scan for the next `0xFF`**
  (payload bytes can be `0xFF`). An unknown type byte → drop to a re-sync state and wait for the next
  clean boundary; host-tested.
- **STATUS is binary**, not JSON: `status` returns the §18.2 **20-byte record** (Base64). B decodes
  it via the shared record layout (§5a) — so even the scaffold's status display needs this decoder.
- **`config set` is line-capped:** the console's `max_cmdline_length` is 256 B, but `config get`
  returns ~939 B. `POST /api/config` therefore **diffs against the current config and sends only the
  changed keys, minified, ≤~250 B per `config set` line** (multiple lines if needed); JSON quotes are
  `\"`-escaped. (If a single value can exceed the cap, §5a's chunked-config is required.)
- **Download semantics:** `open <id> <fmt>` emits the *whole* framed file; `read <offset>` is a
  *resume* of an interrupted transfer, not a chunk loop. The CRC arrives in `---END` *after* the body
  has already streamed to the browser, so a CRC mismatch is handled by **aborting the HTTP socket**
  (the browser sees a failed/truncated download) rather than a pre-validated body.
- **Flash:** `ota recv <size> <sha256hex> <ver> <hwid>` → wait `OTA-READY` → stream the raw image →
  `OTA-END 0x0000` (success → lap-timer reboots) or `OTA-ERR …`. `tools/ota_push.py` (shipped in A)
  is the reference exchange. The lap-timer's `ota recv` **aborts after ~9 s without bytes**, so B
  must feed it from a stable source — see §7 (stage-then-push).
- **DEVUX note:** `ota recv`/`dbg` are DEVUX-gated on the lap-timer (present in A/B dev builds).
  Sub-project C replaces this text console with a hardened authenticated binary channel; B's cmd-host
  is extended against that in C (not this plan).

## 5a. Lap-timer-side (A) changes this plan makes

A shipped a *human* console; making it robust for a *machine* peer warrants three small, additive
lap-timer changes (done as lap-timer commits within B's plan — they don't alter existing behavior):

1. **Length-prefix the `0xFF` stream frame** — add a `len u8` after `flags` (frame becomes
   `0xFF|seq|flags|len|payload`). Removes B's dependency on a type→size table and makes the demux
   robust to future record changes. **Recommended / near-required** for a reliable black-box log.
2. **A shared protocol header** — lift the framing markers, the `0xFF` frame layout, the §18.2 status
   record layout, and the command/format names into a header both firmwares include (the constants
   live only as `printf` literals in `export_serial.c` today). Single source of truth; makes B's
   **contract test** real instead of golden-fixture-only. **Recommended.**
3. **`config set` for >256 B** *(only if needed)* — if diff-minification (§5) cannot keep every
   settable field under the line cap, add a chunked `config set` (append-then-commit) on the
   lap-timer. Decide during session 3 when the real config surface is known.

These are the only lap-timer edits; everything else is B-side.

## 6. Components and flash layout (`devcontroller/`)

A sibling ESP-IDF project at the repo root, `devcontroller/`, own `main` + components. `littlefs`
is a managed component (`joltwallet/littlefs`), as on the lap-timer.

- **`main`** — `app_main`: WiFi AP, mount LittleFS (www + logs), start `linkhost` (UART1), start the
  HTTP server + worker task, wire routes.
- **`linkhost`** — UART1 cmd-host. Interface (illustrative): `linkhost_cmd(cmd, out)` (run one
  command → parsed/validated frame or a typed error), a demuxed-stream ring the webapi + logstore
  read, `linkhost_flash(...)` (stage-then-push OTA), `linkhost_peer_present()`. Internals: request
  framing, response parser (`---BEGIN/END---` + Base64 + CRC), the tolerant length-aware demux, the
  binary STATUS decoder, timeout/retry, re-sync. Bounded + asserted (pragmatic P10).
- **`webapi`** — `esp_http_server` URI handlers + static SPA serving; long-lived ones offloaded to
  the worker task. Endpoints: `/api/status`, `/api/config` (GET/POST), `/api/sessions`,
  `/api/session/<id>?fmt=`, `/api/stream` (SSE), `/api/flash` (POST multipart), `/api/logs`,
  `/api/log/<id>`.
- **`logstore`** — append demuxed stream records to a bounded, rotating log on the logs partition;
  list + read for download.
- **`web/`** — vanilla SPA (no build toolchain), packed into the www LittleFS image at build time.

**Flash budget (4 MB), partition table:** `nvs` (24 K) · `phy_init` (4 K) · **`factory` app**
(~1.5 M — B's own firmware) · **`www`** LittleFS, read-only SPA assets (~256 K) · **`ota_stage`**
raw partition for a staged lap-timer image (~1.25 M — sized to the OTA slot max; real images are
~525–590 KB today) · **`logs`** LittleFS, rotating black-box logs (remainder, ~0.9–1 M). Reflashing
B's app or www does not touch `logs`. **Log budget:** a stream record ≈ `4 (frame) + ~68 (record)` B
at ~10 Hz ≈ **~2.6 MB/h**, so a ~1 MB log partition holds **~20–25 min** of continuous capture; the
`logstore` cap + rotation keep the newest. *(If field sessions need longer unbroken logs, an 8/16 MB
ESP32 module — or dropping on-B staging in favor of browser-chunked upload with resume — buys more
`logs` space; flagged for review, §15.)*

## 7. Data flow

- **Config:** `GET /api/config` → `config get` → relay JSON → JS form; `POST` → diff → one-or-more
  minified `config set` lines.
- **Sessions:** `GET /api/sessions` → `list` (relay §14.3 JSON); `GET /api/session/<id>?fmt=vbo` →
  `open <id> vbo` → stream the decoded body to the browser as a download; CRC checked at `---END`,
  mismatch → socket abort.
- **Live monitor:** `GET /api/stream` (SSE, async handler) → subscribe to `linkhost`'s stream ring →
  emit each decoded record as an SSE event → JS updates the monitor; `logstore` persists in parallel.
- **Flash (stage-then-push):** `POST /api/flash` streams the uploaded `.bin` into the **`ota_stage`**
  partition (with a running SHA-256), decoupling the WiFi transfer from the UART; only once the whole
  image is staged does B run the `ota recv` exchange feeding from flash at a steady rate (never
  starving the lap-timer's ~9 s abort). A failed WiFi upload is retryable without touching the
  lap-timer. On `OTA-END 0x0000` the lap-timer reboots → link drops → the UI polls `/api/status`
  until it answers ("rebooting… reconnected").
- **Logs:** `GET /api/logs` (list + sizes) · `GET /api/log/<id>` (download).

## 8. Coding standard (pragmatic P10-inspired)

B's own logic (`linkhost` parser/demux/state machines, `logstore`, the webapi bridge glue): bounded
loops with explicit caps, ≥2 assertions in real functions, small focused modules, no fn-pointer soup.
The network layer (WiFi, `esp_http_server`, later mbedTLS) uses the framework — dynamic allocation
allowed. **No blocking P10 linter** for B; gate = host tests + review.

## 9. Versioning (independent of the lap-timer)

B versions on its own tag namespace `dc-v*` (`git describe --tags --match "dc-v*" --dirty --always`
→ `CFG_DC_VERSION`). The lap-timer's `--match "v*"` and B's `--match "dc-v*"` do not overlap, so the
frequent lap-timer `v*` releases never bump B's version. B's plan session tags use a distinct prefix
(e.g. `p5.5-d*`).

## 10. Security phasing

A/B prototype: **signed images only** — the lap-timer's cmd-OTA verifies the §19 release signature
and rolls back a bad image, so an image B pushes cannot brick or run unsigned; B holds no signing key
(it relays a developer-supplied signed image). B's AP is a local WPA2 bench/field network, not
internet-exposed in the MVP. Full hardening (secure boot, flash encryption, authenticated sole-path
channel) is sub-project C. **UI:** since B draws from the shared battery, surface the OTA battery
precondition (`E_OTA_PRECOND`, ≥3800 mV or charging) as a "charge first" hint before a flash.

## 11. Error handling and hot-swap

- **Link errors:** UART timeout / lap-timer absent → typed `linkhost` error → API 503 (not
  connected) / 504 (timeout) → UI banner. B keeps serving its UI with no lap-timer attached.
- **Hot-swap:** attach/detach while B runs (a detect line + a periodic `status` heartbeat at **< 3 s**
  cadence — `LINK_PEER_TIMEOUT_MS` — to keep the lap-timer's stream flowing during monitoring; the
  GPIO detect line lands with the Plan-6 connector, heartbeat-only until then). Detach → in-flight
  requests fail cleanly, no hang; attach → `linkhost` drains stale UART + re-syncs the demux. No
  crash/hang on insert/remove.
- **Black-box log gap during transfers:** the lap-timer emits the stream only *between* framed
  commands (and its M1 TX mutex suppresses the stream during any framed transfer), so the log has a
  **gap for the duration of every `open`/`read`/`ota recv`** and a brief blip on each heartbeat.
  Acceptable — monitoring is normally idle-of-commands; stated so no one expects gap-free capture. (A
  lap-timer-side stream buffer to close the gap is possible but out of scope.)
- **Flash:** `OTA-ERR` codes surfaced with meaning; success → reboot → link drop → poll-to-reconnect.
  One flash at a time; other commands blocked during it.
- **Log store bounds:** configurable byte cap, oldest-first rotation.
- **WiFi AP:** clients come/go freely; dead SSE clients reaped; robust to disconnects.

## 12. Testing

- **Host tests** (B's own logic): the response frame parser (`---BEGIN/END---` + Base64 + CRC); the
  demux state machine — fed a **mixed byte stream incl. echo/prompt/log-lines/non-framed-errors + a
  length-prefixed stream frame + an unknown type byte**, assert correct split + re-sync; the STATUS
  record decoder; `logstore` rotation/bounds; the `config set` diff/minify.
- **Contract test:** against the shared protocol header (§5a #2) — B's framing/opcode/stream/status
  assumptions equal the lap-timer's; golden fixtures (captured real responses) as a backstop.
- **On-target integration (flash gates, developer present):** flash B to the 2nd ESP32, wire UART1↔
  the lap-timer's UART0, drive via a browser + a scripted HTTP client — config round-trip, a session
  download byte-compared to a direct console pull, an SSE live-monitor capture, a `/api/flash`
  stage-then-cmd-OTA of a signed image the lap-timer applies.
- **CI:** B builds as its own IDF project; B's host tests run in CI. No blocking P10 linter for B.

## 13. Sequencing (implementation sessions — the plan details these)

1. **Scaffold** — `devcontroller/` project + `dc-v*` versioning + partition table; WiFi AP; httpd
   serving a static page + `/api/status` (needs the STATUS binary decoder). The A-side shared
   protocol header + stream length-prefix (§5a #1,#2) land here as the foundation both sides use.
2. **`linkhost` core** — framed request/response, the tolerant length-aware demux, the stream
   consumer; host tests (parser + demux + status decoder).
3. **Web API + SPA (CRUD)** — config get/set (diff/minify; add chunked `config set` §5a #3 iff
   needed), session list + download, status; the vanilla-JS SPA. Async/worker offload for downloads.
4. **Live monitor + `logstore`** — async SSE stream + bounded persistent logging + download.
5. **Flash** — `/api/flash` upload → `ota_stage` → cmd-OTA; flash-gate a signed image applied by the
   lap-timer.
6. **Later phases (own sessions):** `esp-serial-flasher` + boot pins; GitHub-fetch (STA + TLS).

## 14. Roadmap placement

Sub-project B ("Plan 5.5"): after Plan 5 (A, done), **before Plan 9** (B's black-box logging aids
field validation). The connector (UART TX/RX, power, GND, detect — MVP; + GPIO0/EN when
esp-serial-flasher lands) and B's ~150–250 mA draw feed Plan 6 (Power). B can prototype over jumper
wires ahead of the formal connector.

## 15. Open questions / risks

- **Log capacity vs staging (flash budget, §6):** ~1 MB `logs` ≈ ~20–25 min continuous. If field
  runs need longer unbroken capture, options are an 8/16 MB module (relieves everything) or
  browser-chunked upload with resume (frees the `ota_stage` 1.25 MB for logs, at the cost of a more
  complex uploader). **Flagged for review** — pick before the plan sizes partitions.
- **httpd concurrency:** async handlers + one worker on the classic ESP32 must sustain the SPA + an
  SSE stream + a background flash without RAM starvation; validated in sessions 3–5.
- **`config set` surface:** whether every tunable fits under the 256 B line via diff/minify, or needs
  §5a #3 — resolved in session 3.
- **Demux against real chatter:** the tolerant demux must survive echo/prompt/logs/boot-log/partial
  frames on attach — the main correctness risk, covered by host tests + the attach/detach gate.

## 16. Non-goals / YAGNI

No STA/internet/GitHub-fetch in the MVP · no `esp-serial-flasher`/ROM-recovery in the MVP · no secure
boot/flash-encryption/authenticated channel (sub-project C) · no microSD · no SPA framework or build
toolchain (vanilla JS). The flash upload **is** staged to flash (§7) — chosen over pure streaming for
robustness against WiFi stalls.
