#include "core/lap.h"
#include <string.h>

/* Lap engine (spec §10). Session 2.4 part 1: venue detection, arming and leave-venue. The S/F
 * crossing and lap completion are added in Task 2, pit detection and Doppler distance in Task 3. */

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

    /* ---- S/F crossing and lap completion (ARMED → LAP_RUNNING → completions) are added in Task 2,
     * with pit detection and Doppler-integrated distance in Task 3. This part still only detects the
     * venue, arms the S/F gates and watches for leaving. ---- */

    /* Remember this valid fix as the start of the next segment (§6.4 needs two valid fixes). */
    L->prev_enu = cur;
    L->prev_gps_us = now;
    L->prev_speed_mps = (double)fix->gspeed_mms / 1000.0;
    L->have_prev_fix = true;
}
