#include "replay/replay.h"
#include "replay/logio.h"
#include "core/lap.h"
#include "core/drag.h"
#include "core/fus.h"
#include "core/tb.h"
#include "core/geo.h"
#include "core/consts.h"
#include "core/jw.h"
#include <stdlib.h>
#include <string.h>

/* replay_run (spec §22.2): decode a .log and drive the engines in the exact §9.1 on_fix / on_raw call
 * order minus IDF. The synthetic .log carries FIX_* and FUSED (from truth) but no raw IMU, so fusion
 * (fus_step / on_raw) is not re-run: the decoded FUSED samples are fed straight to the engines (drag
 * via drag_on_fused; lap takes the latest FUSED as its fs argument). Reading raw IMU through fus_step
 * (`--imu capture.csv`) is future work (§22.2). fus_set_gps_speed is still called on every fix to
 * mirror the pipeline order faithfully; with no fus_step it has no effect on the results. */

#define MMS_TO_KMH 0.0036

typedef struct {
    replay_run_t *out;
    int           mode;
    lap_t         lap;
    drag_t        drag;
    fus_t         fus;
    tb_t          tb;

    fused_sample_t latest_fused;
    bool           have_fused;

    /* §6.5 validity state */
    bool    have_last_valid;
    int64_t last_valid_gps_us;
    double  last_valid_lat, last_valid_lon;

    /* §9.1 step 5 motion edge */
    bool moving, have_moving;

    /* per-lap sector-crossing accumulation (§10.5/§10.7) */
    int64_t sec_acc[LAP_MAX_SECTORS + 1];
    int     sec_acc_n;
} rr_ctx_t;

/* §6.5 fix validity rule, recomputed here exactly as the pipeline does. Updates the last-valid state. */
static bool compute_validity(rr_ctx_t *c, const gps_fix_t *fix)
{
    bool ok = fix->fix_type == 3
           && (fix->flags & GPS_FLAG_FIXOK)
           && (fix->flags & GPS_FLAG_TIME)
           && (fix->flags & GPS_FLAG_DATE)
           && fix->gps_us > 0
           && fix->hacc_mm <= (uint32_t)(FIX_HACC_MAX_M * 1000)
           && fix->sats >= FIX_MIN_SATS
           && fix->gspeed_mms <= (int32_t)(FIX_MAX_SPEED_MPS * 1000);
    if (ok && c->have_last_valid) {
        if (fix->gps_us <= c->last_valid_gps_us) ok = false;    /* monotonic */
        else {
            double lat = (double)fix->lat_e7 / 1e7, lon = (double)fix->lon_e7 / 1e7;
            double dt_s = (double)(fix->gps_us - c->last_valid_gps_us) / 1e6;
            double d = geo_dist_m(c->last_valid_lat, c->last_valid_lon, lat, lon);
            if (d > (double)FIX_MAX_JUMP_MPS * dt_s + 20.0) ok = false;   /* jump */
        }
    }
    if (ok) {
        c->have_last_valid = true;
        c->last_valid_gps_us = fix->gps_us;
        c->last_valid_lat = (double)fix->lat_e7 / 1e7;
        c->last_valid_lon = (double)fix->lon_e7 / 1e7;
    }
    return ok;
}

static void snapshot_run(replay_drag_t *R, const drag_result_t *cur)
{
    memset(R, 0, sizeof *R);
    R->run_no    = cur->run_no;
    R->t0_gps_us = cur->t0_gps_us;
    R->flags     = cur->flags;
    R->n_gates   = cur->n_gates;
    R->trap_cms  = cur->trap_cms;
    for (uint8_t i = 0; i < cur->n_gates && i < DRAG_MAX_GATES; i++) R->gates[i] = cur->gates[i];
}

