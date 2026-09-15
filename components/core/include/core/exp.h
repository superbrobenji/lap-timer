#ifndef CORE_EXP_H
#define CORE_EXP_H
#include <stdint.h>
#include <stddef.h>
#include "core/ses.h"

enum { EXP_VBO = 1, EXP_NMEA = 2, EXP_JSON = 3 };
#define EXP_FULL 1
#define EXP_WINDOW 1024

typedef struct {
    char    session_id[11];
    char    fw[16];
    char    hwid[24];
    char    venue[32];
    char    layout[24];
    int64_t created_gps_us;
    uint8_t has_sf;
    double  sf_lat1, sf_lon1, sf_lat2, sf_lon2;
} exp_meta_t;

typedef struct {
    uint8_t  fmt;
    uint8_t  finished;
    exp_meta_t meta;
    /* output window */
    uint8_t  win[EXP_WINDOW];
    size_t   win_len, win_pos;
    /* decoder state shared by formats */
    ses_fix_state_t   fix_st;
    ses_fused_state_t fus_st;
    fused_sample_t    held;            /* latest fused values, sample-and-hold */
    uint8_t           have_held;
    /* JSON summary state */
    uint16_t  laps, runs;
    uint8_t   json_stage;              /* 0 header pending, 1 in laps, 2 in runs */
    ses_hdr_t hdr;
    uint8_t   have_hdr;
    char      venue_name[32];
    uint8_t   run_pending;
    uint8_t   run_gate_idx;            /* DRAG_RUN emission resumes after EXP_FULL */
} exp_t;

int  exp_open(exp_t *e, uint8_t fmt, const exp_meta_t *meta);
int  exp_feed(exp_t *e, uint8_t type, const uint8_t *payload, uint8_t len);   /* 0 consumed, EXP_FULL retry after pull, -1 error */
int  exp_pull(exp_t *e, uint8_t *out, size_t cap, size_t *n_out);           /* 0 ok (n_out may be 0), -1 error */
int  exp_finish(exp_t *e);                                                   /* may return EXP_FULL: pull, then call again. Returns 0 (no-op) if already finished, -1 if a DRAG_RUN frame is mid-emission (re-feed it first). */

/* helpers shared by format implementations (internal) */
int  exp_win_free(const exp_t *e);
int  exp_win_puts(exp_t *e, const char *s);                                  /* -1 if it does not fit (nothing written) */
void exp_civil_from_days(int64_t days, int *y, unsigned *m, unsigned *d);
void exp_utc_parts(int64_t gps_us, int *y, unsigned *mo, unsigned *d, unsigned *hh, unsigned *mm, unsigned *ss, unsigned *cs);
/* per-format hooks */
int  exp_vbo_open(exp_t *e);  int exp_vbo_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len);  int exp_vbo_finish(exp_t *e);
int  exp_nmea_open(exp_t *e); int exp_nmea_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len); int exp_nmea_finish(exp_t *e);
int  exp_json_open(exp_t *e); int exp_json_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len); int exp_json_finish(exp_t *e);
#endif
