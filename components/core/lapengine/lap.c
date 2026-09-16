#include "core/lap.h"
#include <math.h>
#include <string.h>

/* Lap engine (spec §10). Session 2.4: venue detection and arming (part 1), S/F crossing and lap
 * completion (part 2), pit detection and Doppler distance integration (part 3). Sectors, layout
 * disambiguation, deltas, theoretical best, on-device creation, RTC and predictive delta are
 * session 2.5. */

/* ---- configuration and lifecycle ---- */

void lap_cfg_defaults(lap_cfg_t *c)
{
    c->default_layout_id = 0;
}

static void reset_to_no_venue(lap_t *L)
{
    L->venue = NULL;
    L->state = LAP_ST_NO_VENUE;
    L->n_cand = 0;
    L->forced_layout_id = 0;
    L->have_scan = false;
    L->last_scan_us = 0;
    L->lap_no = 0;
    L->lap_flags = 0;
    L->lap_start_gps_us = 0;
    L->lap_dist_m = 0.0;
    L->have_prev_fix = false;
    L->prev_gps_us = 0;
    L->prev_speed_mps = 0.0;
    L->pit_slow = false;
    L->pit_since_us = 0;
    L->leaving = false;
    L->leave_since_us = 0;
    /* best/prev results and cfg are preserved across a reset (§10.3). */
}

void lap_init(lap_t *L, const lap_cfg_t *cfg)
{
    memset(L, 0, sizeof *L);
    if (cfg) L->cfg = *cfg;
    else lap_cfg_defaults(&L->cfg);
    L->state = LAP_ST_NO_VENUE;
}

void lap_reset(lap_t *L)
{
    reset_to_no_venue(L);
}

/* Arm the S/F gate of every candidate layout (all of the venue, or just the forced one). The origin
 * is already the venue centre, so the S/F endpoints project into ENU once here. */
static void arm_candidates(lap_t *L)
{
    L->n_cand = 0;
    if (!L->venue) return;
    for (uint8_t i = 0; i < L->venue->n_layouts && i < TRK_MAX_LAYOUTS; i++) {
        const trk_layout_t *ly = &L->venue->layouts[i];
        if (L->forced_layout_id != 0 && ly->id != L->forced_layout_id) continue;
        lap_cand_t *c = &L->cand[L->n_cand++];
        c->layout = ly;
        c->sf_p = geo_to_enu(&L->origin, ly->sf.p1.lat, ly->sf.p1.lon);
        c->sf_q = geo_to_enu(&L->origin, ly->sf.p2.lat, ly->sf.p2.lon);
        c->sf_armed = true;
        c->sf_last_cross_us = 0;
    }
}

void lap_set_venue(lap_t *L, const trk_venue_t *v)
{
    L->venue = v;
    if (!v) {
        reset_to_no_venue(L);
        return;
    }
    geo_origin_set(&L->origin, v->lat, v->lon);
    L->state = LAP_ST_VENUE_FOUND;
    L->lap_no = 0;
    L->lap_flags = 0;
    L->lap_start_gps_us = 0;
    L->lap_dist_m = 0.0;
    L->have_prev_fix = false;
    L->pit_slow = false;
    L->pit_since_us = 0;
    L->leaving = false;
    L->leave_since_us = 0;
    arm_candidates(L);
}

void lap_force_layout(lap_t *L, uint16_t layout_id)
{
    L->forced_layout_id = layout_id;   /* §10.5: lock to this layout and re-arm its S/F */
    if (L->venue) arm_candidates(L);
}

/* ---- queries ---- */

uint8_t lap_state(const lap_t *L)
{
    return L->state;
}

const lap_result_t *lap_best(const lap_t *L)
{
    return L->have_best ? &L->best : NULL;
}

const lap_result_t *lap_prev(const lap_t *L)
{
    return L->have_prev_result ? &L->prev : NULL;
}

uint32_t lap_current_elapsed_ms(const lap_t *L, int64_t now_gps_us)
{
    if (L->state != LAP_ST_RUNNING) return 0;
    int64_t d = now_gps_us - L->lap_start_gps_us;
    if (d <= 0) return 0;
    return (uint32_t)((d + 500) / 1000);        /* nearest ms */
}

uint32_t lap_theoretical_best_ms(const lap_t *L)
{
    (void)L;
    return 0;                                    /* Σ best sector ms — session 2.5 */
}

int lap_mark_gate(lap_t *L, uint8_t gate_idx, const gps_fix_t *fix, trk_layout_t *out_layout)
{
    (void)L; (void)gate_idx; (void)fix; (void)out_layout;
    return -1;                                   /* on-device creation — session 2.5 */
}

/* ---- fix processing ---- */

static void emit(lap_evt_cb_t cb, void *ctx, uint8_t type, uint8_t flags, uint16_t arg16,
                 int64_t gps_us, int64_t mono_us, uint32_t arg32, uint32_t arg32b)
{
    if (!cb) return;
    event_t ev = { type, flags, arg16, gps_us, mono_us, arg32, arg32b };
    cb(&ev, ctx);
}

