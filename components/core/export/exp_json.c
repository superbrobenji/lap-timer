#include "core/exp.h"
#include "core/jw.h"
#include <stdio.h>
#include <string.h>

/* Streams: {"id":"...","hdr":{...},"laps":[ ... ],"runs":[ ... ]}
 * stage 0: nothing emitted yet (waiting for SESSION_HDR / VENUE); 1: laps array open; 2: runs array open. */

static int emit(exp_t *e, jw_t *w) { return jw_overflow(w) ? -1 : exp_win_puts(e, (const char *)w->buf); }

static int open_hdr(exp_t *e, const ses_hdr_t *h, const char *venue_name)
{
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

int exp_json_open(exp_t *e) { e->json_stage = 0; e->have_hdr = 0; e->venue_name[0] = '\0'; e->run_pending = 0; e->run_gate_idx = 0; return 0; }

int exp_json_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len)
{
    if (e->json_stage == 0) {
        if (type == SES_T_SESSION_HDR) { if (ses_decode_hdr(p, len, &e->hdr) == 1) e->have_hdr = 1; return 0; }
        if (type == SES_T_VENUE) { if (len == 36) { memcpy(e->venue_name, p + 4, 32); e->venue_name[31] = '\0'; } return 0; }
        if (type != SES_T_LAP && type != SES_T_DRAG_RUN && type != SES_T_END) return 0;
        if (exp_win_free(e) < 400) return EXP_FULL;
        if (open_hdr(e, e->have_hdr ? &e->hdr : NULL, e->venue_name[0] ? e->venue_name : NULL) < 0) return -1;
        /* fall through to handle this frame in stage 1/2 */
    }
    if (type == SES_T_LAP) {
        if (e->json_stage != 1) return 0;                     /* laps after runs began: ignore (log order guarantees this never happens) */
        if (exp_win_free(e) < 400) return EXP_FULL;
        lap_result_t lap; if (ses_decode_lap(p, len, &lap) != 1) return -1;
        char buf[400]; jw_t w; jw_init(&w, buf, sizeof buf);
        if (e->laps > 0) exp_win_puts(e, ",");
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
    if (type == SES_T_DRAG_RUN) {
        drag_result_t run; if (ses_decode_drag_run(p, len, &run) != 1) return -1;
        if (!e->run_pending) {
            if (exp_win_free(e) < 200) return EXP_FULL;
            if (e->json_stage == 1) { exp_win_puts(e, "],\"runs\":["); e->json_stage = 2; }
            if (e->runs > 0) exp_win_puts(e, ",");
            char buf[200]; jw_t w; jw_init(&w, buf, sizeof buf);
            jw_obj_open(&w);
            jw_key(&w, "n"); jw_uint(&w, run.run_no);
            jw_key(&w, "t0_utc_us"); jw_int(&w, run.t0_gps_us);
            jw_key(&w, "rollout"); jw_bool(&w, (run.flags & DRAG_F_ROLLOUT) != 0);
            jw_key(&w, "trap_cms"); jw_uint(&w, run.trap_cms);
            jw_key(&w, "gates"); jw_arr_open(&w);
            if (emit(e, &w) < 0) return -1;
            e->run_pending = 1; e->run_gate_idx = 0;
        }
        /* one gate object per step; after EXP_FULL the caller pulls and re-feeds the same frame, and we resume here */
        while (e->run_gate_idx < run.n_gates) {
            if (exp_win_free(e) < 120) return EXP_FULL;
            const drag_gate_res_t *g = &run.gates[e->run_gate_idx];
            char buf[120]; jw_t w; jw_init(&w, buf, sizeof buf);
            if (e->run_gate_idx > 0) exp_win_puts(e, ",");
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
        if (exp_win_free(e) < 4) return EXP_FULL;
        exp_win_puts(e, "]}");
        e->run_pending = 0; e->runs++;
        return 0;
    }
    return 0;
}

int exp_json_finish(exp_t *e)
{
    if (e->run_pending) return -1;
    if (exp_win_free(e) < 400) return EXP_FULL;
    if (e->json_stage == 0) { if (open_hdr(e, e->have_hdr ? &e->hdr : NULL, e->venue_name[0] ? e->venue_name : NULL) < 0) return -1; }
    if (e->json_stage == 1) { exp_win_puts(e, "],\"runs\":["); e->json_stage = 2; }
    exp_win_puts(e, "]}");
    return 0;
}
