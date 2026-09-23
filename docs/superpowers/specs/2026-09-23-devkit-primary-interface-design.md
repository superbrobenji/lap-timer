# Dev-kit as Primary Dev Interface — Design (Plan 5.6)

**Status:** approved design, 2026-09-23. Supersedes the dev-controller-side status/heartbeat
semantics of `2026-09-20-dev-controller-B-design.md` §5/§11 and issue #68; extends
`2026-09-21-peer-presence-detect-design.md` (the lap-timer already drives presence).

## 1. Goal

Make the dev-kit (the `devcontroller/` firmware from Plan 5.5) the primary way a developer works on
the lap-timer, in two access modes that never require plugging into the lap-timer directly:

- **AP mode** (exists): the WiFi AP + SPA, for on-site/phone use.
- **USB mode** (new): a debug/test/flash channel over the dev-kit's own USB console, for bench and
  development work — no WiFi, no AP, no loss of the laptop's internet.

Under one architectural principle: **the lap-timer drives the dev-kit; the dev-kit listens.**
Heartbeat, status and telemetry are pushed by the lap-timer on its autonomous stream. The dev-kit
initiates only on-demand actions (config set, session offload, flash). Nothing polls the lap-timer.

The end state this builds toward is sub-project C: the lap-timer is the standalone unit (own
battery), its dev tooling is stripped, and the dev-kit is the sole control/flash path. Until C,
a direct USB connection to the lap-timer remains a fallback for flashing it.

## 2. The finding that motivated this

