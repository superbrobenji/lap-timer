/* app/logger.h -- logger task (spec §4.3, §13.3).
 *
 * Core 0, prio 8, stack 4096, static. Drains the §4.4 fix/fused rings and the logger's event
 * queue into 4 KB .log batches through core/ses, rebuilds the .sum atomically on LAP/DRAG_RUN,
 * fsyncs every 2 s, and evicts every 60 s (§12.5-12.7). Session open/close is commanded over
 * log_req_q. The producer (the pipeline task) calls logger_notify() after pushing to a ring so the
 * logger wakes before its 1000 ms timeout ("ring notify or 1000 ms").
 */
#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <stdint.h>

#include "core/ses.h"
#include "core/types.h"

void logger_start(void);    /* create + start the task (boot step 12) */
void logger_notify(void);   /* wake the logger (task notification); safe from any task */

/* #97 (§10.9), final review F-4/I-4: load /tracks/user.bin into the core/trk.h user track table.
 * Implemented in logger.c (the file that also writes it, tracks_save()) since it only reads, but
 * called from main/app_main.c's boot sequence -- right after trk_init() and strictly BEFORE
 * logger_start() creates the logger task -- not from inside the logger task's own boot init as
 * before: running both writes on app_main's own task, before any task that could race them even
 * exists, needs no concurrency premise at all (core/trk.h's ownership comment has the full
 * argument). Storage must already be mounted (boot_storage(), before boot_subsystems()). 0 ok
 * (including "no file -- nothing to load", a fresh device's normal case); -1 on an unexpected
 * open/read failure once the file was confirmed to exist. */
int  logger_load_tracks(void);

/* Pipeline -> logger, full engine results (3.4, resolving the 3.3 deferral). Each copies the result
 * onto result_q (cross-core safe) and wakes the logger; the logger writes the complete LAP (+ SECTOR)
 * / DRAG_RUN (+ DRAG_GATE) records (§12.3) and the real VENUE record. Safe from the pipeline task;
 * drop silently if the queue is momentarily full (the .log keeps the EVENT record either way). */
void logger_submit_lap(const lap_result_t *lap);
void logger_submit_drag(const drag_result_t *run);
void logger_set_venue(uint16_t venue_id, uint16_t layout_id, const char *name);

/* R4 producers, implemented beside logger_submit_lap/drag/set_venue: the pipeline owns the time base
 * (TIME_MAP, §12.3 0x0C) and the calibration (SESSION_HDR calib block + CALIB record, §12.3 0x0A). */
void logger_submit_time_map(int64_t mono_us, int64_t gps_us, uint8_t quality);
void logger_submit_calib(const ses_calib_t *c);

#endif /* APP_LOGGER_H */
