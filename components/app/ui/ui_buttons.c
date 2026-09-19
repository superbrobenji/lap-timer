/* ui_buttons.c -- button ISR -> ui queue glue (spec §20.8, §4.4).
 *
 * The board driver (components/drivers/board_devkit_v1) already owns the three button pins
 * (GPIO 32/33/25): board_init() sets them input + pull-down and board_buttons_enable_isr()
 * installs the shared GPIO ISR service, sets ANYEDGE on each pin, and adds an IRAM ISR that
 * applies a 25 ms contact-bounce guard and then pushes a btn_raw_t straight onto the queue we
 * hand it (no function-pointer callback, rule 9). We therefore do NOT re-install the ISR service
 * or reconfigure pins here (spec §20.8: "do not re-init pins the board driver owns");
 * ui_buttons_init() only creates that queue and hands it to board_buttons_enable_isr(). The
 * press-duration / short-long-vlong / UP+DOWN debounce state machine lives in ui.c (it needs the
 * model + timers), per §20.8.
 */
#include "app/lt_assert.h"
#include "app/ui.h"

#include "hal/board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Same per-module rule 5 code as ui.c (design §3): the button glue is part of the ui module. */
#define UI_APP_ASSERT_CODE 0x0B50

#define BTN_Q_DEPTH 8 /* §4.4 btn_q depth 8 */

static StaticQueue_t s_btn_q_ctrl;
static uint8_t       s_btn_q_store[BTN_Q_DEPTH * sizeof(btn_raw_t)];
static QueueHandle_t s_btn_q;

void ui_buttons_init(void)
{
    if (s_btn_q == NULL) {
        s_btn_q = xQueueCreateStatic(BTN_Q_DEPTH, sizeof(btn_raw_t), s_btn_q_store, &s_btn_q_ctrl);
    }
    /* Static creation with a real store never returns NULL. */
    LT_ASSERT_VOID(s_btn_q != NULL, UI_APP_ASSERT_CODE);
    (void)board_buttons_enable_isr(s_btn_q);
}

QueueHandle_t ui_buttons_queue(void)
{
    return s_btn_q;
}
