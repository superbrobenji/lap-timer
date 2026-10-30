# Prototype wiring, as built

The hardware as built on 2026-10-29, written from the owner's DEC-0135 answer and the pin defines in the tracked code. Every fact cites a tracked file as `path:line@sha "quote"`; a quote is a substring of that line in `git show <sha>:<path>`. Pin numbers are GPIO numbers. A pin in a table below appears inside its first citation's quote.

Sources: the owner's DEC-0135 answer (`docs/superpowers/state/decisions.yml`), the board and display pin defines (`components/drivers/board_devkit_v1/board.c`, `components/drivers/display_epaper_ssd1680/display_epaper.c`), the dev-kit link defines, and the owner-transcript extract `docs/superpowers/state/program/sources/wiring-extract.md` (rows there marked UNCONFIRMED are covered here only through the DEC-0135 answer). Spec pin map: `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §3.3.

Owner ruling (DEC-0135, as-instructed-and-working): `docs/superpowers/state/decisions.yml:1433@39a3e8a "all hardware works and is wired correctly"`. The wiring below is the instructed wiring; the bench day proves it.

## Boards

Two ESP32 boards, the same model: the lap timer carries the IMU, GPS, buttons and display, and the dev-kit is the second board (`docs/superpowers/state/prototype/ledger.md:20@f97ff2a "I have a second esp32(the dev kit)"`; DEC-0311, `docs/superpowers/state/prototype/pending-decisions.yml:117@f97ff2a "two ESP32 boards, the lap timer wired with IMU, GPS, buttons and display"`). The model is the owner's: `docs/superpowers/state/program/sources/wiring-extract.md:164@39a3e8a "both boards are the same. its esp-wroom-32d"`. The BOM row for the boards is in `docs/hardware/bom.md` (row 1).

## GPS: G28U7FTTL-P (5 V part), on UART2

Supply and EN: `docs/superpowers/state/decisions.yml:1430@39a3e8a "GPS VCC to VIN 5 V"`. The part is a 5 V part: `docs/superpowers/state/program/sources/wiring-extract.md:89@39a3e8a "it's definitley a 5v"`. The spec requires 3.3 V on both UART2 lines, receive and transmit: `docs/superpowers/specs/2026-09-14-lap-timer-design.md:140@39a3e8a "in \| 3.3 V"` and `docs/superpowers/specs/2026-09-14-lap-timer-design.md:141@39a3e8a "out \| 3.3 V"`.

| Signal | Pin | Source |
|---|---|---|
| GPS TX to lap-timer RX (UART2 RX) | GPIO16 | `docs/superpowers/state/decisions.yml:1430@39a3e8a "TX 16"`; spec `docs/superpowers/specs/2026-09-14-lap-timer-design.md:140@39a3e8a "GPS UART2 RX (from GPS TX) \| 16"` |
| GPS RX from lap-timer TX (UART2 TX) | GPIO17 | `docs/superpowers/state/decisions.yml:1430@39a3e8a "RX 17"`; spec `docs/superpowers/specs/2026-09-14-lap-timer-design.md:141@39a3e8a "GPS UART2 TX (to GPS RX) \| 17"` |
| GPS PPS | GPIO36 | `components/drivers/board_devkit_v1/board.c:61@39a3e8a "GPIO_NUM_36"` |
| GPS VCC | VIN | `docs/superpowers/state/decisions.yml:1430@39a3e8a "GPS VCC to VIN 5 V"` |
| GPS EN | not connected | `docs/superpowers/state/decisions.yml:1430@39a3e8a "EN open"` |

The firmware assigns no GPS UART pins today; the driver is a stub: `components/drivers/gps_neo6m/gps_neo6m.c:4@39a3e8a "This stub exists so moto_neo6m"`. GPIO16 and GPIO17 come from the DEC-0135 answer and the spec only; no tracked define carries them. PPS is wired; the board define reads `components/drivers/board_devkit_v1/board.c:61@39a3e8a "used only on the M10"`.

## GPS power switch: not built

`components/drivers/board_devkit_v1/board.c:48@39a3e8a "drive LOW = GPS on"` defines `PIN_GPS_PWR` as GPIO26 (`components/drivers/board_devkit_v1/board.c:48@39a3e8a "GPIO_NUM_26"`). No MOSFET is owned and nothing is wired to GPIO26: `docs/superpowers/state/program/sources/wiring-extract.md:96@39a3e8a "not built; no MOSFET owned"`. GPS power modes use UBX software commands only (DEC-0147, `docs/superpowers/state/decisions.yml:1535@39a3e8a "id: DEC-0147"`).

## IMU: GY-521 (MPU-6050)

VCC 3V3 and GND (GND row: `docs/superpowers/state/program/sources/wiring-extract.md:110@39a3e8a "GY-521 GND"`, covered by the DEC-0135 ruling); AD0, XDA and XCL not connected (AD0 floating gives address 0x68: `docs/superpowers/state/program/sources/wiring-extract.md:114@39a3e8a "AD0 floating gives 0x68"`). The IMU's SDA and SCL are on GPIO21 and GPIO22: the owner found both pins (`docs/superpowers/state/prototype/ledger.md:20@f97ff2a "i did find 21 and 22"`; DEC-0311, `docs/superpowers/state/prototype/pending-decisions.yml:117@f97ff2a "IMU SDA 21, SCL 22 as wiring.md states"`).

