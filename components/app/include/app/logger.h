/* app/logger.h -- logger task (spec §4.3, §13.3).
 *
 * Core 0, prio 8, stack 4096, static. Drains the §4.4 fix/fused rings and the logger's event
 * queue into 4 KB .log batches through core/ses, rebuilds the .sum atomically on LAP/DRAG_RUN,
 * fsyncs every 2 s, and evicts every 60 s (§12.5-12.7). Session open/close is commanded over
 * log_req_q. The producer (pipeline, 3.4) calls logger_notify() after pushing to a ring so the
 * logger wakes before its 1000 ms timeout ("ring notify or 1000 ms").
 */
#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <stdint.h>

#include "core/types.h"

void logger_start(void);    /* create + start the task (boot step 12) */
void logger_notify(void);   /* wake the logger (task notification); safe from any task */

/* Pipeline -> logger, full engine results (3.4, resolving the 3.3 deferral). Each copies the result
 * onto result_q (cross-core safe) and wakes the logger; the logger writes the complete LAP (+ SECTOR)
 * / DRAG_RUN (+ DRAG_GATE) records (§12.3) and the real VENUE record. Safe from the pipeline task;
 * drop silently if the queue is momentarily full (the .log keeps the EVENT record either way). */
void logger_submit_lap(const lap_result_t *lap);
void logger_submit_drag(const drag_result_t *run);
void logger_set_venue(uint16_t venue_id, uint16_t layout_id, const char *name);

#endif /* APP_LOGGER_H */
