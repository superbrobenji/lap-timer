# Dev-kit port — hardware contract

**Spec:** `docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md` §8 (this document
reproduces that section's contract verbatim for a hardware-focused audience; the spec is the source
of truth if the two ever drift). **Owner:** Plan 6 builds this port; Plan 5.6 defines it.

End state (Plan 6): the lap-timer is the standalone unit, with its own battery. The dev-kit is a
board that plugs into it, is **powered by the lap-timer**, and is **hot-swappable** — it can be
plugged and unplugged while the lap-timer runs, with no crash, hang, or reset on either side. Today's
bench (the dev-kit on USB, feeding the lap-timer over a 5 V jumper — the reverse direction) is an
interim documented below; it is why the interim OTA-apply erase browns the lap-timer out on the
single-USB bench (see "Interim jumper bench").

## 1. Connector: RJ45 (8P8C), standard straight-through Ethernet patch cable

The port lives **outside** the lap-timer enclosure (plug in without opening the case), not sealed or
heavy-duty. RJ45 gives exactly 8 contacts, a positive latch, ~1 A per contact, panel-mount jacks
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

**Current:** ~0.4 A peaks on one 24 AWG conductor each way over a 1.5 m lead ≈ 0.26 Ω loop ≈ 0.1 V
drop — fine for the dev-kit's 5 V→3.3 V LDO. Keep leads 0.5–1 m; shielding not needed.

> **Cable and safety rules**
> - **Straight-through cables only.** A crossover cable swaps the UART pair onto EN/GPIO0.
> - **Never plug this port into real Ethernet or a PoE switch.** Pins 4/5 are the PoE mode-B "+"
>   pair — a PoE switch would put **48 V on 5V_DK**. Label the port. A TVS diode on 5V_DK is a cheap
>   Plan 6 safeguard against a miswire, but is not a substitute for not plugging it into Ethernet.
> - A rubber cap covers the jack when undocked.

## 2. Power: 5 V on the connector

The lap-timer provides **5V_DK from a small boost converter, enabled by DETECT** (zero quiescent
draw when nothing is plugged; a clean power-up sequence; the dev-kit never sees a half-mated rail),
rated **≥ 500 mA**. 5 V is chosen over 3.3 V or raw VBAT because a stock ESP32 devkit's 5V/VIN pin is
designed for an external 5 V *with USB attached* (its USB VBUS is diode-ORed into VIN) — the one
arrangement where "docked and on USB for the console" is clean, with no regulator fighting. 3.3 V
direct would pit the lap-timer's regulator against the dev-kit's LDO whenever USB is present; VBAT
(3.0–4.2 V) starves a devkit LDO's dropout.

**Current budget:** dev-kit ~150–250 mA average, WiFi TX peaks ~300–400 mA; lap-timer ~150–250 mA
plus e-paper refresh spikes. Plan 6 sizes the battery and main regulator for the combined
**~600–700 mA peak**.

## 3. Hot-swap rules

- **DETECT is the software gate** (built in Plan 5.5): the lap-timer drives its stream/logging off
  presence on the DETECT line, so an unplug reads as "not connected" within 3 s, not a hang.
- **GND make-first / power break-last** is the connector's job: RJ45's wiping contacts, plus the
  DETECT-gated boost, cover it in practice — the boost only energises 5V_DK after DETECT confirms a
  mated connector.
- **EN/GPIO0 idle open** (open-drain, only driven during a recovery flash) so a plug/unplug cannot
  glitch a reset on either board.

## 4. Interim jumper bench (until Plan 6)

The connector above is **not built yet** — Plan 6 builds it. Until then, development runs on an
interim bench with the power direction *reversed* from the end state: the dev-kit is on the laptop's
USB (console + power) and feeds the lap-timer over a bare 5 V jumper wire, not through this port.

- **Current bench:** both boards on the laptop's USB, **the 5 V jumper OUT** (each board powers
  itself from its own USB). This is the default working configuration for Plan 5.6 development.
- **Brownout limit:** feeding the lap-timer from the dev-kit's 5 V rail over the jumper cannot
  survive an OTA apply — `esp_ota_begin`'s 1.25 MB slot erase draws enough current, for long enough,
  to brown the lap-timer out before it can even reply `OTA-READY`. This is a hardware headroom
  problem, not a firmware bug: **OTA apply stays deferred to Plan 6 power** (an independent supply,
  or the finished connector's own rail), even though `flash stage` + `flash push` work over this
  bench today.
- **Direct lap-timer flashing:** the dev-kit-TX→lap-timer-RX wire is **pulled only while flashing the
  lap-timer directly** over its own USB — leaving it connected during a direct flash would let the
  dev-kit's idle-high TX and the flashing tool's serial traffic collide on the same UART0 RX pin.

## 5. Ownership

This document mirrors the spec's contract (pinout, rail, budget, cable rules, hot-swap rules) for a
hardware-focused reader. **Plan 6** owns building the lap-timer hardware side: the panel RJ45 jack,
the 5 V boost + DETECT-driven enable, the TVS, and sizing the battery and main regulator for the
combined peak (see `docs/hardware/bom.md`). The dev-kit side is an RJ45 breakout wired to the stock
board's headers until a custom dev-kit board exists. The interim jumper bench above remains
documented as interim, not as the shipped design.