static void engine_cb(const event_t *ev, void *vctx)
{
    rr_ctx_t *c = (rr_ctx_t *)vctx;
    c->out->n_events++;
    if (c->mode == REPLAY_MODE_LAP) {
        switch (ev->type) {
        case EV_VENUE_FOUND:   c->out->venue_id = ev->arg16; break;
        case EV_LAYOUT_LOCKED: c->out->layout_id = ev->arg16; break;
        case EV_SECTOR:
            if (c->sec_acc_n < LAP_MAX_SECTORS + 1) c->sec_acc[c->sec_acc_n++] = ev->gps_us;
            break;
        case EV_LAP_COMPLETE: {
            const lap_result_t *p = lap_prev(&c->lap);
            if (p && c->out->n_laps < REPLAY_MAX_LAPS) {
                replay_lap_t *L = &c->out->laps[c->out->n_laps++];
                memset(L, 0, sizeof *L);
                L->lap_no       = p->lap_no;
                L->flags        = p->flags;
                L->n_sectors    = p->n_sectors;
                L->start_gps_us = p->start_gps_us;
                L->end_gps_us   = ev->gps_us;
                L->time_ms      = p->time_ms;
                for (uint8_t i = 0; i < p->n_sectors && i <= LAP_MAX_SECTORS; i++)
                    L->sector_ms[i] = p->sector_ms[i];
                int nc = c->sec_acc_n < LAP_MAX_SECTORS ? c->sec_acc_n : LAP_MAX_SECTORS;
                L->n_sector_cross = (uint8_t)nc;
                for (int i = 0; i < nc; i++) L->sector_gps_us[i] = c->sec_acc[i];
            }
            c->sec_acc_n = 0;
            break;
        }
        default: break;
        }
    } else if (c->mode == REPLAY_MODE_DRAG) {
        const drag_result_t *cur = drag_current(&c->drag);
        if (ev->type == EV_DRAG_DONE) {
            if (cur && c->out->n_runs < REPLAY_MAX_RUNS) snapshot_run(&c->out->runs[c->out->n_runs++], cur);
        } else if (ev->type == EV_DRAG_GATE && cur && c->out->n_runs > 0 &&
                   c->out->runs[c->out->n_runs - 1].run_no == cur->run_no) {
            /* the braking (100-0) gate can complete after DONE; keep the recorded run in sync. */
            snapshot_run(&c->out->runs[c->out->n_runs - 1], cur);
        }
    }
}

static void on_hdr(const ses_hdr_t *h, void *vctx)
{
    rr_ctx_t *c = (rr_ctx_t *)vctx;
    c->out->hdr = *h;
    c->out->have_hdr = 1;
}

static void on_fix(const gps_fix_t *fix_in, void *vctx)
{
    rr_ctx_t *c = (rr_ctx_t *)vctx;
    gps_fix_t fix = *fix_in;
    bool valid = compute_validity(c, &fix);
    fix.valid = valid ? 1u : 0u;

    if (c->out->n_fix == 0) c->out->first_fix_gps_us = fix.gps_us;
    c->out->last_fix_gps_us = fix.gps_us;
    c->out->n_fix++;
    if (valid) c->out->n_fix_valid++;
    if (fix.gspeed_mms > c->out->max_gspeed_mms) c->out->max_gspeed_mms = fix.gspeed_mms;

    /* §9.1 on_fix order: validity → tb_on_fix → fus_set_gps_speed → active engine → motion edge. */
    if (valid) tb_on_fix(&c->tb, fix.gps_us, fix.mono_us, 0);
    fus_set_gps_speed(&c->fus, (float)fix.gspeed_mms / 1000.0f,
                      (float)fix.head_e5 / 1e5f, fix.mono_us, valid);
    if (c->mode == REPLAY_MODE_LAP) {
        event_t evs[LAP_EVT_MAX];
        int nev = 0;
        lap_on_fix(&c->lap, &fix, c->have_fused ? &c->latest_fused : NULL, evs, LAP_EVT_MAX, &nev);
        for (int i = 0; i < nev; i++) engine_cb(&evs[i], c);
    } else if (c->mode == REPLAY_MODE_DRAG) {
        drag_on_fix(&c->drag, &fix);
    }

    bool moving = valid && ((double)fix.gspeed_mms * MMS_TO_KMH) > (double)MOVING_SPEED_KMH;
    if (!c->have_moving || moving != c->moving) {
        c->have_moving = true;
        c->moving = moving;
        event_t ev = { moving ? (uint8_t)EV_MOTION : (uint8_t)EV_STILL, 0, 0, fix.gps_us, fix.mono_us, 0, 0 };
        engine_cb(&ev, c);          /* faithfully mirror the pipeline's motion-edge emission */
    }
}

static void on_fused(const fused_sample_t *fs, void *vctx)
{
    rr_ctx_t *c = (rr_ctx_t *)vctx;
    c->latest_fused = *fs;
    c->have_fused = true;
    c->out->n_fused++;
    if (c->mode == REPLAY_MODE_DRAG) {
        event_t evs[DRAG_EVT_MAX];
        int nev = 0;
        drag_on_fused(&c->drag, fs, evs, DRAG_EVT_MAX, &nev);
        for (int i = 0; i < nev; i++) engine_cb(&evs[i], c);
    }
}

