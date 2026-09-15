#ifndef REPLAY_REPLAY_H
#define REPLAY_REPLAY_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "core/ses.h"
#include "core/types.h"

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
#endif
