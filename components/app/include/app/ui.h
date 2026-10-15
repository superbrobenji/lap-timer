/* app/ui.h -- the app-side UI task + buttons (spec §4.3, §20.3, §20.7-20.8).
 *
 * The ui task (core 0, prio 6, stack 6144 on every build) owns a static screen_model_t and a
 * static framebuffer, drains the ui's own copy of the event broadcast (g_ui_evt_q -- g_evt_q is
 * the LOGGER's copy, app/lt_ipc.h) and the button queue, updates the model, renders once per
 * change via core/ui's screens_render() and drives the SSD1680 e-paper through hal/display.h with
 * the full §20.3 refresh ladder. Menu navigation, the button debounce state machine (a per-pin
 * leading-edge guard at BTN_DEBOUNCE_MS = 30 ms in the board ISR plus ui.c's live-level reconcile;
 * §20.8's "sampled 30 ms after the edge" is not implemented) and the §20.7 menu-lock gate
 * (pipeline_speed_kmh() >= MENU_LOCK_SPEED_KMH, design 2026-10-13 R11) live in ui.c;
 * ui_buttons.c is only the ISR->queue glue.
 */
#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hal/board.h"   /* btn_raw_t (§20.8 button ISR->queue event); ui.h is app, may include hal */

/* Start the ui task (§4.3). Idempotent; call once at boot step 13, after the pipeline/logger start
 * (the model consumes g_evt_q, which lt_ipc_init() must have created). */
void ui_start(void);

/* Partial refreshes issued since the last full refresh (§15.3 RTC-resume bookkeeping). Safe from
 * any task: the ui task is the only writer and both sides use relaxed atomic accesses. */
uint32_t ui_partial_count(void);

/* ---- internal glue shared between ui.c and ui_buttons.c (§20.8) ---- */

/* btn_raw_t (one raw button edge, as captured by the board ISR) is defined in hal/board.h. */

/* Create the button queue and give it to the board button ISR (board_buttons_enable_isr, §5.1) so
 * any edge on GPIO 32/33/25 pushes a btn_raw_t straight onto it. The board driver already owns
 * those pins (directions + a per-pin leading-edge ISR guard at BTN_DEBOUNCE_MS = 30 ms), so this
 * only creates the queue and hands it over -- it never reconfigures pins. */
void ui_buttons_init(void);

/* The button queue created by ui_buttons_init() (NULL before it runs). Drained by the ui task. */
QueueHandle_t ui_buttons_queue(void);

#endif /* APP_UI_H */
