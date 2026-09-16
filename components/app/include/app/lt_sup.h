/* app/lt_sup.h -- shared runtime state + supervisor (spec §4.3, §4.4, §17.2, §17.4).
 *
 * hb[] and sys_flags are the cross-task state of §4.4: every task bumps its heartbeat, the
 * supervisor watches them and owns the fault flags. btn_q is the button ISR -> ui channel.
 */
#ifndef APP_LT_SUP_H
#define APP_LT_SUP_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Heartbeat slots -- one per canonical §4.3 task (hb[6], §4.4). A task bumps g_hb[HB_x] once
 * per loop; the supervisor detects a stall when the count stops advancing. In 3.2 only the
 * supervisor is live; pipeline/logger/ui/conn/power register as they land (3.4+). */
enum { HB_PIPELINE = 0, HB_SUPERVISOR, HB_LOGGER, HB_UI, HB_CONN, HB_POWER, HB_COUNT };
extern volatile uint32_t g_hb[HB_COUNT];

/* sys_flags bits (§17.4), backed by an atomic u32. */
enum {
    SYS_GPS_DEAD = 0, SYS_GPS_NOFIX, SYS_IMU_DEAD, SYS_IMU_SUSPECT, SYS_DISP_DEAD,
    SYS_STORAGE_DEAD, SYS_STORAGE_FULL, SYS_STORAGE_DEGRADED, SYS_BATT_LOW,
    SYS_SAFE_MODE, SYS_HEAP_LOW, SYS_DISP_TEMP_THROTTLE, SYS_OTA_PENDING, SYS_FUSION_DISAGREE,
};
uint32_t sys_flags_get(void);
void     sys_flags_set(uint8_t bit);
void     sys_flags_clear(uint8_t bit);

/* btn_q: button ISR -> ui (§4.4, depth 8, 4 B item). Created in boot step 11 by lt_queues_init();
 * the button_evt_t layout is finalised by the ui (3.4) -- 3.2 fixes only its 4-byte size. */
typedef struct { uint8_t mask; uint8_t flags; uint16_t age_ms; } button_evt_t;
extern QueueHandle_t g_btn_q;
void lt_queues_init(void);

/* Supervisor task: core 0, prio 22, stack 3072, 1000 ms (§4.3), subscribed to the task WDT. */
void sup_start(void);
/* Register a task for heartbeat-stall watch (§17.2). stall_s = HB_STALL_S for that task. */
int  sup_register_task(uint8_t hb_id, TaskHandle_t task, uint32_t stall_s);

/* Install the core assertion hook (§17.9): core asserts -> NVS error ring. Call once at boot. */
void sup_install_assert_hook(void);

#endif /* APP_LT_SUP_H */
