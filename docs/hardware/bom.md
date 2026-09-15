# Prototype bill of materials

Motorcycle prototype (`moto_neo6m` build). Spec references point at `docs/superpowers/specs/2026-09-14-lap-timer-design.md`. Status: **owned** (in hand), **ordered** (with expected arrival), **to order**. Prices are approximate South African landed prices in September 2026 and are only there to size the order.

## Core electronics

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 1 | ESP32 DevKit V1 (ESP32-WROOM-32, CH340, 4 MB) | 1 | controller; verified ESP32-D0WD-V3 rev 3.1, 4 MB flash | §3.1, §3.3 | owned | — | — |
| 2 | GY-NEO6M v2 GPS module | 1 | position, speed, time (5 Hz prototype) | §3.1, §7.3 | ordered, ~mid-Oct 2026 | Communica / Micro Robotics | R150 |
| 3 | GY-521 (MPU6050) | 1 | lean angle, g-forces, motion wake | §3.1, §8 | ordered, ~mid-Oct 2026 | Communica / Micro Robotics | R60 |
| 4 | Waveshare 2.9" e-Paper Module V2 (SSD1680, 296×128) | 1 | rider display | §3.1, §20.1 | to order | DIYElectronics / Micro Robotics | R450 |

## Power

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 5 | 18650 INR 3000 mAh 15 A, flat top | 2 | battery pack, wired in parallel (1S2P) | §3.1 | ordered, ~21 Sep 2026 | Communica / local vape shops | R120 ea |
| 6 | 2-slot 18650 holder | 1 | pack | §3.1 | ordered, ~21 Sep 2026 | Communica | R30 |
| 7 | TP4056 charger module, 6-pad (DW01A + FS8205A protection) | 1 | charging + cell protection | §3.1, §3.2 | ordered, ~21 Sep 2026 (verify 6-pad) | Communica / Micro Robotics | R25 |
| 8 | XC6220B331MR or AP2112K-3.3 LDO regulator | 1 (+1 spare) | 3.3 V rail into the DevKit 3V3 pin, ≥ 600 mA output, ≤ 60 µA quiescent | §3.1, §3.2 | to order | RS Components ZA / Mantech / AliExpress | R20 |
| 9 | 10 µF ceramic capacitor | 2 | regulator in/out | §3.1 | to order | Communica | R5 |
| 10 | 470 µF electrolytic capacitor, 6.3 V+ | 1 | rail bulk for radio bursts | §3.1 | to order | Communica | R5 |
| 11 | P-channel MOSFET AO3401A or SI2301 | 1 (+1 spare) | GPS power switch (PARK) | §3.1, §3.3 | to order | Mantech / RS / AliExpress | R10 |
| 12 | 100 kΩ resistor | 5 | MOSFET gate pull-up, 3× button pull-downs, CHRG pull-up | §3.1, §3.3 | to order | Communica | R5 |
| 13 | 470 kΩ resistor | 2 | battery divider | §3.1 | to order | Communica | R5 |
| 14 | 100 nF ceramic capacitor | 1 | divider filter | §3.1 | to order | Communica | R2 |
| 15 | SS14 Schottky diode | 1 | optional: USB + battery co-existence | §3.2 | to order (optional) | Communica | R3 |
| 16 | Slide or rocker switch, 3 A | 1 | pack disconnect | §3.2 | to order | Communica | R15 |

## Controls, wiring, enclosure

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 17 | 12 mm momentary pushbutton, sealed, glove-friendly | 3 | MODE / UP / DOWN | §3.3, §20.8 | to order | Communica / AliExpress | R15 ea |
| 18 | Silicone hook-up wire, 22–26 AWG, and Dupont leads | 1 lot | interconnect | §3.4 | to order | Communica | R60 |
| 19 | Prototyping perfboard 5×7 cm | 1 | regulator, MOSFET, divider, pull-downs | §3.4 | to order | Communica | R20 |
| 20 | IP65 ABS enclosure ≈ 115×90×55 mm, clear lid | 1 | weatherproof housing; e-paper behind the lid | §2.3 C3 | to order | Communica / Mantech | R120 |
| 21 | RAM-style ball mount or handlebar clamp | 1 | mounting on the bike | §2.3 C3 | to order | local motorcycle shop | R250 |
| 22 | Cable gland PG7 | 1 | charge port / USB lead | — | to order | Communica | R10 |
| 23 | Double-sided foam / vibration pads | 1 lot | IMU and board damping | §2.3 C3 | to order | hardware store | R30 |

## Bench and tooling

| # | Item | Purpose | Status |
|---|------|---------|--------|
| 24 | USB-A to micro-USB data cable | flashing, serial console | owned |
| 25 | Multimeter | power-state current measurements (§16.5) | owned (verify) |
| 26 | USB-UART adapter (CP2102/CH340) | `tools/gps_sim.py` replay into GPIO 16 before the GPS arrives; optional with the `gps_sim` driver | optional |
| 27 | Bench power supply (variable) | brownout test (§22.3) | optional |

## Upgrade path (not needed for the prototype)

| Part | Replaces | Spec |
|------|----------|------|
| SEQURE M10-25Q (u-blox M10, 10 Hz, QMC5883L) | GY-NEO6M v2 | §3.5, O3 |
| microSD SPI module | internal-only storage | §13.2, O4 |
| Sharp memory LCD LS027B7DH01 or 2.42" SSD1309 OLED | e-paper (live delta variant) | O5 |
| Battery-native ESP32 board (FireBeetle 2, FeatherS3) | DevKit + external regulator | §3.5 |

## Order checklist (window B, before session 6.1)

Items 8–16 and 17–23 above; the regulator (8) and MOSFET (11) gate the power work in plan 6. Confirm the TP4056 (7) is the 6-pad protected version on arrival; if not, order one before wiring the pack.
