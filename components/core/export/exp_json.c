#include "core/exp.h"
#include "core/jw.h"
#include "core/core.h"
#include <stdio.h>
#include <string.h>

/* Streams: {"id":"...","hdr":{...},"laps":[ ... ],"runs":[ ... ]}
 * stage 0: nothing emitted yet (waiting for SESSION_HDR / VENUE); 1: laps array open; 2: runs array open. */

#define EXP_ASSERT_CODE 0x0A60   /* new assertions in this module; 0x0A01 below predates this and is kept as-is */

static int emit(exp_t *e, jw_t *w)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(w != NULL, EXP_ASSERT_CODE, -1);
    return jw_overflow(w) ? -1 : exp_win_puts(e, (const char *)w->buf);
}

static int open_hdr(exp_t *e, const ses_hdr_t *h, const char *venue_name)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);      /* h/venue_name are legitimately NULL: no header/venue seen yet */
    char buf[400]; jw_t w; jw_init(&w, buf, sizeof buf);
    jw_obj_open(&w);
    jw_key(&w, "id"); jw_str(&w, e->meta.session_id);
    jw_key(&w, "hdr"); jw_obj_open(&w);
      jw_key(&w, "start_utc"); jw_int(&w, h ? h->start_gps_us / 1000000 : 0);
      jw_key(&w, "mode"); jw_str(&w, h && h->mode == 1 ? "drag" : "lap");
      jw_key(&w, "venue_id"); jw_uint(&w, h ? h->venue_id : 0);
      jw_key(&w, "layout_id"); jw_uint(&w, h ? h->layout_id : 0);
      jw_key(&w, "venue"); jw_str(&w, venue_name ? venue_name : "");
      jw_key(&w, "fw"); jw_str(&w, h ? h->fw : "");
      jw_key(&w, "gps_hz"); jw_uint(&w, h ? h->gps_hz : 0);
      jw_key(&w, "fused_hz"); jw_uint(&w, h ? h->fused_hz : 0);
    jw_obj_close(&w);
    jw_key(&w, "laps"); jw_arr_open(&w);
    /* leave the array open: strip the closing pieces by not calling jw_arr_close/jw_obj_close */
    e->json_stage = 1;
    return emit(e, &w);
}

int exp_json_open(exp_t *e)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    e->json_stage = 0; e->have_hdr = 0; e->venue_name[0] = '\0'; e->run_pending = 0; e->run_gate_idx = 0;
    return 0;
}

/* Handles a frame while json_stage==0 (header pending). Returns 1 if `type` was fully handled
 * here (the caller returns *ret as-is); 0 if the header was just opened (or the stage was
 * already past 0) and the frame must still be handled by the stage 1/2 logic below. e/p are not
 * re-checked here: exp_json_feed (this function's only caller) already validates them first. */
static int feed_stage0(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len, int *ret)
{
    CORE_ASSERT_RET(ret != NULL, EXP_ASSERT_CODE, 1);   /* *ret is written below: must have somewhere to go */
    if (type == SES_T_SESSION_HDR) {
        if (ses_decode_hdr(p, len, &e->hdr) == 1) e->have_hdr = 1;
        *ret = 0; return 1;
    }
    if (type == SES_T_VENUE) {
        ses_venue_t v;
        if (ses_decode_venue(p, len, &v) == 1) memcpy(e->venue_name, v.name, sizeof e->venue_name);
        *ret = 0; return 1;
    }
    if (type != SES_T_LAP && type != SES_T_DRAG_RUN && type != SES_T_END) { *ret = 0; return 1; }
    if (exp_win_free(e) < 400) { *ret = EXP_FULL; return 1; }
    if (open_hdr(e, e->have_hdr ? &e->hdr : NULL, e->venue_name[0] ? e->venue_name : NULL) < 0) { *ret = -1; return 1; }
    return 0;      /* fall through: handle this frame in stage 1/2 below */
}

static int feed_lap(exp_t *e, const uint8_t *p, uint8_t len)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(p != NULL || len == 0, EXP_ASSERT_CODE, -1);
    if (e->json_stage != 1) return 0;                     /* laps after runs began: ignore (log order guarantees this never happens) */
    if (exp_win_free(e) < 400) return EXP_FULL;
    lap_result_t lap; if (ses_decode_lap(p, len, &lap) != 1) return -1;
    CORE_ASSERT_RET(lap.n_sectors <= LAP_MAX_SECTORS + 1, EXP_ASSERT_CODE, -1);  /* rule 2: explicit bound on the loop below */
    char buf[400]; jw_t w; jw_init(&w, buf, sizeof buf);
    if (e->laps > 0) CORE_ASSERT_RET(exp_win_puts(e, ",") == 0, 0x0A01, -1);
    jw_obj_open(&w);
    jw_key(&w, "n"); jw_uint(&w, lap.lap_no);
    jw_key(&w, "ms"); jw_uint(&w, lap.time_ms);
    jw_key(&w, "valid"); jw_bool(&w, (lap.flags & LAP_F_VALID) != 0);
    jw_key(&w, "flags"); jw_uint(&w, lap.flags);
    jw_key(&w, "sectors"); jw_arr_open(&w); for (uint8_t i = 0; i < lap.n_sectors; i++) jw_uint(&w, lap.sector_ms[i]); jw_arr_close(&w);
    jw_key(&w, "stats"); jw_obj_open(&w);
      jw_key(&w, "max_speed_cms"); jw_uint(&w, lap.stats.max_speed_cms);
      jw_key(&w, "min_speed_cms"); jw_uint(&w, lap.stats.min_speed_cms);
      jw_key(&w, "lean_l"); jw_int(&w, lap.stats.max_lean_l_cdeg);
      jw_key(&w, "lean_r"); jw_int(&w, lap.stats.max_lean_r_cdeg);
      jw_key(&w, "glat"); jw_int(&w, lap.stats.max_glat_e3);
      jw_key(&w, "gacc"); jw_int(&w, lap.stats.max_gacc_e3);
      jw_key(&w, "gbrake"); jw_int(&w, lap.stats.max_gbrake_e3);
    jw_obj_close(&w);
    jw_obj_close(&w);
    e->laps++;
    return emit(e, &w);
}

