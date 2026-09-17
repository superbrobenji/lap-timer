/* ui_buttons.c -- button ISR -> ui queue glue (spec §20.8, §4.4).
 *
 * The board driver (components/drivers/board_devkit_v1) already owns the three button pins
 * (GPIO 32/33/25): board_init() sets them input + pull-down and board_buttons_enable_isr()
 * installs the shared GPIO ISR service, sets ANYEDGE on each pin, adds an IRAM ISR that applies a
 * 25 ms contact-bounce guard and then calls a caller cb(mask, mono_us). We therefore do NOT
 * re-install the ISR service or reconfigure pins here (spec §20.8: "do not re-init pins the board
 * driver owns"); ui_buttons_init() only registers the callback below, which pushes each debounced
 * edge onto a static queue that the ui task drains. The press-duration / short-long-vlong / UP+DOWN
 * debounce state machine lives in ui.c (it needs the model + timers), per §20.8.
 *
 * The cb runs in ISR context (§17.9: ISRs are IRAM_ATTR and touch queues only via *FromISR).
 */
#include "app/ui.h"

#include "hal/board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define BTN_Q_DEPTH 8 /* §4.4 btn_q depth 8 */

static StaticQueue_t s_btn_q_ctrl;
static uint8_t       s_btn_q_store[BTN_Q_DEPTH * sizeof(btn_raw_t)];
static QueueHandle_t s_btn_q;

/* Board button ISR callback (IRAM, ISR context): forward the debounced edge to the ui task. */
static void IRAM_ATTR btn_isr_cb(uint8_t mask, int64_t mono_us)
{
    if (s_btn_q == NULL) {
        return;
    }
    BaseType_t hpw = pdFALSE;
    btn_raw_t  ev = {.mask = mask, .mono_us = mono_us};
    (void)xQueueSendFromISR(s_btn_q, &ev, &hpw); /* drop-on-full: a stale edge is harmless */
    portYIELD_FROM_ISR(hpw);
}

void ui_buttons_init(void)
{
    if (s_btn_q == NULL) {
        s_btn_q = xQueueCreateStatic(BTN_Q_DEPTH, sizeof(btn_raw_t), s_btn_q_store, &s_btn_q_ctrl);
    }
    (void)board_buttons_enable_isr(btn_isr_cb);
}

QueueHandle_t ui_buttons_queue(void)
{
    return s_btn_q;
}