static int run_feed(rr_ctx_t *c, const char *path, const uint8_t *buf, size_t n)
{
    static const logr_cb_t cb = {
        .on_hdr = on_hdr, .on_venue = NULL, .on_time_map = NULL, .on_fix = on_fix,
        .on_fused = on_fused, .on_lap = NULL, .on_sector = NULL, .on_drag_run = NULL,
        .on_drag_gate = NULL, .on_event = NULL, .on_calib = NULL, .on_mark = NULL,
        .on_power = NULL, .on_end = NULL, .on_bad = NULL,
    };
    logr_t r;
    logr_init(&r, &cb, c);
    if (path) {
        if (logr_read_file(&r, path) != 0) return -1;
    } else {
        logr_feed(&r, buf, n);
        logr_finish(&r);
    }
    c->out->n_frames = r.n_frames;
    c->out->n_bad = r.n_bad + r.rd.frames_bad;
    memcpy(c->out->n_by_type, r.n_by_type, sizeof c->out->n_by_type);

    /* A run that launched but never reached DONE (e.g. a truncated log) is not captured by any
     * EV_DRAG_DONE; record its in-progress result once, from the still-live drag_current. */
    if (c->mode == REPLAY_MODE_DRAG && c->out->n_runs == 0) {
        const drag_result_t *cur = drag_current(&c->drag);
        if (cur && cur->run_no > 0 && c->out->n_runs < REPLAY_MAX_RUNS)
            snapshot_run(&c->out->runs[c->out->n_runs++], cur);
    }
    return 0;
}

static int run_common(rr_ctx_t *c, int mode, const trk_venue_t *venue, replay_run_t *out,
                      const char *path, const uint8_t *buf, size_t n)
{
    if (mode != REPLAY_MODE_LAP && mode != REPLAY_MODE_DRAG) return -3;
    if (mode == REPLAY_MODE_LAP && !venue) return -2;
    memset(out, 0, sizeof *out);
    out->mode = mode;
    memset(c, 0, sizeof *c);
    c->out = out;
    c->mode = mode;
    tb_init(&c->tb);
    fus_init(&c->fus, NULL, 1);
    if (mode == REPLAY_MODE_LAP) {
        out->venue_id = venue->id;
        lap_init(&c->lap, NULL);
        lap_set_venue(&c->lap, venue);
    } else {
        drag_init(&c->drag, NULL);
    }
    return run_feed(c, path, buf, n);
}

int replay_run(const char *path, int mode, const trk_venue_t *venue, replay_run_t *out)
{
    if (!path || !out) return -1;
    rr_ctx_t *c = (rr_ctx_t *)malloc(sizeof *c);
    if (!c) return -1;
    int rc = run_common(c, mode, venue, out, path, NULL, 0);
    free(c);
    return rc;
}

int replay_run_mem(const uint8_t *buf, size_t n, int mode, const trk_venue_t *venue, replay_run_t *out)
{
    if ((!buf && n) || !out) return -1;
    rr_ctx_t *c = (rr_ctx_t *)malloc(sizeof *c);
    if (!c) return -1;
    int rc = run_common(c, mode, venue, out, NULL, buf, n);
    free(c);
    return rc;
}

/* ---------------- deterministic JSON emitter ---------------- */

