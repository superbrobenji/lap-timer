#ifndef REPLAY_REPLAY_H
#define REPLAY_REPLAY_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "core/ses.h"
#include "core/types.h"
#include "core/trk.h"

const char *replay_version(void);                 /* == core_version() */

/* Summary of one .log (session 2.1 skeleton; session 2.7 adds the engine replay). */
typedef struct {
    ses_hdr_t hdr;      int have_hdr;
    ses_venue_t venue;  int have_venue;
    uint32_t n_frames, n_bad, n_by_type[128];
    uint32_t n_fix, n_fix_valid, n_fused, n_lap, n_sector, n_drag_run, n_drag_gate, n_event, n_time_map;
    int64_t  first_fix_gps_us, last_fix_gps_us;   /* 0 when no fix */
    int32_t  max_gspeed_mms;
    uint16_t n_laps_listed;
    lap_result_t laps[64];                         /* first 64 LAP records */
    ses_end_t end;      int have_end;
} replay_summary_t;

int  replay_summarize_file(const char *path, replay_summary_t *out);   /* 0 / -1 on I/O error */
void replay_print_text(const replay_summary_t *s, FILE *f);
void replay_print_json(const replay_summary_t *s, FILE *f);            /* one JSON object, keys documented in replay_summary.c */
/* --------- Engine replay (session 2.7, spec §22.2) ---------
 * replay_run decodes a .log and drives the lap or drag engine in the §9.1 call order (see
 * replay_run.c). Because a synthetic .log has FIX_* + FUSED but no raw IMU, fusion is NOT re-run:
 * decoded FUSED samples feed the engines directly. Lap mode needs a venue (from --venue-json or
 * --venue). The output is deterministic and printed by replay_print_run_json, which the
 * test/data/<name>.expected.json fixtures compare against byte-for-byte. */

enum { REPLAY_MODE_SUMMARY = 0, REPLAY_MODE_LAP = 1, REPLAY_MODE_DRAG = 2 };

#define REPLAY_MAX_LAPS      256
#define REPLAY_MAX_RUNS      16
#define REPLAY_RUN_JSON_CAP  262144       /* 256 laps × sectors + scalars fit with headroom */

typedef struct {
    uint16_t lap_no;
    uint8_t  flags;                        /* LAP_F_* */
    uint8_t  n_sectors;                    /* splits = sector gates + 1 */
    int64_t  start_gps_us;                 /* S/F crossing that opened the lap */
    int64_t  end_gps_us;                   /* S/F crossing that closed it */
    uint32_t time_ms;
    uint32_t sector_ms[LAP_MAX_SECTORS + 1];
    int64_t  sector_gps_us[LAP_MAX_SECTORS];   /* absolute crossing gps_us of each sector gate, in order */
    uint8_t  n_sector_cross;               /* sector-gate crossings captured this lap */
} replay_lap_t;

typedef struct {
    uint16_t        run_no;
    int64_t         t0_gps_us;             /* launch instant (back-dated by the engine) */
    uint8_t         flags;                 /* DRAG_F_* */
    uint8_t         n_gates;
    uint16_t        trap_cms;
    drag_gate_res_t gates[DRAG_MAX_GATES];
} replay_drag_t;

typedef struct {
    int       mode;                        /* REPLAY_MODE_LAP / _DRAG */
    uint32_t  n_frames, n_bad;
    uint32_t  n_by_type[128];
    ses_hdr_t hdr; int have_hdr;
    uint32_t  n_fix, n_fix_valid, n_fused, n_events;
    int64_t   first_fix_gps_us, last_fix_gps_us;
    int32_t   max_gspeed_mms;
    uint16_t  venue_id, layout_id;         /* from the lap engine's EV_VENUE_FOUND / EV_LAYOUT_LOCKED */
    uint16_t  n_laps; replay_lap_t  laps[REPLAY_MAX_LAPS];
    uint16_t  n_runs; replay_drag_t runs[REPLAY_MAX_RUNS];
} replay_run_t;

/* mode is REPLAY_MODE_LAP or _DRAG; venue is required (and used) only in lap mode.
 * Returns 0, -1 on an I/O error, -2 if lap mode was asked with no venue, -3 on a bad mode. */
int  replay_run(const char *path, int mode, const trk_venue_t *venue, replay_run_t *out);
int  replay_run_mem(const uint8_t *buf, size_t n, int mode, const trk_venue_t *venue, replay_run_t *out);
void replay_print_run_json(const replay_run_t *r, FILE *f);

#endif
