# Connectivity + Dev-Controller — Architecture Design (Plan 5 replan)

**Status:** architecture design (brainstorming output). Supersedes the original roadmap Plan 5
("Connectivity and OTA"). Decomposes into sub-projects A/B/C, each of which gets its own
spec → plan when it is started.

**Related:** product spec `docs/superpowers/specs/2026-09-14-lap-timer-design.md` (§18 connectivity,
§19 OTA); Power-of-10 compliance is in force (`plan-4.5-done`). Reference for firmware-over-UART:
Espressif `esp-serial-flasher`.

## 1. Goal

Replace the original single-device "WiFi/BLE + OTA on the lap-timer" plan with a **two-device
system**: the lap-timer runs a lean **production** firmware; a separate, removable **dev
controller** ("flasher") owns all development/tooling — flashing, config, tuning, session offload,
GitHub-release OTA, and black-box logging. The lap-timer is eventually hardened so the dev
controller is its **sole** control/flash path.

## 2. Why two devices (headroom findings, 2026-09-20)

Measured on the current firmware (4 MB flash; partitions: dual 1.25 MB OTA slots + 1.375 MB
storage). A WiFi headroom spike (throwaway) established:

- **WiFi + BLE cannot coexist on this ESP32.** With both stacks statically linked, the DRAM
  region overflows by ~14.6 KB, and it is **not** recoverable by WiFi/lwIP/mbedTLS buffer tuning at
  this scale — the overflow is structural.
- **WiFi-only (BT compiled out) links** with ~46.7 KB static-DRAM headroom, ~28 KB IRAM free, and a
  ~1.04 MB image in the 1.25 MB slot (~11% free) — but that 11% must also absorb Plans 6–8, so the
  single-device WiFi path is flash-tight and forces dropping BLE.
- Putting WiFi on a **separate** device sidesteps both: the lap-timer keeps BLE and its full
  DRAM/flash budget, and its prod image actually *shrinks* (dev/tooling code moves off it).

Conclusion: **approach 2 (separate dev controller over UART).** The dev controller connects by
jumper wires to the lap-timer's UART0 + boot pins — no USB-to-USB cable needed.

## 3. Architecture — the prod/dev split

Principle: **the lap-timer prod firmware carries only production function; the dev controller owns
all development responsibilities.**

**Lap-timer (production firmware) keeps only:**
- Core lap timing + fusion, the UI/display, BLE RaceChrono **live telemetry** (read-only out).
- A **thin link interface** on UART0: the existing `app/cmd` responder (config/session/diag/errlog
  read+write), a fused-log + event **stream** emitted when a peer is attached, and the ROM
  bootloader for flashing.
- **Signed-image verification** before accepting a flash/OTA.
- Hot-swap-safe: runs normally with the dev controller absent; detects it on attach; no hang if the
  UART peer is missing/floating.
- It **sheds** the interactive serial REPL console, the `dbg` verb UX, and any web/config UI — those
  move to the dev controller. The lap-timer only *answers* `cmd`; it does not host the operator UX.

**Dev controller ("flasher", separate ESP32) owns all dev/tooling:**
- WiFi **AP + webserver** — config editor, live tuning, session browser, firmware picker, log
  download.
- **Flashing**: drives the lap-timer's GPIO0/EN into ROM-bootloader mode and streams firmware over
  UART0 via `esp-serial-flasher`; can do a full flash (bricked-device recovery), not just OTA.
- **GitHub-release OTA**: STA to the internet, HTTPS to the GitHub releases API, downloads the
  signed `.bin`, flashes it to the lap-timer.
- **`cmd`-host**: speaks the lap-timer's `app/cmd` protocol over UART0 while the lap-timer app runs —
  config round-trip, session offload, diagnostics.
- **Black-box logger**: while attached during a run, persists the lap-timer's fused-log/event stream
  to its own flash; offloads over WiFi. Field/bench sessions only.
- Powered **from the lap-timer** over the connector (no own battery). Removable, hot-swappable.

## 4. Decomposition — sub-projects, order, dependencies