void replay_print_run_json(const replay_run_t *r, FILE *f)
{
    static const char hexdig[] = "0123456789abcdef";
    char *buf = (char *)malloc(REPLAY_RUN_JSON_CAP);
    if (!buf) { fputs("{\"error\":\"overflow\"}\n", f); return; }
    jw_t w;
    jw_init(&w, buf, REPLAY_RUN_JSON_CAP);
    jw_obj_open(&w);

    jw_key(&w, "version"); jw_str(&w, replay_version());
    jw_key(&w, "mode"); jw_str(&w, r->mode == REPLAY_MODE_DRAG ? "drag" : "lap");
    jw_key(&w, "frames"); jw_uint(&w, r->n_frames);
    jw_key(&w, "bad_frames"); jw_uint(&w, r->n_bad);

    jw_key(&w, "by_type"); jw_obj_open(&w);
    for (unsigned t = 0; t < sizeof r->n_by_type / sizeof r->n_by_type[0]; t++) {
        if (r->n_by_type[t] == 0) continue;
        char key[3] = { hexdig[(t >> 4) & 0xFu], hexdig[t & 0xFu], '\0' };
        jw_key(&w, key); jw_uint(&w, r->n_by_type[t]);
    }
    jw_obj_close(&w);

    jw_key(&w, "hdr");
    if (r->have_hdr) {
        jw_obj_open(&w);
        jw_key(&w, "session_id"); jw_str(&w, r->hdr.session_id);
        jw_key(&w, "mode"); jw_uint(&w, r->hdr.mode);
        jw_key(&w, "variant"); jw_uint(&w, r->hdr.variant);
        jw_key(&w, "venue_id"); jw_uint(&w, r->hdr.venue_id);
        jw_key(&w, "layout_id"); jw_uint(&w, r->hdr.layout_id);
        jw_key(&w, "gps_hz"); jw_uint(&w, r->hdr.gps_hz);
        jw_key(&w, "fused_hz"); jw_uint(&w, r->hdr.fused_hz);
        jw_key(&w, "start_gps_us"); jw_int(&w, r->hdr.start_gps_us);
        jw_obj_close(&w);
    } else jw_null(&w);

    jw_key(&w, "fix"); jw_obj_open(&w);
    jw_key(&w, "n"); jw_uint(&w, r->n_fix);
    jw_key(&w, "valid"); jw_uint(&w, r->n_fix_valid);
    jw_key(&w, "first_gps_us"); jw_int(&w, r->first_fix_gps_us);
    jw_key(&w, "last_gps_us"); jw_int(&w, r->last_fix_gps_us);
    jw_key(&w, "max_gspeed_mms"); jw_int(&w, r->max_gspeed_mms);
    jw_obj_close(&w);
    jw_key(&w, "fused"); jw_uint(&w, r->n_fused);
    jw_key(&w, "events"); jw_uint(&w, r->n_events);

    if (r->mode == REPLAY_MODE_DRAG) {
        jw_key(&w, "runs"); jw_arr_open(&w);
        for (uint16_t i = 0; i < r->n_runs; i++) {
            const replay_drag_t *R = &r->runs[i];
            jw_obj_open(&w);
            jw_key(&w, "run_no"); jw_uint(&w, R->run_no);
            jw_key(&w, "t0_gps_us"); jw_int(&w, R->t0_gps_us);
            jw_key(&w, "flags"); jw_uint(&w, R->flags);
            jw_key(&w, "trap_cms"); jw_uint(&w, R->trap_cms);
            jw_key(&w, "gates"); jw_arr_open(&w);
            for (uint8_t g = 0; g < R->n_gates; g++) {
                const drag_gate_res_t *G = &R->gates[g];
                jw_obj_open(&w);
                jw_key(&w, "id"); jw_uint(&w, G->gate_id);
                jw_key(&w, "hit"); jw_uint(&w, G->hit);
                jw_key(&w, "time_ms"); jw_uint(&w, G->time_ms);
                jw_key(&w, "speed_cms"); jw_uint(&w, G->speed_cms);
                jw_key(&w, "dist_cm"); jw_uint(&w, G->dist_cm);
                jw_obj_close(&w);
            }
            jw_arr_close(&w);
            jw_obj_close(&w);
        }
        jw_arr_close(&w);
        jw_key(&w, "n_runs"); jw_uint(&w, r->n_runs);
    } else {
        jw_key(&w, "venue"); jw_obj_open(&w);
        jw_key(&w, "id"); jw_uint(&w, r->venue_id);
        jw_key(&w, "layout_id"); jw_uint(&w, r->layout_id);
        jw_obj_close(&w);
        jw_key(&w, "laps"); jw_arr_open(&w);
        for (uint16_t i = 0; i < r->n_laps; i++) {
            const replay_lap_t *L = &r->laps[i];
            jw_obj_open(&w);
            jw_key(&w, "lap_no"); jw_uint(&w, L->lap_no);
            jw_key(&w, "flags"); jw_uint(&w, L->flags);
            jw_key(&w, "start_gps_us"); jw_int(&w, L->start_gps_us);
            jw_key(&w, "end_gps_us"); jw_int(&w, L->end_gps_us);
            jw_key(&w, "time_ms"); jw_uint(&w, L->time_ms);
            jw_key(&w, "n_sectors"); jw_uint(&w, L->n_sectors);
            jw_key(&w, "sector_ms"); jw_arr_open(&w);
            for (uint8_t k = 0; k < L->n_sectors; k++) jw_uint(&w, L->sector_ms[k]);
            jw_arr_close(&w);
            jw_key(&w, "sector_gps_us"); jw_arr_open(&w);
            for (uint8_t k = 0; k < L->n_sector_cross; k++) jw_int(&w, L->sector_gps_us[k]);
            jw_arr_close(&w);
            jw_obj_close(&w);
        }
        jw_arr_close(&w);
        jw_key(&w, "n_laps"); jw_uint(&w, r->n_laps);
    }

    jw_obj_close(&w);
    if (jw_overflow(&w)) fputs("{\"error\":\"overflow\"}\n", f);
    else { fputs(buf, f); fputc('\n', f); }
    free(buf);
}