At the Plan 5.5 followups bench (2026-09-22), the new `stream_age_ms` metric (#68) exposed a
collision between two existing designs sharing the lap-timer's single UART0 and TX mutex:

- the **presence heartbeat**: the SPA polls `GET /api/status` every 2 s, which makes the dev-kit
  send a framed `status` command to the lap-timer;
- the **autonomous telemetry stream**: the lap-timer emits `0xFF` records at ~10 Hz between framed
  replies, suppressing the stream while it answers a framed command (M1 mutex).

Measured: with no polling the lap-timer streams 10.5 rec/s; under 2 s status polling it drops to
0.4 rec/s, because each `status` round-trip takes ~4 s (the dev-kit's `LINK_CMD_TIMEOUT_MS` 2000 ×
up to 3 attempts — the reply is not recognised on the first attempt). The root of the 4 s is not yet
known; the tooling in this design exists to find it (§9). Diagnosing it on the current bench was
impractical: the dev-kit is HTTP-only (status can only be triggered from the AP), the laptop loses
internet on the AP, one USB port cannot watch both consoles, and a direct `status` on the lap-timer
console collides with the dev-kit's idle-high TX. Those are capability gaps, not one-off bugs.

## 3. Principles

1. The lap-timer pushes; the dev-kit listens. No periodic requests from dev-kit to lap-timer.
2. Two access modes (AP, USB) over one shared dev-kit core; one behaviour, two front-ends.
3. Direct lap-timer USB stays a fallback only for flashing the lap-timer, until
   `esp-serial-flasher` lands and then sub-project C closes it.
4. Hot-swappable: the dev-kit is a board that plugs into the running lap-timer and is powered by it.
5. Lap-timer-side changes stay strict Power-of-10, DRAM-neutral, and byte-compatible with the
   existing `lt_proto.h` contract.

## 4. The contract: lap-timer → dev-kit push

### 4.1 `LT_REC_STATUS` on the existing stream

A new stream record carrying the existing 20-byte §18.2 STATUS payload (the exact bytes the framed
`status` command returns today):

```
0xFF | seq u16 LE | flags u8 | len = 21 | type = LT_REC_STATUS | 20 B STATUS (LT_ST_OFF_* layout)
```

- `LT_REC_STATUS = 0x40`, declared in `components/app/include/app/lt_proto.h` next to
  `LT_SES_T_FUSED`/`LT_SES_T_EVENT`, with a `_Static_assert` in `link.c` that it collides with no
  `SES_T_*` value in `core/ses.h`. The payload layout is the existing `LT_STATUS_LEN` +
  `LT_ST_OFF_*` block — no new layout.
- `status_build(uint8_t out[LT_STATUS_LEN])` is factored out of `cmd.c`'s `op_status` so the framed
  reply and the push share one builder (a host test pins the two byte-identical).
- **Cadence:** `link_task` (the existing 20 ms detect-debounce loop) pushes one STATUS record every
  `LINK_STATUS_PERIOD_MS = 1000` (every 50th tick), **plus one immediately when the detect line
  asserts** (debounced), so the dev-kit has fresh status within milliseconds of a plug-in.
- The record goes through `stream_push()` like fused/event: length-prefixed, demuxed on the dev-kit,
  dropped (not queued) if the serial sink cannot take M1 — the next tick resends.
- Bandwidth ~25 B/s. Lap-timer budget: ~40 lines in `link.c`/`cmd.c`, one `uint8_t` tick counter,
  a 20-byte stack scratch, no new statics (DRAM is at ~432 B free — measured at the gate).

### 4.2 What the dev-kit does with it

- The demux accepts `LT_REC_STATUS` (`stream_type_known`). The record flows like the others: into
  the black-box log, the SSE monitor (battery / free space / sessions become visible over time), and
  the NDJSON transcode (#67) gains a `{"t":"status",…}` row via `linkhost_status_decode`.
- `linkhost` caches the latest STATUS with a timestamp (`linkstats`, §5.1).
- `linkhost_status(out)` returns the cached record if it is younger than
  `LINK_STATUS_STALE_MS = 3000`, else `LINKHOST_E_NOTCONN`. **It never touches UART1.**
- `GET /api/status` is therefore a local read. The SPA's 2 s poll stays (it is cheap now).

### 4.3 Presence semantics

| field | meaning | source |
|---|---|---|
| `connected` | a STATUS record was received < 3 s ago (lap-timer present *and* alive) | status cache age |
| `status_age_ms` | ms since the last STATUS record (−1 = never) | linkstats |
| `stream_age_ms` | ms since the last FUSED record (−1 = never) | linkstats |
| `logging` | black-box store accepting appends | `logstore_ready()` |

- Detect pulled or lap-timer dead → no pushes → `connected:false` within 3 s (the old path needed
  3 × 2 s of timeouts). The lap-timer gates its whole stream (STATUS included) on the detect line, so
  "unplugged" reads as not connected — correct.
- `stream_age_ms` growing while `connected` is true now means exactly one thing: telemetry stopped
  with the link alive. No magic threshold is needed to avoid false idles because nothing suppresses
  the stream periodically any more; the SPA shows "stream idle" above 3 s.
- `503 {"connected":false}` is returned when the cache is stale, as today.

### 4.4 On-demand commands (unchanged shape)

`config get/set`, `list`, `open/read`, `delete`, `ota recv` keep the framed request/response path
(`linkhost_cmd` / `linkhost_download_cmd`), dev-kit-initiated, rare. The ~4 s framed-request
latency is **not** designed away here; §9 diagnoses and fixes it. The framed `status` command stays
on the lap-timer (used by `lt status` and by the transition-period console).

## 5. Dev-kit tooling: shared core, REPL, self-tests, CLI, flash

### 5.1 Shared core

`linkhost` + `logstore` are the core; `webapi` (HTTP) and the new console are front-ends that call
them. Additions to `linkhost`, in the pure host-testable module:

- **`linkstats`**: per-type record counters, sequence-gap detection (dropped frames), last-seen
  timestamps for STATUS/FUSED/EVENT, ring high-water, and the STATUS cache. Single source for
  `status_age_ms`/`stream_age_ms` (the ad-hoc `s_last_stream_ms` in `webapi.c` moves here).
- **link trace hooks**: optional timestamped logging of every request (bytes sent, per-attempt
  outcome, reply latency, and on failure a bounded hex dump of the received bytes).

Neither front-end owns state. Anything the console needs that only `webapi.c` has today is moved
down into the core, not duplicated.

### 5.2 The console (REPL) on the dev-kit's UART0

An `esp_console` REPL on the dev-kit's own USB UART, the same pattern as the lap-timer's console
(logs and prompt share the port; `dc log <level>` quiets logs). Every command accepts `--json` and
then emits exactly one JSON object (or NDJSON lines for `tap`/`trace`), line-atomic, so a host can
filter log lines by prefix.

| command | does |
|---|---|
| `dc status` | AP/clients, link (connected, status/stream age, per-type rates, gaps), logstore (ready, file, bytes), heap, uptime, version |
| `dc log <level>` | dev-kit log verbosity on this console |
| `dc baud <rate>` | switch this console's baud for a faster `flash stage` upload (optional optimisation; 115200 is the requirement) |
| `lt <console cmd…>` | relay any lap-timer console command through `linkhost` (framed or streaming download); prints the reply, round-trip ms, attempt count |
| `lt shell` | transparent byte bridge UART0↔UART1 until `~.` at line start or a 10 min cap; takes the link mutex, pauses the demux (as a download does), silences dev-kit logging; resyncs on exit |
| `link trace on\|off` | per-request trace (§5.1) |
| `stream stats` | rates per type over the last 10 s, seq gaps, ring high-water |
| `stream tap on\|off [type]` | print decoded records as JSON rows, rate-limited to 5/s |
| `selftest link [N=20]` | N framed `status` round-trips: latency min/median/max, attempts, failures; PASS if median < 100 ms, max < 500 ms, 0 failures |
| `selftest stream [s=10]` | measured rates: PASS if fused ≥ 8/s, status ≥ 0.8/s, 0 gaps |
| `selftest framing` | relay one command with trace on; PASS if every `---BEGIN`/`---END` marker is followed by exactly one CR before LF |
| `selftest all` | the three above; one verdict |
| `flash stage <size> <sha256hex>` | receive a raw image over this console into `ota_stage` (§5.4) |
| `flash push` | push the staged image to the lap-timer with the Task 6 `linkhost_flash` |
| `flash status` / `flash abort` | progress (`staged`, `pushing`, `flash_pct`, last result) / cancel |

### 5.3 Host CLI

`tools/devkit.py` (pyserial, beside `ota_push.py`): `devkit.py [--port P] status | lt <cmd…> |
trace | stream [stats|tap] | selftest [link|stream|framing|all] | flash <image.bin> | shell`.
It drives the REPL in `--json` mode and parses the objects; `selftest all` replaces the ad-hoc
bench scripts of the Plan 5.5 gates.

### 5.4 Flashing the lap-timer over USB

`devkit.py flash image.bin` = `flash stage` + `flash push`. The staging protocol over the dev-kit
console deliberately mirrors the lap-timer's own `ota recv` handshake so `ota_push.py`'s logic is
reused on the host:

```
host:    flash stage <size> <sha256hex>\r
dev-kit: STAGE-READY                      (ota_stage erased for size; console in raw mode)
host:    <size> raw bytes
dev-kit: STAGE-END 0x0000                 (SHA-256 matches; image parsed: ver/hwid via image_desc)
      |  STAGE-ERR <code>                 (badsize | badsha | timeout | write)
host:    flash push\r  →  progress lines / final result (same codes as /api/flash's flash_err)
```

`/api/flash` and the console share the staging core (`ota_stage` write + SHA + `image_desc`) and
the push. Works at 115200 baud (a 600 KB image ≈ 55 s per hop); the CLI may negotiate a higher
console baud (`dc baud`) for the upload if the dev-kit's USB bridge tolerates it — an optimisation,
not a requirement. This is cmd-OTA: it needs a signed image, a working lap-timer firmware, and the
§19.5 battery precondition. The ROM-bootloader path (`esp-serial-flasher` over EN/GPIO0, for a
bricked lap-timer; the sub-project C foundation) stays deferred; `flash` is shaped so `flash --rom`
plugs in later without redesign.

### 5.5 Out of scope (YAGNI)

A second HTTP-over-serial transport; log download over the console (use the AP); securing the
console (dev-only, physical access; C's authenticated channel covers the lap-timer link); a custom
dev-kit PCB (stock ESP32 devkit + RJ45 breakout until then).

## 6. Data flow

- **Status (push):** `link_task` 1 Hz → `status_build()` → `stream_push(LT_REC_STATUS)` → serial
  sink (drops if M1 held) → dev-kit `rx_task` → demux → ring → `stream_consumer` → linkstats cache +
  logstore + SSE. `/api/status` and `dc status` read the cache. Zero UART traffic per status read.
- **Command (on demand):** `/api/*` or `lt <cmd>` → `linkhost_cmd`/`linkhost_download_cmd` →
  UART1 → framed reply → demux/`lh_dl` → reply. `link trace` instruments each step. Downloads and
  flash pause the demux (the #65 park handshake); the stream gap during a transfer is accepted, as
  the Plan 5.5 spec already states.
- **Bridge:** `lt shell` takes the link mutex, pauses the demux, pumps bytes both ways, restores and
  drains-to-quiet on exit.
- **Flash over USB:** console raw-receive → `ota_stage` (+SHA) → `linkhost_flash` push → lap-timer
  `ota recv` → apply/reboot → `connected` drops then returns with the new `fw`.

## 7. Edge cases and error handling

- Lap-timer absent or dead: `connected:false` within 3 s; framed commands still fast-fail on the
  absent path (`LINK_ABSENT_TMO_MS`).
- STATUS push dropped under M1: the next 1 s tick resends; staleness is 3 s, so two consecutive
  drops are invisible.
- Link busy (download, flash push, or bridge holds the mutex): `/api/*` and `lt` get BUSY via the
  bounded mutex take → 503 / `busy`, as today. `dc status` never blocks (no mutex).
- Bridge safety: `~.` escape + the 10 min cap; on exit the demux is drained and resynced.
- Console and logs interleave on UART0: accepted (lap-timer precedent); `dc log` quiets; `--json`
  output is line-atomic.
- Ring overflow under a burst: drop-newest as today, now counted (seq gaps) and visible in
  `stream stats`.
- Flash staging over the console: raw mode is bounded by `<size>` and a per-read stall timeout;
  any error returns the console to the REPL and leaves `ota_stage` marked unstaged.
- Lap-timer DRAM/stack: `link_task` stack high-water and free DRAM are recorded at the 5.6.1 gate;
  the change must be DRAM-neutral.

## 8. Physical interface (the connector contract)

End state: the lap-timer is the standalone unit with its own battery (Plan 6); the dev-kit is a
board that plugs into it, is **powered by it**, and is hot-swappable. Today's bench (dev-kit on USB
feeding the lap-timer over a 5 V jumper) is an interim until Plan 6, and is why the OTA slot erase
browned out the lap-timer on the single-USB bench.

### 8.1 Connector: RJ45 (8P8C), standard straight-through Ethernet patch cable

The port lives **outside** the lap-timer enclosure (plug in without opening the case), not sealed
or heavy-duty. RJ45 gives exactly 8 contacts, a positive latch, ~1 A per contact, panel-mount jacks
(gasketed variants and boots available), and passive off-the-shelf cables of any length with no
crimping. The same pin numbers are used on both jacks.

| RJ45 pin | signal | dir | pair rationale (T568B: 1-2, 3-6, 4-5, 7-8) |
|---|---|---|---|
| 1 | LT_TX (lap-timer UART0 GPIO1) | LT→DK | UART pair; fine at 115200 over ≤ 2 m |
| 2 | LT_RX (lap-timer UART0 GPIO3) | DK→LT | |
| 3 | LT_EN | DK→LT, open-drain | boot-strap pair; idle open (lap-timer pull-ups), driven only during a recovery flash |
| 6 | LT_GPIO0 | DK→LT, open-drain | |
| 4 | 5V_DK | LT→DK | power pair twisted with its return — lowest loop inductance for WiFi bursts |
| 5 | GND | — | |
| 7 | GND | — | second ground |
| 8 | DETECT (lap-timer GPIO4) | DK→LT | dev-kit grounds it; lap-timer pull-up; works unpowered; twisted with GND |

Current: ~0.4 A peaks on one 24 AWG conductor each way over a 1.5 m lead ≈ 0.26 Ω loop ≈ 0.1 V
drop — fine for the dev-kit's 5 V→3.3 V LDO. Keep leads 0.5–1 m; shielding not needed.

**Rules:** straight-through cables only (a crossover swaps the UART pair onto EN/GPIO0). **Never
plug the port into real Ethernet**: pins 4/5 are the PoE mode-B "+" pair and a PoE switch would put
48 V on 5V_DK — label the port; a TVS on 5V_DK is a cheap Plan 6 safeguard. A rubber cap covers the
jack when undocked.

### 8.2 Power: 5 V on the connector

The lap-timer provides **5V_DK from a small boost converter, enabled by DETECT** (zero quiescent
draw when nothing is plugged; a clean power-up sequence; the dev-kit never sees a half-mated rail),
rated ≥ 500 mA. 5 V is chosen over 3.3 V or raw VBAT because a stock ESP32 devkit's 5V/VIN pin is
designed for an external 5 V *with USB attached* (its USB VBUS is diode-ORed into VIN) — the one
arrangement where "docked and on USB for the console" is clean, with no regulator fighting. 3.3 V
direct would pit the lap-timer's regulator against the dev-kit's LDO whenever USB is present; VBAT
(3.0–4.2 V) starves a devkit LDO's dropout.

Budget: dev-kit ~150–250 mA average, WiFi TX peaks ~300–400 mA; lap-timer ~150–250 mA plus e-paper
refresh spikes. Plan 6 sizes the battery and main regulator for the combined ~600–700 mA peak.

### 8.3 Hot-swap rules

DETECT is the software gate (built in Plan 5.5); GND make-first / power break-last is the
connector's job (RJ45's wiping contacts plus the DETECT-gated boost cover it in practice); EN/GPIO0
idle open so a plug cannot glitch a reset; the boost comes up only after DETECT.

### 8.4 Ownership

This spec owns the contract (pinout, rail, budget, cable rules, hot-swap rules). **Plan 6** owns
the lap-timer hardware: panel RJ45 jack, 5 V boost + DETECT-driven enable, TVS, battery and main
regulator sizing, BOM. The dev-kit side is an RJ45 breakout wired to the stock board's headers
until a custom dev-kit board exists. The interim jumper bench remains documented as interim.

## 9. Testing and the acceptance loop

**Host tests (no hardware):**
- Lap-timer: `status_build()` output byte-identical to the framed `op_status` body (existing STATUS
  decoder as oracle); `LT_REC_STATUS` value and layout compile-checked.
- Dev-kit: demux accepts and routes `LT_REC_STATUS`; `linkstats` counters, seq-gap detection,
  staleness (`connected` flips at 3 s), age math; STATUS → JSON row; console `--json` formatting of
  `dc status`/`selftest` from a fake stats struct; the `flash stage` handshake parser (pure);
  `tools/devkit.py` parsing against canned JSON.

**On-target self-tests are the flash gate:** `devkit.py selftest all` over one USB cable, no AP:
`stream` (≈10 Hz fused + 1 Hz status, 0 gaps), `link` (latency distribution), `framing`
(byte-exact). Plus one AP check with the SPA open: the live monitor at ≈10 Hz *while* the 2 s
status poll runs.

**Acceptance loop — the tooling proves itself on the bug that motivated it:**
1. Land the contract (5.6.1) and the console + trace + self-tests (5.6.2–5.6.3).
2. Run `selftest link`: it is the diagnosis of the ~4 s framed-request latency (which attempt
   fails, what bytes arrived).
3. Fix the root — lap-timer REPL starvation, dev-kit demux mis-assembly, or whatever the trace
   shows — then `selftest link` reports tens of ms and `selftest all` is green.
If the root turns out to be architectural rather than a bug, that is recorded as a ledger ruling and
becomes its own follow-up; it does not silently widen 5.6.

**Bench configuration for development:** dev-kit on USB (console + power), lap-timer powered from
the dev-kit's 5 V rail (jumper now, the Plan 6 port later). Independent lap-timer power is optional
for dev work and required for the Plan 6 OTA matrix. The lap-timer's own USB is needed only to
flash it until `esp-serial-flasher` lands.

## 10. Roadmap placement and sequencing

**Plan 5.6 — Dev-kit as primary dev interface.** After Plan 5.5 (+ the #64–#67 followups merged),
**before Plan 6**: Plan 6 implements the physical interface this spec defines, Plan 9's field
validation runs through the dev-kit, and sub-project C's sole path assumes this interface. C stays
the roadmap tail.

**Pre-step (approved 2026-09-23):** merge followups #64/#65/#66/#67 to main; #68's ideas re-land in
5.6.1 under §4.3's semantics. Plan 5.6 branches off that main.

| session | scope | gate |
|---|---|---|
| 5.6.1 Contract | `status_build`, `LT_REC_STATUS` + asserts, 1 Hz + on-detect push (`link_task`); dev-kit demux/linkstats/status cache, `/api/status` from cache, `connected`/`stream_age`/`status_age`, STATUS rows in SSE/NDJSON | full lap-timer gate + dev-kit host tests; flash gate: SPA open, live monitor ≈ 10 Hz with the poll running; DRAM/stack recorded |
| 5.6.2 Tooling | `esp_console` REPL, shared-core discipline, `dc status`, `dc log`, `lt <cmd>` + timing, `link trace`, `stream stats\|tap`, `--json` | flash gate over USB only |
| 5.6.3 Self-tests, CLI, flash, acceptance | `selftest *`, `tools/devkit.py`, `lt shell`, `flash stage/push/status/abort` + `devkit.py flash`; then the acceptance loop (root-cause + fix the 4 s latency) | `selftest all` green; `devkit.py flash` stages + pushes (apply still gated on Plan 6 power) |
| 5.6.4 Bench day + docs | full gate via `devkit.py selftest all` + AP check; connector contract into `docs/hardware/` (pinout, cable rules, PoE warning) + Plan 6 BOM lines (panel RJ45, boost, TVS); roadmap update | tag `plan-5.6-done` |

**Cross-cutting into Plan 6:** RJ45 panel port; 5V_DK boost with DETECT enable; TVS on 5V_DK;
battery + main regulator for the combined peak; the interim jumper bench documented as interim.

**Deferred, unchanged:** `esp-serial-flasher` (EN/GPIO0 now provisioned on the connector, so it
slots in as `flash --rom` later), GitHub-fetch OTA, BLE (sub-project C).

## 11. Decisions log

- Scope: one effort, two halves (contract + tooling); C is the endgame; LT USB fallback until then.
- Console style: interactive REPL with machine-parseable `--json` output + dev-kit-native
  self-tests (over a serial REST transport, or a tooling-only protocol).
- Followups branch: merge #64–#67 now, drop #68 from the merge, re-land its ideas here.
- Architecture: status rides the stream + dev-kit REPL + host CLI (over HTTP-over-serial or a total
  inversion of on-demand commands).
- Physical: external port, RJ45 over standard straight-through Ethernet cable (over JST-GH — too
  flimsy — or M8/aviation — heavier than needed); 5 V on the connector via a DETECT-enabled boost.
- Flash over USB: in scope (cmd-OTA via a staging handshake mirroring `ota recv`); ROM path deferred.

## 12. Risks

- The ~4 s framed-request latency root is unknown until 5.6.3's trace; the plan carries a ledger
  ruling point for "architectural, not a bug".
- Lap-timer DRAM at ~432 B free: the contract change is designed DRAM-neutral; measured at the gate.
- The dev-kit's USB bridge may not tolerate a higher console baud; `flash` is required to work at
  115200 (≈ 55 s per hop for 600 KB).
- Plan 6's boost/enable/TVS are new BOM lines; until Plan 6, the bench stays on the interim jumper
  with its known brownout limit under an OTA erase.
