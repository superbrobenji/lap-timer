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
/* True once the pipeline has ingested at least one GPS fix since boot (set-once, monotonic). One
 * of the §19.4 conditions the supervisor gates a pending-OTA validation on. Safe from any task. */
bool pipeline_gps_seen(void);

/* Best-known sector splits + theoretical best for the locked layout (design §2). Refreshed on
 * every EV_LAP_COMPLETE (valid or not -- the engine only updates its own bests on a valid lap, so
 * this simply mirrors it) and cleared when the venue/layout changes. Guarded by the same F4
 * seqlock (s_laps_seq) as pipeline_laps_snapshot/pipeline_lap_at. */
typedef struct {
    uint32_t best_sector_ms[LAP_MAX_SECTORS + 1];   /* from the engine's best-sector table */
    bool     have_best_sector[LAP_MAX_SECTORS + 1];
    uint8_t  n_sectors;                             /* splits in the locked layout (n_sec + 1) */
    uint32_t theo_ms;                                /* lap_theoretical_best_ms(); 0 = not yet */
} pipe_best_t;
/* Copy the current best-sector/theoretical-best record into *out. Safe to call from another task.
 * Returns 0 on success (always succeeds once pipeline_init has run; s_best is valid, if all-zero,
 * from static init before the first lap/venue). */
int  pipeline_best_snapshot(pipe_best_t *out);

/* Drag run in progress (or last frozen) + the session-best value per gate (design §2 follow-up:
 * the DRAG page 1/2 producer). Refreshed once per pipeline step that produced at least one drag
 * event (ARMED/LAUNCH/GATE/DONE alike), under the same F4 seqlock as pipe_best_t. */
typedef struct {
    drag_result_t current;                          /* zeroed n_gates when no run yet */
    uint32_t      best_time_ms[DRAG_MAX_GATES];      /* index = gate id - 1; BRAKE: stopping dist_cm */
    bool          have_best[DRAG_MAX_GATES];
    uint8_t       state;                             /* drag_state(&s_drag) (DRAG_ST_*, core/drag.h)
                                                       * at the instant this snapshot was published --
                                                       * the engine's own state, not a ui-side mirror
                                                       * (#95, bench B4-F1) */
} pipe_drag_t;
/* Copy the current drag run/session-best record into *out. Safe to call from another task. Returns
 * 0 on success (always succeeds; s_dragsnap is valid, if all-zero, before the first drag sample). */
int  pipeline_drag_snapshot(pipe_drag_t *out);

#endif /* APP_PIPELINE_H */
