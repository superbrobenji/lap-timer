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

/* Number of completed laps available (== pipeline_laps_snapshot's return with an unbounded max);
 * the ring keeps the newest PIPE_LAPS_KEEP. Use as the loop bound for pipeline_lap_at. */
int  pipeline_lap_count(void);
/* Copy the `index`-th completed lap (0-based, SAME newest-last order pipeline_laps_snapshot yields
 * at out[index]) into `*out`, using the SAME F4 seqlock retry. Returns 0 on success, <0 if index
 * is out of range for the current ring. Lets a reader stream laps without a full snapshot array. */
int  pipeline_lap_at(int index, lap_result_t *out);

#endif /* APP_PIPELINE_H */
