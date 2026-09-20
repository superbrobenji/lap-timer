/* app/pipeline.h -- the pipeline task (spec §4.3, §9.1).
 *
 * Core 1, prio 20, stack 8192, static. Runs the whole core pipeline: GPS + IMU ingest, time base
 * (tb), fusion (fus), the lap/drag engines, per-lap statistics (§9.4), event emission to evt_q, and
 * hands the logger the full lap_result_t / drag_result_t. On the moto_sim bench build it drives the
 * committed synthetic capture (gps_sim) and prints completed laps to the console; on the real GPS
 * variant it drives the sensor drivers. Started at boot step 12 (§4.7).
 */
#ifndef APP_PIPELINE_H
#define APP_PIPELINE_H

#include "core/types.h"

void pipeline_start(void);   /* create + start the task (boot step 12) */

/* Snapshot of the completed laps this session (for `dbg laps`; newest last). Copies up to `max`
 * lap_result_t into `out` and returns the number copied. Safe to call from another task. */
int  pipeline_laps_snapshot(lap_result_t *out, int max);

/* True once the pipeline has ingested at least one GPS fix since boot (set-once, monotonic). One
 * of the §19.4 conditions the supervisor gates a pending-OTA validation on. Safe from any task. */
bool pipeline_gps_seen(void);

#endif /* APP_PIPELINE_H */
