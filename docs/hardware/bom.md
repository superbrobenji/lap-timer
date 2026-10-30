# Prototype bill of materials

Motorcycle prototype (`moto_neo6m` build). Spec references point at `docs/superpowers/specs/2026-09-14-lap-timer-design.md`. Prices are approximate South African landed prices in September 2026 and are only there to size the order. Status legend, from the owner's answer DEC-0309 (`docs/superpowers/state/prototype/pending-decisions.yml:99@f97ff2a "not-in-hand (owner in session"`): **built** = wired in the prototype; **owned** = in hand, not wired; **not built** = not in hand; **unknown** = no tracked source says whether it is in hand. Unknown rows stay unknown until the b6 bench-day inventory (DEC-0310, `docs/superpowers/state/prototype/pending-decisions.yml:108@f97ff2a "bench-day (owner in session"`). DEC-0310 also asks whether the field power bank gets a BOM row (`docs/superpowers/state/prototype/pending-decisions.yml:103@f97ff2a "should the field power bank get a BOM row?"`) and its answer leaves that to the same inventory, so there is no power-bank row now. The earlier status is kept as "(was: ...)". Set 2026-10-30 from the owner's DEC-0135 answer and the wiring extract (see "Status sources"). The as-built wiring is in `docs/hardware/wiring.md`. The "Upgrade path" table lists alternatives, not prototype rows, and has no status.

## Core electronics

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 1 | ESP32 board, ESP-WROOM-32D module (lap timer and dev-kit; the same model) | 2 | lap-timer controller and dev-kit controller; one board measured ESP32-D0WD-V3 rev 3.1, 4 MB flash | §3.1, §3.3 | built (was: owned) | — | — |
| 2 | G28U7FTTL-P GPS module (u-blox 7-class UBX-G7020; label 2408U0003297) | 1 | position, speed, time (u-blox 7 maximum nav rate 10 Hz, §3.1; driver is a Plan 8 stub) | §3.1, §7.3 | built (was: owned) | Communica / Micro Robotics | R150 |
| 3 | GY-521 (MPU-6050) | 1 | lean angle, g-forces, motion wake | §3.1, §8 | built (was: owned) | Communica / Micro Robotics | R60 |
| 4 | Waveshare 2.13" e-Paper HAT rev2.1, panel V4 (SSD1680, 122×250) | 1 | rider display | §3.1, §20.1 | built (was: owned) | DIYElectronics / Micro Robotics | R450 |
| 4a | Waveshare 2.9" e-Paper Module V2 (SSD1680, 296×128) | 1 | rider display, alternative panel (`ws29v2`) | §3.1, §20.1 | unknown (was: alternative) | DIYElectronics / Micro Robotics | R450 |

## Power

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 5 | 18650 INR 3000 mAh 15 A, flat top | 2 | battery pack, wired in parallel (1S2P) | §3.1 | unknown (was: ordered, ~21 Sep 2026) | Communica / local vape shops | R120 ea |
| 6 | 2-slot 18650 holder | 1 | pack | §3.1 | unknown (was: ordered, ~21 Sep 2026) | Communica | R30 |
| 7 | TP4056 charger module, 6-pad (DW01A + FS8205A protection) | 1 | charging + cell protection | §3.1, §3.2 | unknown (was: ordered, ~21 Sep 2026 (verify 6-pad)) | Communica / Micro Robotics | R25 |
| 8 | XC6220B331MR or AP2112K-3.3 LDO regulator | 1 (+1 spare) | 3.3 V rail into the DevKit 3V3 pin, ≥ 600 mA output, ≤ 60 µA quiescent | §3.1, §3.2 | unknown (was: to order) | RS Components ZA / Mantech / AliExpress | R20 |
| 9 | 10 µF ceramic capacitor | 2 | regulator in/out | §3.1 | unknown (was: to order) | Communica | R5 |
| 10 | 470 µF electrolytic capacitor, 6.3 V+ | 1 | rail bulk for radio bursts | §3.1 | unknown (was: to order) | Communica | R5 |
| 11 | P-channel MOSFET AO3401A or SI2301 | 1 (+1 spare) | GPS power switch (PARK) | §3.1, §3.3 | not built (was: to order) | Mantech / RS / AliExpress | R10 |
| 12 | 100 kΩ resistor | 5 | MOSFET gate pull-up, 3× button pull-downs, CHRG pull-up | §3.1, §3.3 | unknown (was: to order) | Communica | R5 |
| 13 | 470 kΩ resistor | 2 | battery divider | §3.1 | unknown (was: to order) | Communica | R5 |
| 14 | 100 nF ceramic capacitor | 1 | divider filter | §3.1 | unknown (was: to order) | Communica | R2 |
| 15 | SS14 Schottky diode | 1 | optional: USB + battery co-existence | §3.2 | unknown (was: to order (optional)) | Communica | R3 |
| 16 | Slide or rocker switch, 3 A | 1 | pack disconnect | §3.2 | unknown (was: to order) | Communica | R15 |

## Plan 6 — dev-kit connector

