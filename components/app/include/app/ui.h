/* app/ui.h -- the app-side UI task + buttons (spec §4.3, §20.3, §20.7-20.8).
 *
 * The ui task (core 0, prio 6, stack 6144 real / 2560 moto_sim) owns a static screen_model_t and a static framebuffer,
 * drains the pipeline event queue (g_evt_q) and the button queue, updates the model, renders once
 * per change via core/ui screens_render() and -- since no display panel exists yet (plan 04 has no
 * driver) -- logs the dirty box instead of refreshing a panel. Menu navigation, the button debounce
 * state machine and the menu-lock speed gate live in ui.c; ui_buttons.c is only the ISR->queue glue.
 */
#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Start the ui task (§4.3). Idempotent; call once at boot step 13, after the pipeline/logger start
 * (the model consumes g_evt_q, which lt_ipc_init() must have created). */
void ui_start(void);

/* ---- internal glue shared between ui.c and ui_buttons.c (§20.8) ---- */

/* One raw button edge as captured by the ISR: `mask` is the level of all three buttons at the edge
 * (bit0 MODE, bit1 UP, bit2 DOWN -- board_buttons_read() convention, §5.1), `mono_us` the monotonic
 * edge timestamp used by the ui task's debounce/press-duration state machine. */
typedef struct {
    uint8_t mask;
    int64_t mono_us;
} btn_raw_t;

/* Create the button queue and attach the board button ISR (board_buttons_enable_isr, §5.1) so any
 * edge on GPIO 32/33/25 pushes a btn_raw_t. The board driver already owns those pins (directions +
 * a 25 ms ISR debounce guard), so this only registers the callback -- it never reconfigures pins. */
void ui_buttons_init(void);

/* The button queue created by ui_buttons_init() (NULL before it runs). Drained by the ui task. */
QueueHandle_t ui_buttons_queue(void);

#endif /* APP_UI_H */