| Signal | Pin | Source |
|---|---|---|
| SDA | GPIO21 | `components/drivers/board_devkit_v1/board.c:49@39a3e8a "GPIO_NUM_21"` |
| SCL | GPIO22 | `components/drivers/board_devkit_v1/board.c:50@39a3e8a "GPIO_NUM_22"` |
| INT | GPIO27 | `components/drivers/board_devkit_v1/board.c:51@39a3e8a "GPIO_NUM_27"` |
| VCC | 3V3 | `docs/superpowers/state/decisions.yml:1430@39a3e8a "IMU 3V3"` |
| AD0, XDA, XCL | not connected | `docs/superpowers/state/program/sources/wiring-extract.md:114@39a3e8a "XDA, XCL, AD0"` |

## Display: Waveshare 2.13" e-paper HAT (SSD1680)

| Signal | Pin | Source |
|---|---|---|
| CLK | GPIO18 | `components/drivers/board_devkit_v1/board.c:52@39a3e8a "GPIO_NUM_18"` |
| DIN | GPIO23 | `components/drivers/board_devkit_v1/board.c:53@39a3e8a "GPIO_NUM_23"` |
| CS | GPIO5 | `components/drivers/display_epaper_ssd1680/display_epaper.c:37@39a3e8a "GPIO_NUM_5"` |
| DC | GPIO14 | `components/drivers/display_epaper_ssd1680/display_epaper.c:34@39a3e8a "GPIO_NUM_14"` |
| RST | GPIO13 | `components/drivers/display_epaper_ssd1680/display_epaper.c:35@39a3e8a "GPIO_NUM_13"` |
| BUSY | GPIO35 | `components/drivers/display_epaper_ssd1680/display_epaper.c:36@39a3e8a "GPIO_NUM_35"` |

VCC to 3V3 and GND to GND: `docs/superpowers/state/program/sources/wiring-extract.md:39@39a3e8a "HAT VCC"`. RST is on GPIO13 because GPIO4 is the dev-kit detect wire: `docs/superpowers/state/program/sources/wiring-extract.md:37@39a3e8a "we can't plug rst to pin 4"`.

## Buttons

6 mm tactiles, not the 12 mm sealed buttons of the original BOM: `docs/superpowers/state/program/sources/wiring-extract.md:52@39a3e8a "buttons wired in"`. Active-high with pull-down: `components/drivers/board_devkit_v1/board.c:57@39a3e8a "active-high, pull-down"`.

| Signal | Pin | Source |
|---|---|---|
| MODE | GPIO32 | `components/drivers/board_devkit_v1/board.c:57@39a3e8a "GPIO_NUM_32"` |
| UP | GPIO33 | `components/drivers/board_devkit_v1/board.c:58@39a3e8a "GPIO_NUM_33"` |
| DOWN | GPIO25 | `components/drivers/board_devkit_v1/board.c:59@39a3e8a "GPIO_NUM_25"` |

## Dev-kit link (Dupont wires; RJ45 not built)

Transport is plain Dupont wires: `docs/superpowers/state/program/sources/wiring-extract.md:71@39a3e8a "normal duponts for now"`.

| Signal | Pin | Source |
|---|---|---|
| dev-kit TX (UART1) to lap-timer RX0 | dev-kit GPIO17 | `devcontroller/main/build_config.h.in:12@39a3e8a "DC_LINK_TX       17"` |
| dev-kit RX (UART1) from lap-timer TX0 | dev-kit GPIO16 | `devcontroller/main/build_config.h.in:13@39a3e8a "DC_LINK_RX       16"` |
| lap-timer RX0 (UART0 RX) | GPIO3 | `docs/superpowers/state/program/sources/wiring-extract.md:65@39a3e8a "Lap-timer GPIO3 (UART0 RX; RX0)"` |
| lap-timer TX0 (UART0 TX) | GPIO1 | `docs/superpowers/state/program/sources/wiring-extract.md:66@39a3e8a "Lap-timer GPIO1 (UART0 TX; TX0)"` |
| detect: lap-timer GPIO4 to dev-kit GND | GPIO4 | `components/app/CMakeLists.txt:43@39a3e8a "LINK_DETECT_GPIO=4"` |
| GND to GND | GND | `docs/superpowers/state/program/sources/wiring-extract.md:67@39a3e8a "Dev-kit GND"` |

Wiring: dev-kit 17 to lap-timer RX0, dev-kit 16 to lap-timer TX0. No 5 V wire today; the owner adds it as needed: `docs/superpowers/state/decisions.yml:1433@39a3e8a "I can add/remove it as needed"`. UART0 is shared with the USB-serial chip: pull the dev-kit TX wire while flashing the lap timer over USB (`docs/superpowers/state/program/sources/wiring-extract.md:65@39a3e8a "Pull it while flashing the lap timer over USB"`).

## Power (DEC-0134, DEC-0135)

- Field: power bank into the lap timer's USB port (`docs/superpowers/state/decisions.yml:1433@39a3e8a "the power bank will be plugged into the usb port"`); the 5 V wire is in, and the dev-kit is powered by the lap timer and off the laptop (`docs/superpowers/state/decisions.yml:1433@39a3e8a "it'll be powered by the laptimer and disconnected from the laptop"`). While riding, the dev-kit is held in reset or kept off the lap timer's RX0; offload only between sessions (DEC-0312, `docs/superpowers/state/prototype/pending-decisions.yml:126@f97ff2a "devkit-off-rx0 (owner in session"`).
- Between sessions: unplug the power bank first, then the dev-kit goes on the laptop and powers both boards through the 5 V wire (`docs/superpowers/state/decisions.yml:1433@39a3e8a "I think option a"`; `docs/superpowers/state/decisions.yml:1430@39a3e8a "one 5 V source at a time"`).
- Battery pack (Plan 6): not built, and still in scope after the prototype: `docs/superpowers/state/program/sources/wiring-extract.md:24@39a3e8a "does not move the battery pack out of scope"`.
