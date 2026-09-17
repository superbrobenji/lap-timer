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

void logger_start(void);    /* create + start the task (boot step 12) */
void logger_notify(void);   /* wake the logger (task notification); safe from any task */

#endif /* APP_LOGGER_H */
