# As-built prototype wiring, extracted from owner transcripts (read-only investigation, 2026-10-29)

Brief: `.superpowers/sdd/program/wiring-extract-brief.md`. Repo HEAD at extraction: `2e43f13` (branch `block/strict-delivery-b5`).

## How to read this

- **Source keys** (all under `~/.claude/projects/-Users-~-projects-personal-lap-timer/`):
  - `9beb` = `9bebdd36-697e-46eb-9c84-81bd04b224a8.jsonl` (2026-09-14..21)
  - `7ee5` = `7ee58960-7fd0-452e-b7b7-3ab602786208.jsonl` (2026-09-21..10-10; the file's message timestamps end 2026-10-10T16:25)
  - `e2f3` = `e2f358e6-b5f7-4e1e-9b58-92cf5b0b54df.jsonl` (2026-10-10..13)
  - `290d` = `290dd07d-f297-4c32-b3d7-91e2d492fb3b.jsonl` (2026-10-13). Not named in the brief. Found by a keyword sweep of every other transcript in the folder. One owner line is used from it.
  - `b1ec` = `b1ec9f20-b80f-4fda-abcf-bfd5962cee25.jsonl` (the current session). Used only for the owner's 2026-10-29 statement.
  - `XLSX` = `~/Downloads/lattice-poc-bom.xlsx`, cell refs as `Sheet!Cell`.
- Timestamps are each message's own `timestamp` field (UTC).
- **Owner** = a `type:user` message the owner typed. **Assistant** = a model message. Model messages are never an as-built fact.
- **Status column:**
  - **OWNER-CONFIRMED**: the owner said it was wired this way, or said a test passed that depends on it, as quoted.
  - **UNCONFIRMED**: the assistant gave the instruction and the owner never confirmed this pin.
  - **SUPERSEDED**: a later message replaced it, and the replacing message is cited.
- Nothing in this file is inferred without a mark. "DevKit" means the stock ESP32 board (the lap timer). "Dev-kit" means the second ESP32 that acts as the dev controller.

## Owner's framing statement (current)

- b1ec 2026-10-29T18:04:10.907Z (owner): "all hardware is here. the hardware side of the prototype is fully built. the only thing that isn't is the battery, but a powerbank will be used for the power for the initial prototype. but it does not move the battery pack out of scope. I did ask a lot about the wiring for the hardware so you should already know the ones I got"
- Consequence: "fully built" is an owner claim about the whole build. For most subsystems, though, the transcripts hold an assistant instruction and no owner confirmation of that pin. The tables below keep the two apart.

---

## 1. Display: Waveshare 2.13" e-Paper HAT, lap-timer DevKit

| from (part, pin) | to (part, pin) | wire/notes | source | verbatim quote | final? |
|---|---|---|---|---|---|
| HAT CLK | DevKit GPIO18 | HAT 8-wire cable | 7ee5 2026-09-25T21:40:10.783Z (assistant) + 21:41:37.756Z (owner) | asst: "**RST → GPIO 13**. Everything else stays: CLK 18, DIN 23, CS 5, DC 14, BUSY 35, 3V3, GND. ... 1. Wire the panel that way (RST on 13) — OK?"; owner: "yes, wired up. no buttons yet though. I will get them tomorrow" | OWNER-CONFIRMED, final |
| HAT DIN | GPIO23 | same | same | same | OWNER-CONFIRMED, final |
| HAT CS | GPIO5 | same | same | same | OWNER-CONFIRMED, final |
| HAT DC | GPIO14 | same | same | same | OWNER-CONFIRMED, final |
| HAT RST | GPIO13 | moved off GPIO4 after an owner correction (see incident I-4) | 7ee5 2026-09-25T21:39:40.165Z (owner) | "pin 4 is used for the dev kit detect wire. we can't plug rst to pin 4" | OWNER-CONFIRMED, final. Supersedes the earlier "RST 4" pin map (7ee5 2026-09-25T21:32:17.281Z, asst: "SCK 18, DIN(MOSI) 23, CS 5, DC 14, RST 4, BUSY 35") |
| HAT BUSY | GPIO35 | — | 7ee5 21:40:10 / 21:41:37 | as above | OWNER-CONFIRMED, final |
| HAT VCC | DevKit 3V3 | — | 7ee5 21:40:10 / 21:41:37 | "... 3V3, GND" / "yes, wired up" | OWNER-CONFIRMED, final |
| HAT GND | DevKit GND | — | same | same | OWNER-CONFIRMED, final |
| HAT BS strap | 0 (4-wire SPI) | on-board resistor; owner's reading of the board | 7ee5 2026-09-25T21:33:51.310Z (owner) | "there is just a small what seems like a resistor over a pad that says BS and then the left is 1 and right is 0. the resister goes to the 0. so 4 line spi according to the board" | OWNER-CONFIRMED (board state, not a wire) |
| HAT cable, panel end | — | single connector, cannot be re-pinned | 7ee5 2026-09-27T18:33:22.976Z (owner) | "I can't move that side, all the wires on the screen side connects in 1 plug" | OWNER-CONFIRMED |

Repo cross-check:
- Agrees: `components/drivers/display_epaper_ssd1680/display_epaper.c:34-36` (DC 14, RST 13, BUSY 35); `components/drivers/board_devkit_v1/board.c:52-55` (SCK 18, MOSI 23, MISO 19, EPD CS 5); spec `2026-09-14-lap-timer-design.md:147-153` and `:180`.
- The panel version (V4) in `docs/hardware/bom.md:12` is not quoted by the owner anywhere in the sources read. The assistant asked "does it say **V2**, **V3** or **V4**" (7ee5 2026-09-25T21:30:26.755Z). The owner's only reply in that window is the BS-strap line. **UNCONFIRMED in these sources.**

## 2. Buttons: MODE / UP / DOWN, lap-timer DevKit

| from | to | wire/notes | source | verbatim quote | final? |
|---|---|---|---|---|---|
| 3V3 | button (one leg) | 6 mm 4-leg tactile; use legs on opposite sides | 7ee5 2026-09-29T09:16:42.962Z (asst) + 10:07:35.766Z (owner) | asst: "One leg of the switch to **3V3**. The diagonally opposite leg to the GPIO: MODE 32, UP 33, DOWN 25 ... The 100 kΩ from that same GPIO leg to **GND**."; owner: "buttons wired in" | OWNER-CONFIRMED that the buttons are wired. Each pin is UNCONFIRMED: the owner never restated them |
| MODE button | GPIO32 | — | same | same | as above |
| UP button | GPIO33 | — | same | same | as above |
| DOWN button | GPIO25 | — | same | same | as above |
| GPIO32/33/25 | GND via 100 kΩ each | external pull-down | same | same | **UNCONFIRMED**. The owner never said the resistors were fitted. The idle test ("Idle is clean", asst 7ee5 2026-09-29T10:09:01.952Z) cannot tell, because the assistant also states "the internal pull-down is enabled too" (09:16:42) |
| button part | — | 6 mm tactile, not the 12 mm sealed button in the spec/BOM | 7ee5 2026-09-29T09:16:12.539Z (owner) | "would 6mm tactile push buttons work?" | OWNER-CONFIRMED type (follows from "buttons wired in") |

Repo: `board.c:57-59` (32/33/25, "active-high, pull-down") and spec `:155-157`, `:181` agree on the pins. `bom.md:47` says 12 mm sealed buttons are "to order" (see D-6).

## 3. Dev-kit link (Dupont; RJ45 not built)

| from (part, pin) | to (part, pin) | wire/notes | source | verbatim quote | final? |
|---|---|---|---|---|---|
| Dev-kit GPIO17 (UART1 TX; silkscreen TX2, or bare "17" on the owner's board) | Lap-timer GPIO3 (UART0 RX; RX0) | Dupont. Pull it while flashing the lap timer over USB (I-3) | 9beb 2026-09-21T09:31:03.424Z (owner) | "16->tx and 17->rx. the boards don't show any symbols for tx or rx on pins 16 and 17" | OWNER-CONFIRMED, final |
| Dev-kit GPIO16 (UART1 RX) | Lap-timer GPIO1 (UART0 TX; TX0) | Dupont | same | same | OWNER-CONFIRMED, final |
| Dev-kit GND | Lap-timer GND | Dupont | 9beb 2026-09-21T09:28:11.106Z (owner) | "a. with 5v jumper led is on. b gnd is connected between them. c connected: false." | OWNER-CONFIRMED, final |
| Lap-timer GPIO4 (DETECT, silkscreen D4) | Dev-kit GND (a dedicated wire) | active-low, internal pull-up on the lap timer | 9beb 2026-09-21T17:59:56.986Z (asst instruction) + 18:22:36.371Z (owner) | asst: "lap-timer **GPIO4 (header pin `D4`) → dev-controller GND** (any GND pin) — a *dedicated* jumper"; owner: "stream stops on unplug now, works. and restarts when reconnected." | OWNER-CONFIRMED by a test that depends on it, final |
| Dev-kit 5V/VIN | Lap-timer 5V/VIN | "5V jumper"; one USB on the dev-kit fed both boards | 9beb 2026-09-21T09:28:11.106Z (owner); 7ee5 2026-09-21T20:32:26.654Z (owner) | "with 5v jumper led is on"; "the 5v came loose on the laptimer and lost power, we should probably retry" | **SUPERSEDED**: assistant-only, see the next row |
| (no 5V jumper) | — | each board on its own USB | 7ee5 2026-09-23T11:29 (asst, msg idx 358); 7ee5 2026-09-25T20:35 (asst) | "Wiring: 5 V jumper out, GND + UART pair + detect kept, dev-kit TX to LT RX pulled only while flashing the LT"; "**Wiring:** both boards on laptop USB, 5 V jumper OUT, GND + UART pair + detect kept." | **UNCONFIRMED**. The assistant instructed it, and from then on the assistant saw two USB ports (7ee5 2026-09-23T11:41: "lap-timer is `/dev/cu.usbserial-3`, dev-kit is `/dev/cu.usbserial-0001`"). The owner never said "jumper out" |
| Transport | — | Dupont wires, not Ethernet | e2f3 2026-10-10T16:53:24.617Z (owner) | "we are going to use normal duponts for now for the dev kit connection so we can ignore ethernet for now" | OWNER-CONFIRMED, final |
| Rule | — | no 5 V or 3V3 wire between the boards | e2f3 2026-10-13T12:57:35.576Z (asst) | "No 5 V or 3V3 wire between the boards. Lap timer powers itself, from its own USB for now, from the pack later." | UNCONFIRMED (assistant) |

RJ45 port (`docs/hardware/devkit-port.md:21-30`): the owner approved the design ("rj45 would be perfect, could we wire it to use standard ethernet cable?", 7ee5 2026-09-23T09:35:19.373Z; "looks right", 09:37:35.968Z). The owner later set it aside for the prototype (e2f3 2026-10-10T16:53:24.617Z above). **NOT BUILT.**

Repo cross-check:
- `components/app/CMakeLists.txt:43` (`LINK_DETECT_GPIO=4`); `components/app/link/link.c:185` (active-low read).
- `devcontroller/main/build_config.h.in:11-14` (`DC_LINK_UART 1`, `DC_LINK_TX 17`, `DC_LINK_RX 16`, `115200`).
- Spec `:160-161`.
- All agree with the owner-confirmed rows.

## 4. GPS: G28U7FTTL-P (owner-identified part)

Part identity, owner: "the gps i have is g28u7fttl-p no battery pack yet. how do i wire it" (e2f3 2026-10-12T12:02:37.911Z); "we also confirmed the accelerometer hardware and gps hardware. gps is g28u7fttl-p-2408u0003297 and accelerometer is MPU-6050" (290d 2026-10-13T17:59:38.573Z).

| from | to | wire/notes | source | verbatim quote | final? |
|---|---|---|---|---|---|
| GPS VCC (red) | DevKit 3V3 | first instruction | e2f3 2026-10-12T12:03:40.926Z (asst) | "VCC \| red \| 3V3 (for now, see below)" | **SUPERSEDED** by the owner's correction at 12:42:56 (I-6) |
| GPS VCC (red) | DevKit VIN (5V) | — | e2f3 2026-10-12T12:42:56.038Z (owner) + 12:47:33.294Z (asst) | owner: "you were wrong it's 5v"; owner 12:43:58: "it's definitley a 5v"; asst: "red VCC \| VIN (5V)" | Owner-confirmed that the part is a 5 V part. That the owner then wired it to VIN is implied by "the gps module just got hot, no lights" (12:49:05). **Final as-built supply UNCONFIRMED** |
| GPS GND (black) | DevKit GND | lead colours disputed; see I-6 | e2f3 12:03:40 / 12:47:33 (asst) | "black GND \| GND" | UNCONFIRMED. The owner then reported "flipped polarity" (12:53:01), so the original lead mapping was wrong or reversed |
| GPS TXD (blue) | DevKit GPIO16 (RX2) | 3.3 V logic | e2f3 12:03:40 / 12:47:33 (asst) | "blue TX \| 16" | UNCONFIRMED |
| GPS RXD (green) | DevKit GPIO17 (TX2) | — | same | "green RX \| 17" | UNCONFIRMED |
| GPS PPS (white) | DevKit GPIO36 (VP) | — | same | "white PPS \| 36" | UNCONFIRMED |
| GPS EN (yellow) | unconnected | high or floating = on | same | "yellow EN \| unconnected" | UNCONFIRMED |
| Connector order | — | per the assistant's reading of the datasheet | e2f3 12:47:33 (asst) | "Lead order on the 1.25 mm connector: EN, GND, RX, TX, VCC, PPS." | UNCONFIRMED (assistant datasheet claim, not checked here) |
| GPS power switch (P-MOSFET on GPIO26) | — | not built; no MOSFET owned | e2f3 2026-10-10T16:40:52.590Z (asst) | "Nothing you own can switch GPS power. Without it GPS stays on" | not built |

**Last known GPS state.** e2f3 2026-10-12T12:58:08.464Z (owner): "swapped module same behaviour". Then 12:59:31.740Z (owner): "i dont have a multimeter." No later owner message in any transcript read says the GPS lights up or produces NMEA. **The GPS's working state is UNCONFIRMED; it may be two dead modules.**

Repo cross-check:
- Firmware has **no GPS UART pin assignment at all**: `grep -rnE "UART_NUM_2|GPIO_NUM_1[67]" components main` returns nothing. `components/drivers/gps_neo6m/gps_neo6m.c:31-35` is a stub returning `-ENODEV`. GPIO16/17 come only from spec `:140-141`.
- `board.c:61` defines PPS 36 "used only on the M10 (CFG_HAS_PPS)".
- `board.c:48` defines GPS_PWR 26 as "drive LOW = GPS on". Nothing is wired to GPIO26 on the as-built (UNCONFIRMED; no MOSFET).

## 5. IMU: GY-521 (MPU-6050)

| from | to | wire/notes | source | verbatim quote | final? |
|---|---|---|---|---|---|
| GY-521 VCC | DevKit 3V3 | — | e2f3 2026-10-13T12:38:01.671Z (asst) | "VCC \| 3V3" | UNCONFIRMED |
| GY-521 GND | GND | — | same | "GND \| GND" | UNCONFIRMED |
| GY-521 SCL | GPIO22 (D22) | — | same | "SCL \| 22 (D22)" | UNCONFIRMED |
| GY-521 SDA | GPIO21 (D21) | — | same | "SDA \| 21 (D21)" | UNCONFIRMED |
| GY-521 INT | GPIO27 (D27) | — | same | "INT \| 27 (D27)" | UNCONFIRMED |
| XDA, XCL, AD0 | unconnected | AD0 floating gives 0x68 | same; owner asked why at 12:49:20 | owner: "why are there 3 unconnected?" | UNCONFIRMED |

**Unresolved owner observation (blocks confirming this table).** e2f3 2026-10-13T12:45:04.353Z (owner): "i dont see a 21 or 22". 12:45:35.031Z: "there is no d21 or d22 there is only d1 d2 d3". 12:46:25.041Z: "both boards are the same. its esp-wroom-32d". The assistant asked for "every label on both edges in order from the USB end" (12:46:34.689Z). The owner never sent them; the next owner message is "why are there 3 unconnected?" (12:49:20). **Where SDA/SCL were actually wired is unknown.**

Repo: `board.c:49-51` (SDA 21, SCL 22, INT 27) and spec `:144-146`, `:179` agree with the assistant's table.

## 6. Power

### 6a. Now: power bank (owner, 2026-10-29)

| from | to | notes | source | quote | final? |
|---|---|---|---|---|---|
| power bank | lap timer (connection point not stated) | the initial-prototype supply | b1ec 2026-10-29T18:04:10.907Z (owner) | "a powerbank will be used for the power for the initial prototype" | Owner-confirmed as the power source. **Connection point (DevKit USB socket? VIN?) UNCONFIRMED** |

No transcript read has any wiring discussion of the power bank: the keyword sweep for `power ?bank|powerbank` over every `.jsonl` in the folder hits only `b1ec`.

Bench history before the power bank:
- One USB on the dev-kit, with a 5V jumper to the lap timer (owner, 9beb 2026-09-21T09:28:11 and 7ee5 2026-09-21T20:32:26).
- Then both boards on their own USB (assistant only, 7ee5 2026-09-23T11:29 onward). See §3.

### 6b. Later: battery pack (planned, not built)

All rows come from the assistant (e2f3 2026-10-10T16:40:52.590Z, "What you can wire now, with what you own", and 16:53:24.582Z, "Full sequence"). The owner confirmed none of them. **All UNCONFIRMED, NOT BUILT.**

| from | to | notes | verbatim (assistant) |
|---|---|---|---|
| 2× 18650 (parallel holder) + | TP4056 B+ | | "Holder red wire to TP4056 pad B+. Holder black wire to pad B-." |
| holder − | TP4056 B− | | same |
| TP4056 OUT+ | AMS1117 module IN | interim regulator; the spec wants XC6220/AP2112K | "TP4056 OUT+ to AMS1117 module IN. TP4056 OUT- to AMS1117 module GND." |
| AMS1117 OUT | DevKit 3V3 pin (not VIN) | | "3V3 header to the DevKit pin labelled 3V3." |
| 3V3 rail | 4–5 × 10 µF ceramic to GND | stand-in for the 470 µF | "Four or five 10 µF ceramics between 3V3 and GND at the header" |
| TP4056 OUT+ | 470k (or 100k) – tap – 470k (or 100k) – GND; 100 nF tap–GND; tap → GPIO34 | battery sense | "Divider: TP4056 OUT+ through one 470k to a junction. ... Junction to the TAP header pin. If you have no 470k, use two 100k." / "TAP to pin 34." |
| (off switch) | — | none; "pull a cell" | "Off switch is \"pull a cell\" until the 3 A switch arrives." |
| GPS VCC | 3V3 rail | written before the 5 V correction | "When the GY-NEO6M arrives: VCC to 3V3" (16:53:24). SUPERSEDED for this module by the owner's "it's 5v" (2026-10-12T12:42:56). The assistant's replacement plan is the boost converter: "Pack OUT+ to boost IN, boost OUT to GPS red, boost enable to pin 26" (e2f3 2026-10-12T12:43:09.434Z, UNCONFIRMED) |

Owner status of the battery: "the gps i have is g28u7fttl-p no battery pack yet" (e2f3 2026-10-12T12:02:37.911Z); "the only thing that isn't is the battery" (b1ec 2026-10-29).

---

## 7. Incidents and corrections the owner reported

| id | when / source | owner quote | what changed after |
|---|---|---|---|
| I-1 | 9beb 2026-09-21T09:16:39.334Z → 09:52:08 | "the gpio numbers don't match the tx and rx on the boards"; "16->tx and 17->rx. the boards don't show any symbols for tx or rx on pins 16 and 17"; "swapped the cable. it is a wroom"; "moved it to gpio3"; "back on gpio16"; "still not connected" | Wiring stayed as is (owner-confirmed correct). The fault was firmware peer-gating (asst 09:46:20). That led to the GPIO4 detect line (9beb 17:38:06 asst design; owner "stream stops on unplug now, works", 18:22:36) |
| I-2 | 7ee5 2026-09-21T20:32:26.654Z | "the 5v came loose on the laptimer and lost power, we should probably retry" | Retried with the jumper reseated (asst 20:33:55: "Check 5V jumper firmly seated"). Later the assistant moved to "5 V jumper out", each board on its own USB (7ee5 2026-09-23T08:59, idx 219: "Each board on its own USB ... kills the reboots"). The owner never confirmed that move |
| I-3 | 9beb 2026-09-21T18:16–18:19 | owner "ready" (twice) around a failed flash | Rule: pull the dev-kit TX → lap-timer RX wire before a direct USB flash of the lap timer (asst 18:18:37: "Download mode detected, but no sync reply"). Repeated in `devkit-port.md:80-82` |
| I-4 | 7ee5 2026-09-25T21:39:40.165Z | "ther is no sck I assumed you ment clk. and pin 4 is used for the dev kit detect wire. we can't plug rst to pin 4" | E-paper RST moved to GPIO13 (asst 21:40:10). Now in `display_epaper.c:35` and spec `:152` |
| I-5 | 7ee5 2026-09-27T18:17–18:48 | "busy high" / "I can't move that side, all the wires on the screen side connects in 1 plug" / "busy back" / "boot test done" | Temporary test jumper 3V3 → GPIO35 with the BUSY lead pulled. Restored to the HAT BUSY lead ("busy back", "boot test done"). No lasting wiring change |
| I-6 | e2f3 2026-10-12T12:11:44.385Z → 12:59:31.740Z | "the small black chip on the esp32 got insanely hot with that wiring. gps had no led" → "you were wrong it's 5v" → "reloom up the specs of an esp32 and that gps. it's definitley a 5v. what else are you wrong about" → "the gps module just got hot, no lights" → "flipped polarity. still no led but no longer gets hot" → "swapped module same behaviour" → "i dont have a multimeter." | GPS VCC moved from 3V3 to VIN (asst 12:43:09 / 12:47:33). Polarity flipped by the owner. Second module tried. **No resolution recorded.** The assistant's open diagnosis (12:47:33) was "Which chip was it, and was the USB cable in the lap timer at the time?" The owner never answered. Whether the "black chip" was the DevKit regulator or the USB-serial chip, and whether the DevKit was damaged, is unknown |
| I-7 | e2f3 2026-10-12T11:30:09.170Z | "i don't have a 1 or 3" | Assistant relabel: "They are labelled TX0 and RX0 on the DevKit silkscreen" (11:30:13). Owner did not confirm |
| I-8 | e2f3 2026-10-13T12:45:04–12:46:25 | "i dont see a 21 or 22" / "there is no d21 or d22 there is only d1 d2 d3" / "both boards are the same. its esp-wroom-32d" | Unresolved. The assistant asked for the full label list, which the owner never sent (§5) |

## 8. Disagreements (each with both quotes)

| id | between | quote A | quote B |
|---|---|---|---|
| D-1 GPS supply | owner vs spec | owner e2f3 2026-10-12T12:43:58.629Z: "it's definitley a 5v" | spec `:116`: "UART, 3.3 V logic \| ... Supply current, onboard regulator/backup cell and whether a PPS pad is broken out are **[VERIFY]**" and spec `:173`: "└── P-MOSFET S ── D ── GY-NEO6M VCC" (raw pack OUT+, 3.0–4.2 V). Asst e2f3 12:47:33 claims a datasheet "3.3 V min, 5 V nominal, 5.5 V max" (not checked here) |
| D-2 GPS part name in the wiring diagram | spec §3.4 vs owner part | spec `:173`: "GY-NEO6M VCC" | owner 290d 2026-10-13T17:59:38: "gps is g28u7fttl-p-2408u0003297"; spec `:116` itself says "(The GY-NEO6M v2 this row originally named is not the owned part.)" |
| D-3 PPS | spec vs firmware vs assistant instruction | spec `:142`: "Input-only (SENSOR_VP). Unused on NEO-6M v2." | `board.c:61`: "used only on the M10 (CFG_HAS_PPS)"; asst e2f3 2026-10-12T12:47:33: "white PPS \| 36" |
| D-4 GPS UART pins | spec vs firmware | spec `:140-141`: "GPS UART2 RX (from GPS TX) \| 16 ... GPS UART2 TX (to GPS RX) \| 17" | firmware: no definition. `grep -rnE "UART_NUM_2\|GPIO_NUM_1[67]" components main` → no hits; `gps_neo6m.c:33`: "return -ENODEV; /* R28: no receiver is attached and none can be (Plan 08) */" |
| D-5 interim bench power | devkit-port.md contradicts itself | `devkit-port.md:10-11`: "Today's bench (the dev-kit on USB, feeding the lap-timer over a 5 V jumper — the reverse direction)" | `devkit-port.md:72-73`: "**Current bench:** both boards on the laptop's USB, **the 5 V jumper OUT** (each board powers itself from its own USB)." |
| D-6 buttons | bom vs as-built | `bom.md:47`: "12 mm momentary pushbutton, sealed, glove-friendly \| 3 \| ... \| to order" | owner 7ee5 2026-09-29T09:16:12: "would 6mm tactile push buttons work?" → 10:07:35 "buttons wired in"; XLSX `Shopping List!A23`="x", `B23`="Tactile Button 6x6mm (pack of 20)" |
| D-7 multimeter | bom vs spreadsheet vs owner | `bom.md:60`: "Multimeter \| ... \| owned (verify)"; XLSX `Shopping List!A39`="x" `B39`="Digital Multimeter" | owner e2f3 2026-10-12T12:59:31.740Z: "i dont have a multimeter." |
| D-8 100 kΩ resistors | bom vs spreadsheet vs owner | `bom.md:26`: "100 kΩ resistor \| 5 \| ... \| to order" | XLSX `Shopping List!A17`="x" `B17`="100k Resistor 1/4W (pack)"; owner e2f3 2026-10-10T16:40:26.903Z: "I also have plenty of ceramic capacitors and resistors" |
| D-9 hook-up wire / Dupont | bom vs as-built | `bom.md:48`: "Silicone hook-up wire, 22–26 AWG, and Dupont leads \| 1 lot \| ... \| to order" | owner e2f3 2026-10-10T16:53:24.617Z: "we are going to use normal duponts for now" (implies they are owned) |
| D-10 regulator | bom vs owner | `bom.md:22`: "XC6220B331MR or AP2112K-3.3 LDO regulator \| ... \| to order" (no AMS1117 row) | owner e2f3 2026-10-10T16:40:26.903Z: "also have multiple ans1117". XLSX `Shopping List!A14` is blank (Have? not marked) for "AMS1117-3.3V LDO Module" |
| D-11 battery parts | owner vs owner vs bom | owner 9beb 2026-09-14T16:17:00.104Z: "for battery, this is what I currently have: TP4056 charger module, 2-slot 18650 battery case, 18650 3000mah flat top 15a(INR) batteries" | owner 9beb 2026-09-14T17:44:57.713Z: "I currently don't have any of the battery systems, they should be delivered in a week's time"; `bom.md:19-21`: "ordered, ~21 Sep 2026"; XLSX A11–A13 blank (not marked owned) |
| D-12 MCU board identity | owner vs bom/spec vs spreadsheet | owner e2f3 2026-10-13T12:46:25.041Z: "both boards are the same. its esp-wroom-32d"; 12:45:35: "there is only d1 d2 d3" | `bom.md:9` and spec `:115`: "ESP32 DevKit V1 (ESP32-WROOM-32, CH340, 4 MB)"; `bom.md:59`: "USB-A to micro-USB data cable \| ... \| owned"; XLSX `Shopping List!B10`="ESP32 DevKit V4 (WROOM-32D, USB-C)" (A10 blank); asst 9beb 2026-09-21T09:11:05: "its CP2102 chip also drives GPIO3" vs asst e2f3 2026-10-12T12:47:33: "The smaller black chip next to the USB socket is the CH340 serial converter" |
| D-13 who powered whom on the 5V jumper | assistant vs owner | asst 7ee5 2026-09-22T13:56:48.355Z: "the dev controller runs off the LT's 5V jumper" | owner 7ee5 2026-09-21T20:32:26.654Z: "the 5v came loose on the laptimer and lost power" (the lap timer lost power when the jumper came loose, so the dev-kit end fed it); asst 9beb 2026-09-21T09:11:05: "Jumper B `5V`(VIN) → lap-timer `5V`(VIN) to power the lap-timer" |
| D-14 UART0 contention with the lap timer on USB | assistant vs assistant (practice) | asst 9beb 2026-09-21T09:11:05: "If the lap-timer is plugged into USB, its CP2102 chip also drives GPIO3 — and it fights B's GPIO17 every time B sends ... **Keep the lap-timer OFF USB during the link test.**" | asst 7ee5 2026-09-25T20:35: "**Wiring:** both boards on laptop USB, 5 V jumper OUT, GND + UART pair + detect kept." This matters for the power bank if it plugs into the lap timer's USB socket |
| D-15 DETECT wire missing from the interim-bench doc | devkit-port.md vs as-built | `devkit-port.md:66-82` (§4 Interim jumper bench) names the 5 V jumper and the TX → RX wire only | owner-confirmed DETECT wire, lap-timer GPIO4 → dev-kit GND (9beb 2026-09-21T18:22:36); spec `:160` has the row |
| D-16 GPS part in bom vs owner's quantity | bom vs owner | `bom.md:10`: G28U7FTTL-P "\| 1 \|" | owner e2f3 2026-10-12T12:58:08.464Z: "swapped module same behaviour" (at least 2 modules) |

## 9. Parts owned per the spreadsheet vs bom.md

**What the spreadsheet is.** XLSX `Shopping List!A1`: "Lattice Mesh Network - POC Parts List". It is a different project. The "Have?" column (A) is marked "x" only on rows 17, 19, 20, 21, 23, 24, 35, 39, 40, 43, 44, 45. The sheet has no GPS, no IMU and no e-paper rows.

| XLSX row (Have?) | item | bom.md row / status | agree? |
|---|---|---|---|
| A10 blank | ESP32 DevKit V4 (WROOM-32D, USB-C) ×7 | #1 DevKit V1 WROOM-32 CH340, owned | no (identity, D-12) |
| A11 blank | Samsung 30Q 18650 ×10 | #5 18650 INR 3000 mAh 15 A, "ordered, ~21 Sep 2026" | spreadsheet not marked; bom stale (D-11) |
| A12 blank | Dual 18650 holder (parallel) ×5 | #6 2-slot holder, ordered | same |
| A13 blank | TP4056 USB-C (DW01A) ×5 | #7 TP4056 6-pad, ordered | same |
| A14 blank | AMS1117-3.3V module ×5 | no row (#8 XC6220/AP2112K to order) | owner says owned (D-10) |
| A15 blank | 10 µF ceramic ×10 (link is "assorted-capacitors-mega-pack") | #9 10 µF, to order | owner: "plenty of ceramic capacitors" (e2f3 16:40:26) |
| A16 blank | SPST slide switch (link: 50 mA SMD toggle) | #16 3 A switch, to order | not owned; wrong rating per asst e2f3 16:27:20 |
| A17 **x** | 100k resistor pack | #12 100 kΩ, to order | **no** (D-8) |
| A19/A20 **x** | red / green 3 mm LED | — | n/a |
| A21 **x** | 220 Ω pack | — | n/a |
| A23 **x** | tactile button 6×6 mm ×20 | #17 12 mm sealed, to order | **no** (D-6) |
| A24 **x** | USB-A to USB-C cable ×7 | #24 USB-A to micro-USB, owned | connector type differs (D-12) |
| A27 blank | 22AWG hookup wire | #18 wire + Dupont, to order | owner uses Duponts (D-9) |
| A26 blank | perfboard 7×9 | #19 perfboard 5×7, to order | consistent (neither owned) |
| A35 **x** | desoldering pump | — | n/a |
| A39 **x** | digital multimeter | #25 owned (verify) | **owner says no** (D-7) |
| A40 **x** | flush cutters | — | n/a |
| A43–A45 **x** | RPi 4, PSU, microSD | — | n/a |
| — | (no row) | #2 G28U7FTTL-P owned; #3 GY-521 owned; #4 2.13" HAT owned | owner-confirmed GPS + IMU (290d 2026-10-13T17:59:38); display owner-confirmed (7ee5 2026-09-25T21:29:02: "I did get the displays today") |
| — | (no row) | #10, #11, #13, #14, #15, #28–#31 to order | owner never reported having them; MOSFET "Nothing you own" (asst e2f3 16:40:52) |

Summary totals: XLSX `Shopping List!D5`="2730" ("Already Own"), `D6`="6891" ("Still Need to Buy").

## 10. Open questions for the owner (as-built is the owner's fact)

1. **Power bank connection.** Where does the power bank plug in: the lap-timer DevKit's USB socket, or 5 V into VIN? Does the dev-kit stay on the laptop's USB? (D-14 bears on UART0 contention.)
2. **5 V jumper.** Is the dev-kit-to-lap-timer 5 V jumper out today, as the assistant instructed from 2026-09-23 on (D-5, D-13)?
3. **GPS.** Did either G28U7FTTL-P ever light its red LED or send NMEA after 2026-10-12 (I-6)? If yes, what is the final wiring of VCC/GND/TX/RX/PPS/EN: VIN or 5V pin, 16/17, 36, EN floating? If not, are both modules considered dead?
4. **GPS heat incident.** Which chip got hot (regulator by the USB socket, or the USB-serial chip)? Was the lap timer's USB cable plugged in at the time? Is the lap-timer DevKit undamaged (I-6)?
5. **IMU.** Pin-by-pin, is the GY-521 wired VCC→3V3, GND→GND, SDA→21, SCL→22, INT→27, XDA/XCL/AD0 open? What are the silkscreen labels on both edges of the board (I-8: "only d1 d2 d3")?
6. **Buttons.** Are the three external 100 kΩ pull-downs fitted, or does the build rely on the internal pull-downs only?
7. **Board identity.** What is the exact board: DevKit V1 or V4, WROOM-32 or WROOM-32D, CH340 or CP2102, micro-USB or USB-C (D-12)? Are the lap timer and the dev-kit the same model?
8. **Display panel version.** Is it V4, as `bom.md:12` says? No owner quote was found in these sources.
9. **Parts owned.** Which battery-pack parts are actually in hand: cells, holder, TP4056 (6-pad?), AMS1117, capacitors, 470k/100k? The spreadsheet's Have? column and bom.md disagree (D-7, D-8, D-10, D-11).
10. **Multimeter.** Is one owned now (D-7)? The assistant's battery sequence depends on one (e2f3 2026-10-12T13:00:14: "Stop the pack work until it arrives").