/* §10.3 leave-venue: beyond radius·VENUE_LEAVE_FACTOR for VENUE_LEAVE_S continuous seconds. */
static bool update_leave_venue(lap_t *L, double lat, double lon, int64_t now)
{
    double d = geo_dist_m(lat, lon, L->venue->lat, L->venue->lon);
    double thr = (double)L->venue->radius_m * VENUE_LEAVE_FACTOR;
    if (d > thr) {
        if (!L->leaving) {
            L->leaving = true;
            L->leave_since_us = now;
        } else if (now - L->leave_since_us >= (int64_t)VENUE_LEAVE_S * 1000000) {
            return true;
        }
    } else {
        L->leaving = false;
    }
    return false;
}

/* Complete the current lap at t_cross (§10.4) and advance to the next. */
static void complete_lap(lap_t *L, int64_t t_cross, int64_t mono_us, lap_evt_cb_t cb, void *ctx)
{
    int64_t elapsed = t_cross - L->lap_start_gps_us;
    if (elapsed < 0) elapsed = 0;
    uint32_t time_ms = (uint32_t)((elapsed + 500) / 1000);          /* nearest ms (§10.4 step 1) */

    const bool is_out = (L->lap_no == 0);

    /* §10.4 step 2 debounce guard: a sub-MIN_LAP_S flying lap can only be a re-fire (the §6.4 step 5
     * re-arm already forbids it), so ignore the crossing without completing or advancing. */
    if (!is_out && time_ms < (uint32_t)MIN_LAP_S * 1000) return;

    uint8_t flags = L->lap_flags;                                  /* GPS_LOST / PIT accumulated in-lap */
    if (is_out) flags |= LAP_F_OUT_LAP;
    if (time_ms > (uint32_t)MAX_LAP_S * 1000) flags |= LAP_F_TOO_LONG;
    const bool valid = !(flags & (LAP_F_GPS_LOST | LAP_F_PIT | LAP_F_INCOMPLETE |
                                  LAP_F_OUT_LAP | LAP_F_TOO_LONG));
    if (valid) flags |= LAP_F_VALID;

    lap_result_t r;
    memset(&r, 0, sizeof r);
    r.lap_no       = L->lap_no;
    r.start_gps_us = L->lap_start_gps_us;
    r.time_ms      = is_out ? 0u : time_ms;                        /* out-lap reports no time (§10.4 step 3) */
    r.flags        = flags;
    r.n_sectors    = 0;                                            /* sectors are session 2.5 */

    L->prev = r;                                                   /* §10.4 step 7: prev = this */
    L->have_prev_result = true;
    if (valid && (!L->have_best || r.time_ms < L->best.time_ms)) { /* §10.4 step 6: best from valid laps */
        L->best = r;
        L->have_best = true;
    }
    emit(cb, ctx, EV_LAP_COMPLETE, flags, L->lap_no, t_cross, mono_us, r.time_ms, 0);

    /* Advance to the next lap (§10.4 step 7). */
    L->lap_start_gps_us = t_cross;
    L->lap_no++;
    L->lap_flags = 0;
    L->lap_dist_m = 0.0;
    L->pit_slow = false;
    L->pit_since_us = 0;
}

/* Re-arm every fired S/F gate that is clear of its line and past the debounce window (§6.4 step 5). */
static void rearm_gates(lap_t *L, geo_enu_t cur, int64_t now)
{
    for (uint8_t i = 0; i < L->n_cand; i++) {
        lap_cand_t *c = &L->cand[i];
        if (c->sf_armed) continue;
        double d = geo_dist_point_segment(cur, c->sf_p, c->sf_q);
        if (d > GATE_REARM_DIST_M &&
            now - c->sf_last_cross_us > (int64_t)GATE_REARM_MIN_S * 1000000 &&
            now - c->sf_last_cross_us > (int64_t)MIN_LAP_S * 1000000)     /* S/F needs MIN_LAP_S too */
            c->sf_armed = true;
    }
}