New lines from the Plan 5.6 physical-interface design (`docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md` §8; pinout and rules reproduced in `docs/hardware/devkit-port.md`). Plan 6 builds the port; these are lap-timer-side additions only (the dev-kit side is an RJ45 breakout wired to the stock devkit's headers).

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 28 | RJ45 8P8C panel-mount jack (IP-rated boot optional) | 1 | lap-timer-side dev-kit connector, outside the enclosure | Plan 5.6 spec §8.1 | unknown (was: to order) | Communica / Mantech / AliExpress | R30 |
| 29 | 5 V boost converter module, ≥ 500 mA, with an enable pin | 1 | 5V_DK rail, DETECT-gated enable (zero quiescent draw unplugged) | Plan 5.6 spec §8.2, §8.3 | unknown (was: to order) | AliExpress / Mantech | R25 |
| 30 | TVS diode, 5 V working voltage (SMAJ5.0A-class) | 1 | 5V_DK surge/miswire protection (guards against a PoE 48 V mistake) | Plan 5.6 spec §8.1 | unknown (was: to order) | Communica / Mantech | R5 |
| 31 | Straight-through Cat5e patch cable, short (≤ 1 m) | 1 | dev-kit ↔ lap-timer link; crossover cables are explicitly unsafe here | Plan 5.6 spec §8.1 | unknown (was: to order) | any electronics/computer shop | R20 |

## Controls, wiring, enclosure

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 17 | 12 mm momentary pushbutton, sealed, glove-friendly | 3 | MODE / UP / DOWN | §3.3, §20.8 | unknown (was: to order) | Communica / AliExpress | R15 ea |
| 18 | Silicone hook-up wire, 22–26 AWG, and Dupont leads | 1 lot | interconnect | §3.4 | built (Dupont leads) (was: to order) | Communica | R60 |
| 19 | Prototyping perfboard 5×7 cm | 1 | regulator, MOSFET, divider, pull-downs | §3.4 | unknown (was: to order) | Communica | R20 |
| 20 | IP65 ABS enclosure ≈ 115×90×55 mm, clear lid | 1 | weatherproof housing; e-paper behind the lid | §2.3 C3 | unknown (was: to order) | Communica / Mantech | R120 |
| 21 | RAM-style ball mount or handlebar clamp | 1 | mounting on the bike | §2.3 C3 | unknown (was: to order) | local motorcycle shop | R250 |
| 22 | Cable gland PG7 | 1 | charge port / USB lead | — | unknown (was: to order) | Communica | R10 |
| 23 | Double-sided foam / vibration pads | 1 lot | IMU and board damping | §2.3 C3 | unknown (was: to order) | hardware store | R30 |

## Bench and tooling

| # | Item | Purpose | Status |
|---|------|---------|--------|
| 24 | USB-A to micro-USB data cable | flashing, serial console | unknown (was: owned) |
| 25 | Multimeter | power-state current measurements (§16.5) | unknown (was: owned (verify)) |
| 26 | USB-UART adapter (CP2102/CH340) | spare serial console / flash path; no longer a GPS replay source (the `gps_sim` driver replays a capture compiled into the image, spec §4.6, and the real GPS has been in hand since 2026-10-13) | unknown (was: optional) |
| 27 | Bench power supply (variable) | brownout test (§22.3) | unknown (was: optional) |

## Upgrade path (not needed for the prototype)

| Part | Replaces | Spec |
|------|----------|------|
| SEQURE M10-25Q (u-blox M10, 10 Hz, QMC5883L) | G28U7FTTL-P (row 2) | §3.5, O3 |
| microSD SPI module | internal-only storage | §13.2, O4 |
| Sharp memory LCD LS027B7DH01 or 2.42" SSD1309 OLED | e-paper (live delta variant) | O5 |
| Battery-native ESP32 board (FireBeetle 2, FeatherS3) | DevKit + external regulator | §3.5 |

## Order checklist (window B, before session 6.1)

Rows 8–16 and 19–23 above that are not built or unknown (row 17 is replaced by the wired 6 mm tactiles, row 18 is built); the regulator (8) and MOSFET (11) gate the power work in plan 6. Confirm the TP4056 (7) is the 6-pad protected version on arrival; if not, order one before wiring the pack.


## Status sources

Each citation is `path:line@sha "quote"`. Under the legend, a row is "not built" only where a tracked source states the part is not in hand; the absence of an owner report, a blank spreadsheet cell or a stale "to order" is not such a statement, so those rows are unknown.

- Rows 1-4 built: `docs/superpowers/state/decisions.yml:1433@39a3e8a "all hardware works and is wired correctly"`; GPS and IMU parts owner-identified: `docs/superpowers/state/program/sources/wiring-extract.md:211@39a3e8a "owner-confirmed GPS + IMU"`.
- Row 1 (two boards, same model): the owner's DEC-0311 answer records `docs/superpowers/state/prototype/ledger.md:20@f97ff2a "I have a second esp32(the dev kit)"` and `docs/superpowers/state/prototype/pending-decisions.yml:117@f97ff2a "two ESP32 boards, the lap timer wired with IMU, GPS, buttons and display"`; the model: `docs/superpowers/state/program/sources/wiring-extract.md:164@39a3e8a "both boards are the same. its esp-wroom-32d"`. The lap timer is wired and the dev-kit is wired to it by the link (`docs/hardware/wiring.md`), so built. The chip detail is from one measured board: `docs/measurements.md:250@39a3e8a "Board: ESP32 DevKit V1, ESP32-D0WD-V3 rev 3.1, 4 MB flash"`. The earlier row text also said "DevKit V1" and "CH340" (`docs/superpowers/specs/2026-09-14-lap-timer-design.md:115@39a3e8a "ESP32 DevKit V1 (ESP32-WROOM-32, CH340)"`); no tracked source ties those two details to both boards, so they are left out and the board name and serial chip are unknown.
- Divergence, row 2: qty 1, but the owner has at least two modules: `docs/superpowers/state/program/sources/wiring-extract.md:185@39a3e8a "swapped module same behaviour"`; the row text is left as is.
- Row 4a: no tracked source says the 2.9" panel is in hand; unknown.
- Rows 5-7 (cells, holder, TP4056): the owner's two statements conflict, so unknown: `docs/superpowers/state/program/sources/wiring-extract.md:180@39a3e8a "for battery, this is what I currently have"` against `docs/superpowers/state/program/sources/wiring-extract.md:180@39a3e8a "I currently don't have any of the battery systems"`. The pack is not wired: `docs/superpowers/state/program/sources/wiring-extract.md:149@39a3e8a "the only thing that isn't is the battery"`.
- Row 8 (XC6220/AP2112K): the owner has an AMS1117, not this part: `docs/superpowers/state/program/sources/wiring-extract.md:179@39a3e8a "also have multiple ans1117"`; whether the listed part is in hand is unsettled, so unknown.
- Rows 9, 12, 13, 14 (10 uF capacitors, 100 kohm and 470 kohm resistors, 100 nF): the owner's general statement does not name the values, so unknown: `docs/superpowers/state/program/sources/wiring-extract.md:177@39a3e8a "I also have plenty of ceramic capacitors and resistors"`. Row 12 also serves the button pull-downs, never confirmed fitted: `docs/superpowers/state/program/sources/wiring-extract.md:56@39a3e8a "The owner never said the resistors were fitted"`.
- Rows 10, 15 (470 uF capacitor, SS14 diode): no tracked source states whether they are in hand; the extract only records an absence, `docs/superpowers/state/program/sources/wiring-extract.md:212@39a3e8a "owner never reported having them"`, so unknown.
- Row 11 (GPS power MOSFET on GPIO26) not built: `docs/superpowers/state/program/sources/wiring-extract.md:96@39a3e8a "not built; no MOSFET owned"`; `docs/superpowers/plans/2026-10-29-prototype-field-ready.md:31@6c3f445 "no MOSFET is owned and nothing is wired to GPIO26"`.
- Row 16 (3 A slide or rocker switch): the extract's "not owned" is about a different spreadsheet line and rests on a blank spreadsheet cell, `docs/superpowers/state/program/sources/wiring-extract.md:199@39a3e8a "not owned; wrong rating"`, and the same extract shows a blank cell next to a part the owner has (`docs/superpowers/state/program/sources/wiring-extract.md:179@39a3e8a "also have multiple ans1117"`), so unknown.
- Row 17 (12 mm sealed buttons): the 6 mm tactiles are wired instead (`docs/superpowers/state/program/sources/wiring-extract.md:52@39a3e8a "buttons wired in"`); whether the 12 mm buttons are in hand is unsettled, so unknown.
- Row 18 built (Dupont leads): `docs/superpowers/state/program/sources/wiring-extract.md:71@39a3e8a "normal duponts for now"`. That source covers the Dupont leads only; no tracked source says the silicone hook-up wire is in hand.
- Row 19 (perfboard): the extract's "neither owned" rests on a blank spreadsheet cell, `docs/superpowers/state/program/sources/wiring-extract.md:206@39a3e8a "consistent (neither owned)"`, so unknown.
- Rows 20-23, 24, 26, 27: no tracked source settles them; unknown. Row 24's connector type is also unresolved: `docs/superpowers/state/program/sources/wiring-extract.md:181@39a3e8a "both boards are the same"`.
- Row 25: `docs/superpowers/state/program/sources/wiring-extract.md:176@39a3e8a "i dont have a multimeter"` is the owner's 2026-10-12 line against "owned (verify)"; whether one has been bought since is not recorded, so unknown.
- Rows 28-31 (RJ45 dev-kit port, Plan 6): no tracked source states whether they are in hand (`docs/superpowers/state/program/sources/wiring-extract.md:212@39a3e8a "owner never reported having them"` is an absence), so unknown; set aside for the prototype: `docs/superpowers/state/program/sources/wiring-extract.md:71@39a3e8a "so we can ignore ethernet for now"`.
