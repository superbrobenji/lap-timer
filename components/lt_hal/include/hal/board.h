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

int  board_init(void);
int  board_gps_power(bool on);
int  board_battery_read_mv(uint16_t *mv);          /* 64-sample average, calibrated */
int  board_charger_present(bool *out);             /* CHRG pin, or false if not wired */
int  board_buttons_read(uint8_t *mask);            /* bit0 MODE, bit1 UP, bit2 DOWN */
int  board_buttons_enable_isr(void (*cb)(uint8_t mask, int64_t mono_us));
int  board_prepare_deep_sleep(void);               /* holds, EXT0/EXT1 masks, RTC pulls */
int  board_pps_enable(void (*cb)(int64_t mono_us));   /* no-op when CFG_HAS_PPS == 0 */
const char *board_name(void);

#endif /* HAL_BOARD_H */
