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
int  board_charger_present(bool *out);             /* CHRG pin, or false if not wired */
int  board_buttons_read(uint8_t *mask);            /* bit0 MODE, bit1 UP, bit2 DOWN */

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