| # | Sub-project | Scope | Depends on |
|---|---|---|---|
| **A** | Lap-timer prod link (revised Plan 5) | `app/cmd` + fused-log stream + OTA-receive/signed-verify/rollback over UART0; keep BLE RaceChrono live; slim dev-only code out of the prod image; hot-swap-safe link + detect | — |
| **B** | Dev controller / flasher (new plan) | Separate ESP32 firmware: WiFi AP+webserver, `esp-serial-flasher` + boot-pin control, `cmd`-host, GitHub OTA fetch, black-box log store + WiFi offload | A's protocol |
| **C** | Prod hardening (later, gated on B tested) | Make the dev controller the lap-timer's **sole** control/flash path: secure boot v2 + flash encryption (only key-signed images boot), authenticated `cmd` control channel, no open console; keep the ROM UART bootloader (the dev controller flashes through it, but only signed images run) | A, **B built + tested** |

Build order: **A → B (build + test) → C.** C is deliberately last because secure boot is an
**irreversible eFuse burn**: hardening before the dev controller is proven would risk locking the
device out entirely. During A and B the lap-timer keeps its direct paths (USB/esptool, open serial)
for bootstrapping and for debugging the dev controller itself; C removes them only once B works.

## 5. Security — phased

- **Prototype (A, B):** **signed images only** (the §19 release-signing key). The lap-timer accepts
  firmware only if it carries a valid signature; an attacker with UART access cannot flash a working
  image without the private key. Reversible, no eFuse changes, dev-friendly.
- **Production units (C):** **secure boot v2 + flash encryption** — hardware-enforced, only the
  signed bootloader/app boot, flash encrypted at rest. Irreversible; done deliberately near ship, on
  prod units (the dev prototype board stays open for development).

## 6. Cross-cutting inputs

- **Connector / pinout** (feeds Plan 6 / hardware): UART0 **TX** (GPIO1), UART0 **RX** (GPIO3),
  **GPIO0** (boot strap), **EN/RST**, a **power rail** (lap-timer → dev controller), **GND**, and a
  **detect** line (presence handshake) — ≈ 7 pins, hot-swap-safe (no damage or hang on insert/remove
  while the lap-timer runs).
- **Power** (feeds Plan 6): the dev controller draws ~150–250 mA from the lap-timer. It is
  **field/bench-only and removable**, so Plan 6 sizes the lap-timer alone for the ACTIVE riding
  budget and treats "dev-controller attached" as a separate, higher-draw bench/field mode.
- **Logs:** land on the dev controller's flash, offloaded over its WiFi (bounded field-test
  captures; a microSD is a possible later upgrade for very long runs).

## 7. Relationship to the original Plan 5

- 5.1 `app/cmd` over serial → **kept** (foundation of A; `cmd.c` already exists).
- 5.2 `conn_ble` (NimBLE RaceChrono) → **kept on the lap-timer** (live telemetry out).
- 5.3 / 5.4 web export + config pages → **moved to the dev controller** (B); the lap-timer only
  answers `cmd`.
- 5.5 OTA → **split**: lap-timer side = OTA-receive + signed-verify + rollback (A); GitHub fetch +
  flashing = dev controller (B). Prod hardening (secure boot) = C.

## 8. Open questions / risks

- Exact `cmd` extensions needed for live tuning + full config round-trip (designed in A's spec).
- UART0 is shared three ways (app `cmd`/console-stream, ROM bootloader, log stream) — the bootloader
  only runs when the app does not, so the conflict is temporal, but the attach/detach + mode
  transitions need a clean state machine (A).
- Hot-swap electrical safety (series resistors / level considerations on the shared UART/boot lines)
  — hardware detail for the connector design.
- The dev controller is a second firmware to maintain; it is out of the lap-timer's Power-of-10
  scope but should hold to the same standards where practical (decided in B's spec).

## 9. Consequences

- The lap-timer prod image gets **leaner** (sheds tooling), easing flash/RAM — the opposite of the
  single-device WiFi path.
- BLE RaceChrono live telemetry is **retained**.
- A second device + a ~7-pin connector are added; field updates require the dev controller attached
  (this is a dev/maintenance tool, not over-the-air-while-riding).
- Bricked-device recovery + full (re)flash become possible (the dev controller does full flashes).
