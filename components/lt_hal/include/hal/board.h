/* hal/board.h -- board HAL contract (spec §5.1, called from app tasks).
 *
 * The declarations below are the §5.1 board.h slice verbatim; the include guard and the
 * <stdint.h>/<stdbool.h> includes (needed for bool / the fixed-width types the §5.1 slice
 * uses) are added here so this is a self-contained, compilable header. All functions return
 * int (0 = OK, negative = -errno-style) unless noted; each is called from one task only and
 * is not reentrant. The concrete implementation is components/drivers/board_devkit_v1.
 */
#ifndef HAL_BOARD_H
#define HAL_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* One raw button edge as captured by the board ISR (§20.8): `mask` is the level of all three
 * buttons at the edge (bit0 MODE, bit1 UP, bit2 DOWN -- board_buttons_read()'s convention below),
 * `mono_us` the monotonic edge timestamp the ui task's debounce/press-duration state machine
 * uses. Plain data, no FreeRTOS dependency, so both the firmware (app/ui.h re-exports it) and the
 * host test build can see this type. */
typedef struct {
    uint8_t mask;
    int64_t mono_us;
} btn_raw_t;

int  board_init(void);
int  board_gps_power(bool on);
int  board_battery_read_mv(uint16_t *mv);          /* 64-sample average, calibrated */
/* Charger-present sense (§3.3 CHRG, GPIO 39, open-drain active-low with an EXTERNAL 100k
 * pull-up). Returns false UNCONDITIONALLY on a build with CFG_CHRG_WIRED == 0 -- the prototype,
 * which has neither the pull-up nor a battery. GPIO 34-39 have no internal pull resistor on
 * classic ESP32, so an unwired pin floats and a float that happens to read low is
 * indistinguishable from a charger; answering "false if not wired" is the only implementable
 * reading of this contract, and it is the fail-safe direction for §19.5. Plan 6 (#54) adds the
 * pull-up and builds with CHRG=ON. */
int  board_charger_present(bool *out);
int  board_buttons_read(uint8_t *mask);            /* bit0 MODE, bit1 UP, bit2 DOWN */

/* Bench injection only; see dbg btn (export_serial.c, Plan 7 T8). While `on` is true, `mask`'s
 * bits are OR-ed into every board_buttons_read()/read_button_mask() result -- i.e. through the
 * SAME path the real GPIO level and the ISR-fed debounce state machine use -- so a console
 * command can simulate a physical press without real hardware wired. `on` = false clears those
 * bits again. Never called from production code paths. */
void board_buttons_override(uint8_t mask, bool on);

/* §20.8 / Appendix A: the board ISR's per-pin contact-bounce guard for the three button GPIOs,
 * in milliseconds: an edge within this window of the pin's last accepted edge is dropped (audit
 * fix wave app:L6 -- the ISR used 25 ms before). It is the ONLY user of this constant. §20.8's
 * "level sampled 30 ms after the edge" is NOT implemented: the ISR reads the level at the edge and
 * the ui reconciles the level once per loop iteration. */
#define BTN_DEBOUNCE_MS 30

#ifdef ESP_PLATFORM   /* QueueHandle_t needs FreeRTOS headers, absent from the host test build */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Install the shared button ISR (GPIO 32/33/25, §3.3) and give it evt_q: on every debounced edge
 * the ISR pushes a btn_raw_t straight onto evt_q (xQueueSendFromISR, drop-on-full) -- no
 * function-pointer callback (rule 9). Caller owns evt_q's lifetime (never destroyed here). */
int  board_buttons_enable_isr(QueueHandle_t evt_q);
#endif

int  board_prepare_deep_sleep(void);               /* holds, EXT0/EXT1 masks, RTC pulls */
/* board_pps_enable() removed (session 4.5.5 rule-9 cleanup): dead code, zero callers anywhere.
 * M10 PPS support will re-add a queue-based enable (no function pointer) in its own future plan. */
const char *board_name(void);

#endif /* HAL_BOARD_H */