void lap_on_fix(lap_t *L, const gps_fix_t *fix, const fused_sample_t *fs, lap_evt_cb_t cb, void *ctx)
{
    (void)fs;                                    /* fused stats feed sectors/§9.4 stats in later sessions */
    if (!fix) return;
    const bool    valid = fix->valid != 0;
    const int64_t now   = fix->gps_us;
    const double  lat   = (double)fix->lat_e7 / 1e7;
    const double  lon   = (double)fix->lon_e7 / 1e7;

    /* NO_VENUE: scan for a venue every VENUE_SCAN_S (§10.3). */
    if (L->state == LAP_ST_NO_VENUE) {
        if (!valid) return;
        if (!L->have_scan || now - L->last_scan_us >= (int64_t)VENUE_SCAN_S * 1000000) {
            L->have_scan = true;
            L->last_scan_us = now;
            uint32_t dist_m = 0;
            const trk_venue_t *v = trk_find_nearest(lat, lon, &dist_m);
            if (v) {
                lap_set_venue(L, v);
                emit(cb, ctx, EV_VENUE_FOUND, 0, v->id, now, fix->mono_us, 0, 0);
            }
        }
        if (L->state == LAP_ST_NO_VENUE) return;
    }

    /* An invalid fix while a venue is held only sets the GPS-lost flag if a lap is running (§6.4
     * step 1); its position is unusable, so no crossing, distance, prev or leave-timer update. */
    if (!valid) {
        if (L->state == LAP_ST_RUNNING) L->lap_flags |= LAP_F_GPS_LOST;
        return;
    }

    /* Leave-venue watch runs in every venue-bound state (§10.3). */
    if (update_leave_venue(L, lat, lon, now)) {
        reset_to_no_venue(L);
        return;
    }

    /* VENUE_FOUND → ARMED on this same valid fix (§10.3, "immediately"). */
    if (L->state == LAP_ST_VENUE_FOUND) {
        L->state = LAP_ST_ARMED;
        emit(cb, ctx, EV_ARMED, 0, 0, now, fix->mono_us, 0, 0);
    }

    const geo_enu_t cur = geo_to_enu(&L->origin, lat, lon);

    /* S/F crossing (§6.4) over the previous→current segment, for ARMED and LAP_RUNNING. */
    if ((L->state == LAP_ST_ARMED || L->state == LAP_ST_RUNNING) && L->have_prev_fix) {
        rearm_gates(L, cur, now);

        bool   matched[TRK_MAX_LAYOUTS];
        bool   hit = false;
        double t_earliest = 2.0;
        for (uint8_t i = 0; i < L->n_cand; i++) {
            matched[i] = false;
            lap_cand_t *c = &L->cand[i];
            if (!c->sf_armed) continue;
            double t = 0.0;
            int    dir = 0;
            if (!geo_segment_cross(L->prev_enu, cur, c->sf_p, c->sf_q, &t, &dir)) continue;
            if (dir != c->layout->dir_sign) continue;    /* only the layout's own crossing direction */
            matched[i] = true;
            hit = true;
            if (t < t_earliest) t_earliest = t;
        }

        if (hit) {
            /* Crossing time by constant-acceleration interpolation over the segment (§6.4 step 4). */
            const double dt_s = (double)(now - L->prev_gps_us) / 1e6;
            const double v1   = (double)fix->gspeed_mms / 1000.0;
            const double rlen = hypot(cur.x - L->prev_enu.x, cur.y - L->prev_enu.y);
            const double tau  = geo_interp_time(t_earliest * rlen, L->prev_speed_mps, v1, dt_s);
            const int64_t t_cross = L->prev_gps_us + (int64_t)llround(tau * 1e6);

            /* Disarm every gate that fired and stamp it for the re-arm timer. */
            for (uint8_t i = 0; i < L->n_cand; i++) {
                if (!matched[i]) continue;
                L->cand[i].sf_armed = false;
                L->cand[i].sf_last_cross_us = t_cross;
            }

            if (L->state == LAP_ST_ARMED) {
                /* First accepted crossing opens the out-lap and narrows candidates (§10.3). */
                uint8_t k = 0;
                for (uint8_t i = 0; i < L->n_cand; i++)
                    if (matched[i]) L->cand[k++] = L->cand[i];
                L->n_cand = k;
                L->state = LAP_ST_RUNNING;
                L->lap_no = 0;
                L->lap_flags = 0;
                L->lap_start_gps_us = t_cross;
                L->lap_dist_m = 0.0;
                L->pit_slow = false;
                L->pit_since_us = 0;
            } else {
                complete_lap(L, t_cross, fix->mono_us, cb, ctx);
            }
        }
    }

    /* While a lap is in progress: pit detection (§10.6) and Doppler-integrated lap distance (§10.5
     * input; consumed by disambiguation and length measurement in session 2.5). */
    if (L->state == LAP_ST_RUNNING) {
        const double kmh = (double)fix->gspeed_mms * 0.0036;      /* mm/s → km/h */
        if (kmh < (double)PIT_SPEED_KMH) {
            if (!L->pit_slow) {
                L->pit_slow = true;
                L->pit_since_us = now;
            } else if (now - L->pit_since_us >= (int64_t)PIT_TIME_S * 1000000) {
                L->lap_flags |= LAP_F_PIT;
            }
        } else {
            L->pit_slow = false;
        }
        if (L->have_prev_fix) {
            const double seg_dt = (double)(now - L->prev_gps_us) / 1e6;
            if (seg_dt > 0.0)
                L->lap_dist_m += 0.5 * (L->prev_speed_mps + (double)fix->gspeed_mms / 1000.0) * seg_dt;
        }
    }

    /* Remember this valid fix as the start of the next segment (§6.4 needs two valid fixes). */
    L->prev_enu = cur;
    L->prev_gps_us = now;
    L->prev_speed_mps = (double)fix->gspeed_mms / 1000.0;
    L->have_prev_fix = true;
}