/* Opens the run's outer JSON object exactly once: transitions stage 1->2 if needed, emits the
 * "n"/"t0_utc_us"/"rollout"/"trap_cms"/"gates":[ header, and arms run_pending/run_gate_idx. */
static int feed_drag_run_open(exp_t *e, const drag_result_t *run)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(run != NULL, EXP_ASSERT_CODE, -1);
    if (exp_win_free(e) < 200) return EXP_FULL;
    if (e->json_stage == 1) {
        CORE_ASSERT_RET(exp_win_puts(e, "],\"runs\":[") == 0, 0x0A01, -1);
        e->json_stage = 2;
    }
    if (e->runs > 0) CORE_ASSERT_RET(exp_win_puts(e, ",") == 0, 0x0A01, -1);
    char buf[200]; jw_t w; jw_init(&w, buf, sizeof buf);
    jw_obj_open(&w);
    jw_key(&w, "n"); jw_uint(&w, run->run_no);
    jw_key(&w, "t0_utc_us"); jw_int(&w, run->t0_gps_us);
    jw_key(&w, "rollout"); jw_bool(&w, (run->flags & DRAG_F_ROLLOUT) != 0);
    jw_key(&w, "trap_cms"); jw_uint(&w, run->trap_cms);
    jw_key(&w, "gates"); jw_arr_open(&w);
    if (emit(e, &w) < 0) return -1;
    e->run_pending = 1; e->run_gate_idx = 0;
    return 0;
}

/* Emits gates[run_gate_idx..n_gates) one object per call-in, resuming after a prior EXP_FULL
 * (the caller re-feeds the same frame). Rule 2: explicitly bounded by n_gates, itself asserted
 * within DRAG_MAX_GATES -- ses_decode_drag_run() already guarantees this, checked again here
 * because a corrupted `run` reaching this point must not walk off the end of gates[]. */
static int feed_drag_run_gates(exp_t *e, const drag_result_t *run)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(run != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(run->n_gates <= DRAG_MAX_GATES, EXP_ASSERT_CODE, -1);
    while (e->run_gate_idx < run->n_gates) {
        if (exp_win_free(e) < 120) return EXP_FULL;
        const drag_gate_res_t *g = &run->gates[e->run_gate_idx];
        char buf[120]; jw_t w; jw_init(&w, buf, sizeof buf);
        if (e->run_gate_idx > 0) CORE_ASSERT_RET(exp_win_puts(e, ",") == 0, 0x0A01, -1);
        jw_obj_open(&w);
        jw_key(&w, "id"); jw_uint(&w, g->gate_id);
        jw_key(&w, "ms"); jw_uint(&w, g->time_ms);
        jw_key(&w, "speed_cms"); jw_uint(&w, g->speed_cms);
        jw_key(&w, "dist_cm"); jw_uint(&w, g->dist_cm);
        jw_key(&w, "hit"); jw_bool(&w, g->hit != 0);
        jw_obj_close(&w);
        if (emit(e, &w) < 0) return -1;
        e->run_gate_idx++;
    }
    return 0;
}

static int feed_drag_run(exp_t *e, const uint8_t *p, uint8_t len)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(p != NULL || len == 0, EXP_ASSERT_CODE, -1);
    drag_result_t run; if (ses_decode_drag_run(p, len, &run) != 1) return -1;
    if (!e->run_pending) {
        int r = feed_drag_run_open(e, &run);
        if (r != 0) return r;
    }
    /* one gate object per step; after EXP_FULL the caller pulls and re-feeds the same frame, and we resume here */
    int r = feed_drag_run_gates(e, &run);
    if (r != 0) return r;
    if (exp_win_free(e) < 4) return EXP_FULL;
    CORE_ASSERT_RET(exp_win_puts(e, "]}") == 0, 0x0A01, -1);
    e->run_pending = 0; e->runs++;
    return 0;
}

int exp_json_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(p != NULL || len == 0, EXP_ASSERT_CODE, -1);
    if (e->json_stage == 0) {
        int ret;
        if (feed_stage0(e, type, p, len, &ret)) return ret;
    }
    if (type == SES_T_LAP)      return feed_lap(e, p, len);
    if (type == SES_T_DRAG_RUN) return feed_drag_run(e, p, len);
    return 0;
}

int exp_json_finish(exp_t *e)
{
    CORE_ASSERT_RET(e != NULL, EXP_ASSERT_CODE, -1);
    if (e->run_pending) return -1;
    if (exp_win_free(e) < 400) return EXP_FULL;
    if (e->json_stage == 0) { if (open_hdr(e, e->have_hdr ? &e->hdr : NULL, e->venue_name[0] ? e->venue_name : NULL) < 0) return -1; }
    if (e->json_stage == 1) {
        CORE_ASSERT_RET(exp_win_puts(e, "],\"runs\":[") == 0, 0x0A01, -1);
        e->json_stage = 2;
    }
    CORE_ASSERT_RET(exp_win_puts(e, "]}") == 0, 0x0A01, -1);
    return 0;
}
