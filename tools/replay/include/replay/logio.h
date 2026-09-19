#ifndef REPLAY_LOGIO_H
#define REPLAY_LOGIO_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "core/types.h"
#include "core/ses.h"

/* Session .log writer and reader built on the ses_* codecs (spec §12). Host only. */

typedef struct {
    FILE    *f;                 /* file mode, or NULL */
    uint8_t *mem; size_t cap;   /* memory mode, or NULL */
    size_t   len;               /* bytes written so far (both modes) */
    ses_fix_state_t   fix_st;
    ses_fused_state_t fused_st;
    uint32_t frames;
    int      err;               /* sticky: 0 ok, -1 after any failed write */
} logw_t;

int  logw_open_file(logw_t *w, const char *path);            /* 0 / -1 */
void logw_open_mem(logw_t *w, uint8_t *buf, size_t cap);      /* writes fail (err = -1) once cap is exceeded */
/* Each returns the frame length written, or -1 (and sets err). */
int  logw_hdr(logw_t *w, const ses_hdr_t *h);
int  logw_venue(logw_t *w, uint16_t venue_id, uint16_t layout_id, const char *name);
int  logw_time_map(logw_t *w, int64_t mono_us, int64_t gps_us, uint8_t quality);
int  logw_fix(logw_t *w, const gps_fix_t *fix);             /* FIX_KEY / FIX_DELTA via fix_st; also ses_fused_state_on_fix */
int  logw_fused(logw_t *w, const fused_sample_t *fs);
int  logw_lap(logw_t *w, const lap_result_t *lap);
int  logw_sector(logw_t *w, uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms);
int  logw_drag_run(logw_t *w, const drag_result_t *run);
int  logw_drag_gate(logw_t *w, uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm);
int  logw_event(logw_t *w, int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg);
int  logw_calib(logw_t *w, const ses_calib_t *c);
int  logw_mark(logw_t *w, int64_t gps_us, uint8_t kind);
int  logw_power(logw_t *w, int64_t mono_us, uint8_t state, uint16_t batt_mv);
int  logw_end(logw_t *w, int64_t gps_us, uint8_t reason);
int  logw_close(logw_t *w);                                  /* flushes and closes the file; returns err */

typedef struct {
    void (*on_hdr)(const ses_hdr_t *h, void *ctx);
    void (*on_venue)(const ses_venue_t *v, void *ctx);
    void (*on_time_map)(const ses_time_map_t *t, void *ctx);
    void (*on_fix)(const gps_fix_t *fix, void *ctx);          /* reconstructed absolute fix */
    void (*on_fused)(const fused_sample_t *fs, void *ctx);
    void (*on_lap)(const lap_result_t *lap, void *ctx);
    void (*on_sector)(const ses_sector_t *s, void *ctx);
    void (*on_drag_run)(const drag_result_t *run, void *ctx);
    void (*on_drag_gate)(const ses_drag_gate_t *g, void *ctx);
    void (*on_event)(const ses_event_t *e, void *ctx);
    void (*on_calib)(const ses_calib_t *c, void *ctx);
    void (*on_mark)(const ses_mark_t *m, void *ctx);
    void (*on_power)(const ses_power_t *p, void *ctx);
    void (*on_end)(const ses_end_t *e, void *ctx);
    void (*on_bad)(uint8_t type, uint8_t len, void *ctx);     /* framed OK but the decoder rejected it, or unknown type */
} logr_cb_t;                                                  /* any member may be NULL */

typedef struct {
    ses_reader_t      rd;
    ses_fix_state_t   fix_st;
    ses_fused_state_t fused_st;
    const logr_cb_t  *cb;
    void             *ctx;
    uint32_t n_frames;          /* frames the framing layer accepted */
    uint32_t n_bad;             /* decoder rejections + unknown types */
    uint32_t n_by_type[128];    /* accepted frames per record type */
} logr_t;

void logr_init(logr_t *r, const logr_cb_t *cb, void *ctx);
void logr_feed(logr_t *r, const uint8_t *buf, size_t n);
void logr_finish(logr_t *r);                                  /* ses_reader_finish + drain at EOF */
int  logr_read_file(logr_t *r, const char *path);             /* feed in 4096-byte chunks then finish; 0 / -1 on open or read error */
#endif
