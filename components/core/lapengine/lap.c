#include "core/lap.h"
#include "core/exp.h"      /* exp_utc_parts: gps_us -> Y/M/D for the Track_YYYYMMDD name (§10.9) */
#include "core/core.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Power of 10 rule 5: per-module assertion code; file:line at the hook pins the exact check. */
#define LAP_ASSERT_CODE 0x0A20

/* Lap engine (spec §10). Session 2.4: venue detection and arming (part 1), S/F crossing and lap
 * completion (part 2), pit detection and Doppler distance integration (part 3). Session 2.5 layers on
 * sector gates and splits, §10.5 layout disambiguation, §10.7 sector delta, §10.8 theoretical best,
 * §10.9 on-device creation, §10.10 RTC continuity and §10.11 predictive delta. */

/* Two candidates' gate lines project to the same physical gate when their ENU endpoints agree to
 * within this many metres (§10.5 union deduplication and lock re-derivation). */
#define UGATE_SAME_M 2.0

/* ---- configuration and lifecycle ---- */

void lap_cfg_defaults(lap_cfg_t *c)
{
    CORE_ASSERT_VOID(c != NULL, LAP_ASSERT_CODE);
    c->default_layout_id = 0;
}

/* Clear the per-lap sector accumulation (§10.4 step 5): gate crossing times and the next-expected
 * sector index; re-arm every sector gate of the locked layout. Called whenever a lap opens. */
static void reset_lap_sectors(lap_t *L)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    L->sec_next = 1;
    memset(L->gate_times, 0, sizeof L->gate_times);
    for (uint8_t i = 0; i < LAP_MAX_SECTORS; i++) L->sec_armed[i] = true;
    memset(L->sec_last_cross_us, 0, sizeof L->sec_last_cross_us);
}

static void reset_to_no_venue(lap_t *L)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    L->venue = NULL;
    L->state = LAP_ST_NO_VENUE;
    L->mode = LAP_MODE_NORMAL;
    L->n_cand = 0;
    L->forced_layout_id = 0;
    L->have_scan = false;
    L->last_scan_us = 0;
    L->lap_no = 0;
    L->lap_flags = 0;
    L->lap_start_gps_us = 0;
    L->lap_dist_m = 0.0;
    L->locked = false;
    L->locked_layout = NULL;
    L->n_sec = 0;
    L->n_ugate = 0;
    L->best_sector_count = 0;
    L->sec_next = 1;
    memset(L->gate_times, 0, sizeof L->gate_times);
    L->have_prev_fix = false;
    L->prev_gps_us = 0;
    L->prev_speed_mps = 0.0;
    L->pit_slow = false;
    L->pit_since_us = 0;
    L->leaving = false;
    L->leave_since_us = 0;
    L->create_have_sf = false;
    L->pred_rec_n = 0;
    L->pred_rec_fixes = 0;
    /* best/prev results, best_sector table, the predictive reference table and cfg are preserved
     * across a reset (§10.3). */
}

void lap_init(lap_t *L, const lap_cfg_t *cfg)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
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
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    L->n_cand = 0;
    if (!L->venue) return;
    for (uint8_t i = 0; i < L->venue->n_layouts && i < TRK_MAX_LAYOUTS; i++) {
        const trk_layout_t *ly = &L->venue->layouts[i];
        if (L->forced_layout_id != 0 && ly->id != L->forced_layout_id) continue;
        CORE_ASSERT_VOID(L->n_cand < TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* candidate array bound */
        lap_cand_t *c = &L->cand[L->n_cand++];
        c->layout = ly;
        c->sf_p = geo_to_enu(&L->origin, ly->sf.p1.lat, ly->sf.p1.lon);
        c->sf_q = geo_to_enu(&L->origin, ly->sf.p2.lat, ly->sf.p2.lon);
        c->sf_armed = true;
        c->sf_last_cross_us = 0;
        c->hits_matching = 0;
        c->hits_foreign = 0;
    }
}

void lap_set_venue(lap_t *L, const trk_venue_t *v)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    L->venue = v;
    if (!v) {
        reset_to_no_venue(L);
        return;
    }
    CORE_ASSERT_VOID(v->n_layouts <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* venue layout count */
    geo_origin_set(&L->origin, v->lat, v->lon);
    L->state = LAP_ST_VENUE_FOUND;
    L->mode = LAP_MODE_NORMAL;
    L->lap_no = 0;
    L->lap_flags = 0;
    L->lap_start_gps_us = 0;
    L->lap_dist_m = 0.0;
    L->locked = false;
    L->locked_layout = NULL;
    L->n_sec = 0;
    L->n_ugate = 0;
    reset_lap_sectors(L);
    L->have_prev_fix = false;
    L->pit_slow = false;
    L->pit_since_us = 0;
    L->leaving = false;
    L->leave_since_us = 0;
    L->create_have_sf = false;
    L->pred_rec_n = 0;
    L->pred_rec_fixes = 0;
    arm_candidates(L);
}

void lap_force_layout(lap_t *L, uint16_t layout_id)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    L->forced_layout_id = layout_id;   /* §10.5: lock to this layout and re-arm its S/F */
    if (L->venue) arm_candidates(L);
}

/* ---- queries ---- */

uint8_t lap_state(const lap_t *L)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, LAP_ST_NO_VENUE);
    return L->state;
}

const lap_result_t *lap_best(const lap_t *L)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, NULL);
    return L->have_best ? &L->best : NULL;
}

const lap_result_t *lap_prev(const lap_t *L)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, NULL);
    return L->have_prev_result ? &L->prev : NULL;
}

uint32_t lap_current_elapsed_ms(const lap_t *L, int64_t now_gps_us)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, 0);
    if (L->state != LAP_ST_RUNNING) return 0;
    int64_t d = now_gps_us - L->lap_start_gps_us;
    if (d <= 0) return 0;
    return (uint32_t)((d + 500) / 1000);        /* nearest ms */
}

uint32_t lap_theoretical_best_ms(const lap_t *L)
{
    /* Sigma best_sector_ms[i] over every split, only when each split has a value (§10.8). */
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, 0);
    CORE_ASSERT_RET(L->best_sector_count <= LAP_MAX_SECTORS + 1, LAP_ASSERT_CODE, 0);   /* loop bound */
    if (L->best_sector_count == 0) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < L->best_sector_count; i++) {
        if (!L->have_best_sector[i]) return 0;
        sum += L->best_sector_ms[i];
    }
    return sum;
}

/* ---- §10.9 on-device track creation (bodies: session 2.5 Task 3) ---- */

void lap_create_begin(lap_t *L)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    reset_to_no_venue(L);
    L->mode = LAP_MODE_CREATE;
    memset(&L->create_layout, 0, sizeof L->create_layout);
    L->create_have_sf = false;
}

void lap_create_cancel(lap_t *L)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    if (L->mode == LAP_MODE_CREATE) reset_to_no_venue(L);
}

/* §10.9: add a gate at the current fix while in CREATE mode. gate_idx 0 builds the S/F line and the
 * venue skeleton; gate_idx n (added in order) appends sector gate n. The gate line is perpendicular to
 * the fix heading (lap_gate_line), so dir_sign +1 accepts the current motion. Returns 0, or -1 if not
 * creating, the fix is invalid or not moving, or the gate index is out of order / full. */
int lap_mark_gate(lap_t *L, uint8_t gate_idx, const gps_fix_t *fix, trk_layout_t *out_layout)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(L->create_layout.n_sectors <= LAP_MAX_SECTORS, LAP_ASSERT_CODE, -1);   /* sector count invariant */
    if (L->mode != LAP_MODE_CREATE) return -1;
    if (!fix || fix->valid == 0 || fix->gspeed_mms <= 0) return -1;   /* need a heading of motion */

    const double lat = (double)fix->lat_e7 / 1e7;
    const double lon = (double)fix->lon_e7 / 1e7;
    const double heading_deg = (double)fix->head_e5 / 1e5;
    const trk_line_t line = lap_gate_line(lat, lon, heading_deg);

    if (gate_idx == 0) {
        memset(&L->create_layout, 0, sizeof L->create_layout);
        L->create_layout.id = 1;
        snprintf(L->create_layout.name, sizeof L->create_layout.name, "Layout 1");
        L->create_layout.dir_sign = 1;
        L->create_layout.sf = line;
        L->create_layout.n_sectors = 0;
        L->create_venue_id = trk_next_user_id();
        L->create_lat = lat;
        L->create_lon = lon;
        L->create_gps_us = fix->gps_us;
        geo_origin_set(&L->origin, lat, lon);
        L->create_sf_p = geo_to_enu(&L->origin, line.p1.lat, line.p1.lon);
        L->create_sf_q = geo_to_enu(&L->origin, line.p2.lat, line.p2.lon);
        L->create_have_sf = true;
        L->lap_dist_m = 0.0;
        L->prev_enu = geo_to_enu(&L->origin, lat, lon);   /* integrate distance from the S/F press */
        L->prev_gps_us = fix->gps_us;
        L->prev_speed_mps = (double)fix->gspeed_mms / 1000.0;
        L->have_prev_fix = true;
    } else {
        if (!L->create_have_sf) return -1;
        if (gate_idx != (uint8_t)(L->create_layout.n_sectors + 1)) return -1;   /* in order only */
        if (L->create_layout.n_sectors >= LAP_MAX_SECTORS) return -1;
        L->create_layout.sectors[L->create_layout.n_sectors] = line;
        L->create_layout.n_sectors++;
    }
    if (out_layout) *out_layout = L->create_layout;
    return 0;
}

static void project_locked_sectors(lap_t *L);

/* ---- §10.10 RTC continuity ---- */

void lap_export_rtc(const lap_t *L, lap_rtc_t *out)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(!L->locked || L->locked_layout != NULL, LAP_ASSERT_CODE);   /* locked implies a layout to read */
    memset(out, 0, sizeof *out);
    out->venue_id         = L->venue ? L->venue->id : 0;
    out->layout_id        = L->locked ? L->locked_layout->id : 0;
    out->lap_no           = L->lap_no;
    out->sector_idx       = (uint8_t)(L->sec_next - 1);   /* sector gates crossed so far this lap */
    out->mode             = L->mode;
    out->lap_start_gps_us = L->lap_start_gps_us;
    memcpy(out->gate_times, L->gate_times, sizeof out->gate_times);
    out->best             = L->best;
    out->prev             = L->prev;
}

/* Restore the mirrored state into LAP_RUNNING with LAP_F_INTERRUPTED (§10.10). Unconditional: the
 * CRC32/version container and the RTC_RESUME_MAX_S freshness gate are the pipeline's job (plan 03).
 * Returns -1 only if the venue id is unknown to the track store. */
int lap_import_rtc(lap_t *L, const lap_rtc_t *s)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(s != NULL, LAP_ASSERT_CODE, -1);
    const trk_venue_t *v = trk_get(s->venue_id);
    if (!v) return -1;

    L->venue = v;
    geo_origin_set(&L->origin, v->lat, v->lon);
    L->forced_layout_id = s->layout_id;
    arm_candidates(L);                       /* arm the S/F of the (forced) layout(s) */
    CORE_ASSERT_RET(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE, -1);   /* loop bound below */

    L->locked = false;
    L->locked_layout = NULL;
    L->n_sec = 0;
    L->best_sector_count = 0;
    if (s->layout_id != 0) {
        for (uint8_t i = 0; i < L->n_cand; i++)
            if (L->cand[i].layout->id == s->layout_id) {
                L->locked = true;
                L->locked_layout = L->cand[i].layout;
                L->cand[0] = L->cand[i];
                L->n_cand = 1;
                project_locked_sectors(L);
                break;
            }
    }

    L->state = LAP_ST_RUNNING;
    L->mode = LAP_MODE_NORMAL;
    L->lap_no = s->lap_no;
    L->lap_start_gps_us = s->lap_start_gps_us;
    L->lap_flags = LAP_F_INTERRUPTED;        /* §10.10 / §10.4 step 3 */
    L->lap_dist_m = 0.0;
    memcpy(L->gate_times, s->gate_times, sizeof L->gate_times);
    L->sec_next = (uint8_t)(s->sector_idx + 1);
    L->best = s->best;
    L->have_best = (s->best.flags & LAP_F_VALID) != 0;
    L->prev = s->prev;
    L->have_prev_result = (s->prev.flags != 0);

    L->have_prev_fix = false;                /* the next fix re-establishes the segment origin */
    L->pit_slow = false;
    L->pit_since_us = 0;
    L->leaving = false;
    L->leave_since_us = 0;
    L->n_ugate = 0;
    L->pred_rec_n = 0;
    L->pred_rec_fixes = 0;
    return 0;
}

/* ---- §10.11 predictive delta (O5) ---- */

void lap_set_predictive(lap_t *L, uint16_t *best_dist, uint32_t *best_t,
                        uint16_t *rec_dist, uint32_t *rec_t, uint16_t cap)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    L->pred_best_dist_m = best_dist;
    L->pred_best_t_ms   = best_t;
    L->pred_rec_dist_m  = rec_dist;
    L->pred_rec_t_ms    = rec_t;
    L->pred_cap         = cap;
    L->pred_best_n      = 0;
    L->pred_rec_n       = 0;
    L->pred_rec_fixes   = 0;
}

/* Append the current (distance, elapsed) sample of the lap in progress to the recording table. Beyond
 * PRED_TABLE_MAX entries the table is halved in place and recording continues at half density ("every
 * 2nd fix"), so it always holds <= PRED_TABLE_MAX monotonic-distance samples spanning the whole lap. */
static void pred_record(lap_t *L, int64_t now)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    if (L->pred_cap == 0) return;                        /* predictive disabled: no buffers to fill */
    CORE_ASSERT_VOID(L->pred_rec_dist_m != NULL, LAP_ASSERT_CODE);   /* enabled => buffers set */
    CORE_ASSERT_VOID(L->pred_rec_t_ms != NULL, LAP_ASSERT_CODE);
    if (now <= L->lap_start_gps_us) return;
    uint32_t t_ms = (uint32_t)((now - L->lap_start_gps_us + 500) / 1000);
    double d = L->lap_dist_m;
    if (d < 0.0) d = 0.0; else if (d > (double)UINT16_MAX) d = (double)UINT16_MAX;
    uint16_t d16 = (uint16_t)llround(d);
    L->pred_rec_fixes++;

    if (L->pred_rec_n >= L->pred_cap) {                  /* full: keep every 2nd entry, then continue */
        uint16_t m = 0;
        for (uint16_t i = 0; i < L->pred_rec_n; i += 2) {
            L->pred_rec_dist_m[m] = L->pred_rec_dist_m[i];
            L->pred_rec_t_ms[m]   = L->pred_rec_t_ms[i];
            m++;
        }
        L->pred_rec_n = m;
    }
    if (L->pred_rec_n > 0 && d16 <= L->pred_rec_dist_m[L->pred_rec_n - 1]) return;   /* keep monotone */
    L->pred_rec_dist_m[L->pred_rec_n] = d16;
    L->pred_rec_t_ms[L->pred_rec_n]   = t_ms;
    L->pred_rec_n++;
}

int32_t lap_live_delta_ms(const lap_t *L, int64_t now_gps_us, double dist_m, bool *have)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, 0);
    if (have) *have = false;
    if (L->pred_cap == 0) return 0;                       /* predictive disabled */
    if (L->state != LAP_ST_RUNNING || L->pred_best_n < 2) return 0;
    CORE_ASSERT_RET(L->pred_best_dist_m != NULL, LAP_ASSERT_CODE, 0);   /* populated reference */
    CORE_ASSERT_RET(L->pred_best_t_ms != NULL, LAP_ASSERT_CODE, 0);
    CORE_ASSERT_RET(L->pred_best_n <= L->pred_cap, LAP_ASSERT_CODE, 0);   /* indexes pred_best_*[pred_best_n - 1] below */
    if (dist_m < (double)L->pred_best_dist_m[0] ||
        dist_m > (double)L->pred_best_dist_m[L->pred_best_n - 1]) return 0;   /* outside the reference */

    uint16_t lo = 0, hi = (uint16_t)(L->pred_best_n - 1);
    while (hi - lo > 1) {                               /* binary search the bracketing interval */
        uint16_t mid = (uint16_t)((lo + hi) / 2);
        if ((double)L->pred_best_dist_m[mid] <= dist_m) lo = mid; else hi = mid;
    }
    double d0 = L->pred_best_dist_m[lo],  d1 = L->pred_best_dist_m[hi];
    double t0 = L->pred_best_t_ms[lo],    t1 = L->pred_best_t_ms[hi];
    double t_ref = (d1 > d0) ? t0 + (t1 - t0) * (dist_m - d0) / (d1 - d0) : t0;   /* linear interp */
    uint32_t elapsed = (now_gps_us > L->lap_start_gps_us)
                     ? (uint32_t)((now_gps_us - L->lap_start_gps_us + 500) / 1000) : 0;
    if (have) *have = true;
    return (int32_t)((double)elapsed - t_ref);
}

/* ---- geometry and sector helpers ---- */

static geo_enu_t pt_enu(const lap_t *L, trk_pt_t p)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, (geo_enu_t){0});
    return geo_to_enu(&L->origin, p.lat, p.lon);
}

static bool enu_near(geo_enu_t a, geo_enu_t b)
{
    return hypot(a.x - b.x, a.y - b.y) < UGATE_SAME_M;
}

/* Crossing time by constant-acceleration interpolation over the previous->current segment (§6.4
 * step 4), given the crossing's fraction t_frac along the segment and the current speed v1. */
static int64_t cross_time(const lap_t *L, geo_enu_t cur, double v1, double t_frac, int64_t now)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, now);
    CORE_ASSERT_RET(t_frac >= 0.0 && t_frac <= 1.0, LAP_ASSERT_CODE, now);   /* crossing fraction along the segment */
    CORE_ASSERT_RET(v1 >= 0.0, LAP_ASSERT_CODE, now);                        /* ground speed non-negative */
    const double dt_s = (double)(now - L->prev_gps_us) / 1e6;
    const double rlen = hypot(cur.x - L->prev_enu.x, cur.y - L->prev_enu.y);
    const double tau  = geo_interp_time(t_frac * rlen, L->prev_speed_mps, v1, dt_s);
    return L->prev_gps_us + (int64_t)llround(tau * 1e6);
}

/* Project the locked layout's sector gates into ENU and arm them; set the split count (§10.5). */
static void project_locked_sectors(lap_t *L)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->locked_layout != NULL, LAP_ASSERT_CODE);   /* layout pointer valid before use */
    const trk_layout_t *ly = L->locked_layout;
    L->n_sec = ly->n_sectors;
    for (uint8_t j = 0; j < ly->n_sectors && j < LAP_MAX_SECTORS; j++) {
        L->sec_p[j] = pt_enu(L, ly->sectors[j].p1);
        L->sec_q[j] = pt_enu(L, ly->sectors[j].p2);
        L->sec_armed[j] = true;
        L->sec_last_cross_us[j] = 0;
    }
    L->best_sector_count = (uint8_t)(L->n_sec + 1);
}

static void emit(event_t *out, int cap, int *n, uint8_t type, uint8_t flags, uint16_t arg16,
                 int64_t gps_us, int64_t mono_us, uint32_t arg32, uint32_t arg32b);

/* Lock the layout at candidate index idx (§10.5): narrow candidates to it, project and arm its sector
 * gates, and announce EV_LAYOUT_LOCKED. */
static void lock_layout(lap_t *L, uint8_t idx, int64_t t, int64_t mono, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(idx < L->n_cand, LAP_ASSERT_CODE);   /* candidate index in range */
    L->locked = true;
    L->locked_layout = L->cand[idx].layout;
    L->cand[0] = L->cand[idx];
    L->n_cand = 1;
    project_locked_sectors(L);
    emit(out, cap, n, EV_LAYOUT_LOCKED, 0, L->locked_layout->id, t, mono, 0, 0);
}

/* Find the union gate whose endpoints match (p, q), or -1. */
static int ugate_find(const lap_t *L, geo_enu_t p, geo_enu_t q)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, -1);
    CORE_ASSERT_RET(L->n_ugate <= LAP_MAX_UGATES, LAP_ASSERT_CODE, -1);   /* loop bound */
    for (uint8_t gi = 0; gi < L->n_ugate; gi++)
        if (enu_near(L->ugate[gi].p, p) && enu_near(L->ugate[gi].q, q)) return (int)gi;
    return -1;
}

/* Build the deduplicated union of every candidate's sector gates, tagged by candidate (§10.5). */
static void build_union(lap_t *L)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* loop bound */
    L->n_ugate = 0;
    for (uint8_t i = 0; i < L->n_cand; i++) {
        const trk_layout_t *ly = L->cand[i].layout;
        for (uint8_t s = 0; s < ly->n_sectors && s < LAP_MAX_SECTORS; s++) {
            geo_enu_t p = pt_enu(L, ly->sectors[s].p1);
            geo_enu_t q = pt_enu(L, ly->sectors[s].p2);
            int gi = ugate_find(L, p, q);
            if (gi < 0) {
                if (L->n_ugate >= LAP_MAX_UGATES) continue;
                gi = (int)L->n_ugate++;
                lap_ugate_t *g = &L->ugate[gi];
                g->p = p; g->q = q;
                g->dir_sign = ly->dir_sign;
                g->armed = true;
                g->cross_us = 0;
                g->last_cross_us = 0;
                g->in_layout = 0;
            }
            L->ugate[gi].in_layout |= (uint8_t)(1u << i);
        }
    }
}

/* Re-arm S/F candidate gates clear of their line and past the debounce window (§6.4 step 5). */
static void rearm_sf(lap_t *L, geo_enu_t cur, int64_t now)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* loop bound */
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

/* Re-arm sector gates (§6.4 step 5); sectors need only GATE_REARM_DIST_M / GATE_REARM_MIN_S. */
static void rearm_sectors(lap_t *L, geo_enu_t cur, int64_t now)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_sec <= LAP_MAX_SECTORS, LAP_ASSERT_CODE);   /* loop bound */
    for (uint8_t j = 0; j < L->n_sec; j++) {
        if (L->sec_armed[j]) continue;
        double d = geo_dist_point_segment(cur, L->sec_p[j], L->sec_q[j]);
        if (d > GATE_REARM_DIST_M && now - L->sec_last_cross_us[j] > (int64_t)GATE_REARM_MIN_S * 1000000)
            L->sec_armed[j] = true;
    }
}

static void rearm_ugates(lap_t *L, geo_enu_t cur, int64_t now)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_ugate <= LAP_MAX_UGATES, LAP_ASSERT_CODE);   /* loop bound */
    for (uint8_t gi = 0; gi < L->n_ugate; gi++) {
        lap_ugate_t *g = &L->ugate[gi];
        if (g->armed) continue;
        double d = geo_dist_point_segment(cur, g->p, g->q);
        if (d > GATE_REARM_DIST_M && now - g->last_cross_us > (int64_t)GATE_REARM_MIN_S * 1000000)
            g->armed = true;
    }
}

/* Record a sector gate crossing at t_cross (§10.4 step 5, §10.7). k is the 1-based sector gate number
 * in the locked layout. Out-of-order or skipped gates set LAP_F_INCOMPLETE and resync to k. */
static void handle_sector_cross(lap_t *L, uint8_t k, int64_t t_cross, int64_t mono,
                                event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(k >= 1 && k <= LAP_MAX_SECTORS, LAP_ASSERT_CODE);   /* 1-based gate index into gate_times[] */
    if (k < L->sec_next) return;                 /* already recorded this lap */
    if (k > L->sec_next) L->lap_flags |= LAP_F_INCOMPLETE;   /* a sector was skipped */
    L->gate_times[k] = t_cross;

    uint8_t prev = (uint8_t)(k - 1);             /* last recorded boundary before k (0 = lap start) */
    while (prev > 0 && L->gate_times[prev] == 0) prev--;
    uint32_t split_ms = (uint32_t)llround((double)(t_cross - L->gate_times[prev]) / 1000.0);

    uint8_t idx = (uint8_t)(k - 1);              /* split index this crossing closes */
    int32_t delta = 0;                           /* §10.7: 0 when no best lap exists yet (else-no-delta
                                                   * case), same wire value as a genuine zero delta */
    if (L->have_best && idx < L->best.n_sectors)
        delta = (int32_t)split_ms - (int32_t)L->best.sector_ms[idx];
    emit(out, cap, n, EV_SECTOR, 0, idx, t_cross, mono, split_ms, (uint32_t)delta);

    L->sec_next = (uint8_t)(k + 1);
}

typedef struct { uint8_t k; int64_t t; } sec_fire_t;   /* a sector gate that fired in one segment */

/* Evaluate the locked layout's sector gates over the previous->current segment. */
static void evaluate_locked_sectors(lap_t *L, geo_enu_t cur, const gps_fix_t *fix,
                                    event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->locked_layout != NULL, LAP_ASSERT_CODE);       /* layout pointer valid before use */
    CORE_ASSERT_VOID(L->n_sec <= LAP_MAX_SECTORS, LAP_ASSERT_CODE);    /* loop / fired[] bound */
    const int64_t now = fix->gps_us;
    const double  v1  = (double)fix->gspeed_mms / 1000.0;
    CORE_ASSERT_VOID(v1 >= 0.0, LAP_ASSERT_CODE);                      /* ground speed non-negative */
    rearm_sectors(L, cur, now);

    sec_fire_t fired[LAP_MAX_SECTORS];
    uint8_t nf = 0;
    for (uint8_t j = 0; j < L->n_sec; j++) {
        if (!L->sec_armed[j]) continue;
        double t; int dir;
        if (!geo_segment_cross(L->prev_enu, cur, L->sec_p[j], L->sec_q[j], &t, &dir)) continue;
        if (dir != L->locked_layout->dir_sign) continue;
        fired[nf].k = (uint8_t)(j + 1);
        fired[nf].t = cross_time(L, cur, v1, t, now);
        nf++;
    }
    /* Process in crossing-time order (§6.4: two gates may fall in one segment). */
    for (uint8_t a = 1; a < nf; a++)
        for (uint8_t b = a; b > 0 && fired[b - 1].t > fired[b].t; b--) {
            sec_fire_t tmp = fired[b - 1];
            fired[b - 1] = fired[b];
            fired[b] = tmp;
        }
    for (uint8_t f = 0; f < nf; f++) {
        uint8_t j = (uint8_t)(fired[f].k - 1);
        L->sec_armed[j] = false;
        L->sec_last_cross_us[j] = fired[f].t;
        handle_sector_cross(L, fired[f].k, fired[f].t, fix->mono_us, out, cap, n);
    }
}

/* Evaluate the disambiguation union over the segment: record each gate's crossing time and update
 * every candidate's matching/foreign score (§10.5). No EV_SECTOR while the layout is unknown. */
static void evaluate_union(lap_t *L, geo_enu_t cur, const gps_fix_t *fix)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_ugate <= LAP_MAX_UGATES, LAP_ASSERT_CODE);   /* loop bound */
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* inner loop bound */
    const int64_t now = fix->gps_us;
    const double  v1  = (double)fix->gspeed_mms / 1000.0;
    CORE_ASSERT_VOID(v1 >= 0.0, LAP_ASSERT_CODE);                      /* ground speed non-negative */
    rearm_ugates(L, cur, now);

    for (uint8_t gi = 0; gi < L->n_ugate; gi++) {
        lap_ugate_t *g = &L->ugate[gi];
        if (!g->armed) continue;
        double t; int dir;
        if (!geo_segment_cross(L->prev_enu, cur, g->p, g->q, &t, &dir)) continue;
        if (dir != g->dir_sign) continue;
        int64_t tc = cross_time(L, cur, v1, t, now);
        g->armed = false;
        g->last_cross_us = tc;
        if (g->cross_us == 0) g->cross_us = tc;
        for (uint8_t i = 0; i < L->n_cand; i++) {
            if (g->in_layout & (uint8_t)(1u << i)) L->cand[i].hits_matching++;
            else                                   L->cand[i].hits_foreign++;
        }
    }
}

/* §10.5 tie-break: the venue's default layout wins, else the lowest layout id. */
static bool tiebreak_better(const lap_t *L, const trk_layout_t *a, const trk_layout_t *b)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, false);
    CORE_ASSERT_RET(a != NULL, LAP_ASSERT_CODE, false);
    CORE_ASSERT_RET(b != NULL, LAP_ASSERT_CODE, false);
    uint16_t def = L->cfg.default_layout_id;
    bool ad = (a->id == def), bd = (b->id == def);
    if (ad != bd) return ad;
    return a->id < b->id;
}

/* At the out-lap's closing S/F crossing, score the candidates and lock the winner (§10.5), then map
 * the recorded union crossing times onto the locked layout's sector gates so the out-lap result
 * carries splits. */
static void disambiguate_and_lock(lap_t *L, int64_t t_cross, int64_t mono, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(!L->locked, LAP_ASSERT_CODE);                    /* only called on an unlocked out-lap */
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);  /* scoring loop bound */
    uint8_t best_i = 0;
    int     best_score = -1000000;
    for (uint8_t i = 0; i < L->n_cand; i++) {
        int score = L->cand[i].hits_matching - L->cand[i].hits_foreign;
        double len = (double)L->cand[i].layout->length_m;
        if (len > 0.0 && fabs(L->lap_dist_m - len) < LAYOUT_LEN_TOL * len) score += 1;   /* §10.5 */
        if (score > best_score ||
            (score == best_score && tiebreak_better(L, L->cand[i].layout, L->cand[best_i].layout))) {
            best_score = score;
            best_i = i;
        }
    }
    lock_layout(L, best_i, t_cross, mono, out, cap, n);

    /* Re-derive the out-lap's sector times from the union (§10.5: splits recorded per gate). */
    CORE_ASSERT_VOID(L->n_sec <= LAP_MAX_SECTORS, LAP_ASSERT_CODE);   /* loop bound after lock */
    for (uint8_t j = 0; j < L->n_sec; j++) {
        int u = ugate_find(L, L->sec_p[j], L->sec_q[j]);
        if (u >= 0 && L->ugate[u].cross_us != 0) L->gate_times[(uint8_t)(j + 1)] = L->ugate[u].cross_us;
    }
    L->n_ugate = 0;
}

/* Fill sector_ms[] for the completing lap from gate_times and the S/F crossing t_cross. For a complete
 * lap the splits are rounded cumulatively, so they sum exactly to the lap time (§10.4 step 5). */
static void compute_sectors(const lap_t *L, int64_t t_cross, uint32_t *sector_ms,
                            uint8_t *n_out, bool *incomplete)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(sector_ms != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(n_out != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(incomplete != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_sec <= LAP_MAX_SECTORS, LAP_ASSERT_CODE);   /* b[] index bound */
    if (!L->locked) { *n_out = 0; *incomplete = false; return; }
    uint8_t n = L->n_sec;
    int64_t b[LAP_MAX_SECTORS + 2];
    b[0] = L->lap_start_gps_us;
    bool inc = false;
    for (uint8_t j = 1; j <= n; j++) { b[j] = L->gate_times[j]; if (b[j] == 0) inc = true; }
    b[n + 1] = t_cross;

    if (!inc) {
        uint32_t prev_cum = 0;
        for (uint8_t i = 0; i <= n; i++) {
            uint32_t cum = (uint32_t)llround((double)(b[i + 1] - b[0]) / 1000.0);
            sector_ms[i] = cum - prev_cum;
            prev_cum = cum;
        }
    } else {
        for (uint8_t i = 0; i <= n; i++)
            sector_ms[i] = (b[i] != 0 && b[i + 1] != 0)
                         ? (uint32_t)llround((double)(b[i + 1] - b[i]) / 1000.0) : 0u;
    }
    *n_out = (uint8_t)(n + 1);
    *incomplete = inc;
}

/* ---- fix processing ---- */

static void emit(event_t *out, int cap, int *n, uint8_t type, uint8_t flags, uint16_t arg16,
                 int64_t gps_us, int64_t mono_us, uint32_t arg32, uint32_t arg32b)
{
    CORE_ASSERT_VOID(*n < cap, LAP_ASSERT_CODE);   /* caller passes a LAP_EVT_MAX buffer; overflow = miscount bug */
    event_t ev = { type, flags, arg16, gps_us, mono_us, arg32, arg32b };
    out[*n] = ev;
    (*n)++;
}

/* §10.3 leave-venue: beyond radius·VENUE_LEAVE_FACTOR for VENUE_LEAVE_S continuous seconds. */
static bool update_leave_venue(lap_t *L, double lat, double lon, int64_t now)
{
    CORE_ASSERT_RET(L != NULL, LAP_ASSERT_CODE, false);
    CORE_ASSERT_RET(L->venue != NULL, LAP_ASSERT_CODE, false);   /* venue pointer valid before use */
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

/* Open a new lap at t_cross: reset per-lap accumulation and re-arm the locked layout's sector gates. */
static void open_lap(lap_t *L, uint16_t lap_no, int64_t t_cross)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    L->lap_no = lap_no;
    L->lap_flags = 0;
    L->lap_start_gps_us = t_cross;
    L->lap_dist_m = 0.0;
    L->pit_slow = false;
    L->pit_since_us = 0;
    L->sec_next = 1;
    memset(L->gate_times, 0, sizeof L->gate_times);
    L->gate_times[0] = t_cross;
    for (uint8_t j = 0; j < LAP_MAX_SECTORS; j++) L->sec_armed[j] = true;
    L->pred_rec_n = 0;
    L->pred_rec_fixes = 0;
}

/* Complete the current lap at t_cross (§10.4) and advance to the next. */
static void complete_lap(lap_t *L, int64_t t_cross, int64_t mono_us, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    int64_t elapsed = t_cross - L->lap_start_gps_us;
    if (elapsed < 0) elapsed = 0;
    uint32_t time_ms = (uint32_t)((elapsed + 500) / 1000);          /* nearest ms (§10.4 step 1) */

    const bool is_out = (L->lap_no == 0);

    /* §10.4 step 2 debounce guard (should not fire after §6.4 step 5 on the S/F). */
    if (!is_out && time_ms < (uint32_t)MIN_LAP_S * 1000) return;

    uint32_t sector_ms[LAP_MAX_SECTORS + 1];
    uint8_t  n_splits;
    bool     inc;
    compute_sectors(L, t_cross, sector_ms, &n_splits, &inc);
    CORE_ASSERT_VOID(n_splits <= LAP_MAX_SECTORS + 1, LAP_ASSERT_CODE);   /* sector_ms[] / result bound */

    uint8_t flags = L->lap_flags;                                  /* GPS_LOST / PIT / INCOMPLETE in-lap */
    if (is_out) flags |= LAP_F_OUT_LAP;
    if (time_ms > (uint32_t)MAX_LAP_S * 1000) flags |= LAP_F_TOO_LONG;
    if (inc) flags |= LAP_F_INCOMPLETE;                            /* a locked sector went unrecorded */
    const bool valid = !(flags & (LAP_F_GPS_LOST | LAP_F_PIT | LAP_F_INCOMPLETE |
                                  LAP_F_OUT_LAP | LAP_F_TOO_LONG));
    if (valid) flags |= LAP_F_VALID;

    int32_t lap_delta = 0;                                         /* §10.7 lap delta vs best-before */
    if (L->have_best && !is_out) lap_delta = (int32_t)time_ms - (int32_t)L->best.time_ms;

    lap_result_t r;
    memset(&r, 0, sizeof r);
    r.lap_no       = L->lap_no;
    r.start_gps_us = L->lap_start_gps_us;
    r.time_ms      = is_out ? 0u : time_ms;                        /* out-lap reports no time (§10.4 step 3) */
    r.flags        = flags;
    r.n_sectors    = n_splits;
    for (uint8_t i = 0; i < n_splits && i <= LAP_MAX_SECTORS; i++) r.sector_ms[i] = sector_ms[i];

    L->prev = r;                                                   /* §10.4 step 7: prev = this */
    L->have_prev_result = true;
    if (valid && (!L->have_best || r.time_ms < L->best.time_ms)) { /* §10.4 step 6: best from valid laps */
        L->best = r;
        L->have_best = true;
        /* §10.11: this lap is the new best, so its recorded trace becomes the predictive reference
         * (open_lap has not yet reset pred_rec for the next lap). Contents-copy, not a pointer swap,
         * since the buffers are caller-owned fixed storage; a no-op when predictive is disabled. */
        if (L->pred_cap) {
            CORE_ASSERT_VOID(L->pred_rec_n <= L->pred_cap, LAP_ASSERT_CODE);   /* recorded fits reference buffers */
            memcpy(L->pred_best_dist_m, L->pred_rec_dist_m, sizeof(uint16_t) * L->pred_rec_n);
            memcpy(L->pred_best_t_ms,   L->pred_rec_t_ms,   sizeof(uint32_t) * L->pred_rec_n);
            L->pred_best_n = L->pred_rec_n;
        }
    }
    if (valid && L->locked) {                                      /* §10.8 best per-sector, any valid lap */
        for (uint8_t i = 0; i < n_splits && i <= LAP_MAX_SECTORS; i++)
            if (!L->have_best_sector[i] || sector_ms[i] < L->best_sector_ms[i]) {
                L->best_sector_ms[i] = sector_ms[i];
                L->have_best_sector[i] = true;
            }
        L->best_sector_count = n_splits;
    }
    emit(out, cap, n, EV_LAP_COMPLETE, flags, L->lap_no, t_cross, mono_us, r.time_ms, (uint32_t)lap_delta);

    open_lap(L, (uint16_t)(L->lap_no + 1), t_cross);               /* §10.4 step 7 */
}

/* §10.9 step 3: close creation at the S/F crossing. Length comes from the integrated distance, the
 * venue is saved (with an auto reverse layout), and the engine enters VENUE_FOUND on it. */
static void finalize_create(lap_t *L, int64_t t_cross, int64_t mono, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->lap_dist_m >= 0.0, LAP_ASSERT_CODE);   /* integrated distance non-negative */
    L->create_layout.length_m = (uint32_t)llround(L->lap_dist_m);

    trk_venue_t nv;
    memset(&nv, 0, sizeof nv);
    nv.id = L->create_venue_id;
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(L->create_gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    snprintf(nv.name, sizeof nv.name, "Track_%04d%02u%02u", y, mo, d);
    nv.lat = L->create_lat;
    nv.lon = L->create_lon;
    nv.radius_m = VENUE_RADIUS_DEFAULT_M;
    nv.flags = 0;                                /* on-device creation is the most-confirmed case, not
                                                   * TRK_F_UNVERIFIED (§10.1/§10.9) */
    nv.n_layouts = 2;
    nv.layouts[0] = L->create_layout;                          /* id 1, "Layout 1" */
    lap_layout_reverse(&L->create_layout, &nv.layouts[1]);     /* id 2, "Layout 1 Reverse" (§10.9 step 4) */

    if (trk_user_add(&nv) != 0) {                /* store full or invalid: abort creation */
        reset_to_no_venue(L);
        return;
    }
    lap_set_venue(L, trk_get(nv.id));            /* → VENUE_FOUND on the saved venue (mode = NORMAL) */
    emit(out, cap, n, EV_VENUE_FOUND, 0, nv.id, t_cross, mono, 0, 0);
}

/* §10.9 CREATE sub-mode: no venue scan; integrate distance and watch for the S/F crossing that closes
 * creation. The MIN_LAP_S guard rejects the spurious crossing right after the S/F press. */
static void handle_create_mode(lap_t *L, const gps_fix_t *fix, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->mode == LAP_MODE_CREATE, LAP_ASSERT_CODE);
    const bool    valid = fix->valid != 0;
    const int64_t now   = fix->gps_us;
    const double  lat   = (double)fix->lat_e7 / 1e7;
    const double  lon   = (double)fix->lon_e7 / 1e7;
    const double  v1    = (double)fix->gspeed_mms / 1000.0;
    if (!valid || !L->create_have_sf) return;
    CORE_ASSERT_VOID(v1 >= 0.0, LAP_ASSERT_CODE);   /* ground speed non-negative (feeds lap_dist_m below) */
    const geo_enu_t cur = geo_to_enu(&L->origin, lat, lon);
    if (L->have_prev_fix) {
        const double seg_dt = (double)(now - L->prev_gps_us) / 1e6;
        if (seg_dt > 0.0) L->lap_dist_m += 0.5 * (L->prev_speed_mps + v1) * seg_dt;
        double t; int dir;
        if (now - L->create_gps_us >= (int64_t)MIN_LAP_S * 1000000 &&
            geo_segment_cross(L->prev_enu, cur, L->create_sf_p, L->create_sf_q, &t, &dir) &&
            dir == L->create_layout.dir_sign) {
            int64_t t_cross = cross_time(L, cur, v1, t, now);
            finalize_create(L, t_cross, fix->mono_us, out, cap, n);
            return;
        }
    }
    L->prev_enu = cur;
    L->prev_gps_us = now;
    L->prev_speed_mps = v1;
    L->have_prev_fix = true;
}

/* NO_VENUE: scan for a venue every VENUE_SCAN_S (§10.3). A hit enters VENUE_FOUND (lap_set_venue). */
static void scan_for_venue(lap_t *L, const gps_fix_t *fix, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->state == LAP_ST_NO_VENUE, LAP_ASSERT_CODE);
    if (fix->valid == 0) return;
    const int64_t now = fix->gps_us;
    const double  lat = (double)fix->lat_e7 / 1e7;
    const double  lon = (double)fix->lon_e7 / 1e7;
    if (!L->have_scan || now - L->last_scan_us >= (int64_t)VENUE_SCAN_S * 1000000) {
        L->have_scan = true;
        L->last_scan_us = now;
        uint32_t dist_m = 0;
        const trk_venue_t *v = trk_find_nearest(lat, lon, &dist_m);
        if (v) {
            lap_set_venue(L, v);
            emit(out, cap, n, EV_VENUE_FOUND, 0, v->id, now, fix->mono_us, 0, 0);
        }
    }
}

/* Act on an accepted S/F crossing at t_cross (§10.3/§10.4/§10.5): debounce the matched gates, then
 * either open the out-lap and narrow candidates (ARMED) or complete the current lap (RUNNING). */
static void process_sf_crossing(lap_t *L, const gps_fix_t *fix, int64_t t_cross,
                                const bool *matched, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(matched != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* loop bound */
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
        open_lap(L, 0, t_cross);
        if (L->n_cand == 1) {
            lock_layout(L, 0, t_cross, fix->mono_us, out, cap, n);   /* single/forced layout */
        } else {
            L->locked = false;
            for (uint8_t i = 0; i < L->n_cand; i++) {
                L->cand[i].hits_matching = 0;
                L->cand[i].hits_foreign = 0;
            }
            build_union(L);
        }
    } else {
        if (L->lap_no == 0 && !L->locked)
            disambiguate_and_lock(L, t_cross, fix->mono_us, out, cap, n);   /* §10.5 */
        complete_lap(L, t_cross, fix->mono_us, out, cap, n);
    }
}

/* Evaluate the previous→current segment (§6.4) while ARMED/RUNNING: re-arm S/F, evaluate sector gates,
 * then detect the earliest accepted S/F crossing and act on it. */
static void process_segment(lap_t *L, geo_enu_t cur, const gps_fix_t *fix, event_t *out, int cap, int *n)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->n_cand <= TRK_MAX_LAYOUTS, LAP_ASSERT_CODE);   /* matched[]/loop bound */
    if (!((L->state == LAP_ST_ARMED || L->state == LAP_ST_RUNNING) && L->have_prev_fix)) return;
    const int64_t now = fix->gps_us;
    const double  v1  = (double)fix->gspeed_mms / 1000.0;
    rearm_sf(L, cur, now);

    /* Sectors are evaluated before the S/F so a sector crossing lands in the lap it belongs to
     * (§6.4: crossings in a segment are processed in t_cross order; a sector gate precedes the
     * S/F that ends the lap for any realistic gate spacing). */
    if (L->state == LAP_ST_RUNNING) {
        if (L->locked)            evaluate_locked_sectors(L, cur, fix, out, cap, n);
        else if (L->n_ugate != 0) evaluate_union(L, cur, fix);
    }

    /* S/F crossing (§6.4) over the previous→current segment. */
    bool   matched[TRK_MAX_LAYOUTS];
    bool   hit = false;
    double t_earliest = 2.0;    /* sentinel > 1.0; geo_segment_cross's t is in [0,1] */
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
        const int64_t t_cross = cross_time(L, cur, v1, t_earliest, now);
        process_sf_crossing(L, fix, t_cross, matched, out, cap, n);
    }
}

/* While a lap is in progress (§10.6/§10.5/§10.11): pit detection, Doppler-integrated lap distance and
 * the predictive trace of the lap in progress. */
static void update_running_progress(lap_t *L, const gps_fix_t *fix)
{
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fix != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->state == LAP_ST_RUNNING, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(L->lap_dist_m >= 0.0, LAP_ASSERT_CODE);   /* integrated distance non-negative */
    const int64_t now = fix->gps_us;
    const double  v1  = (double)fix->gspeed_mms / 1000.0;
    const double  kmh = (double)fix->gspeed_mms * 0.0036;      /* mm/s → km/h */
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
            L->lap_dist_m += 0.5 * (L->prev_speed_mps + v1) * seg_dt;
    }
    pred_record(L, now);                                      /* §10.11 record the lap in progress */
}

void lap_on_fix(lap_t *L, const gps_fix_t *fix, const fused_sample_t *fs, event_t *out, int cap, int *n)
{
    (void)fs;                                    /* fused stats feed sector stats in a later session */
    CORE_ASSERT_VOID(L != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL && n != NULL && cap > 0, LAP_ASSERT_CODE);   /* caller-drained event buffer */
    *n = 0;                                      /* engine appends this fix's events; caller drains them */
    if (!fix) return;
    CORE_ASSERT_VOID(L->state <= LAP_ST_RUNNING, LAP_ASSERT_CODE);   /* valid state enum */
    const bool    valid = fix->valid != 0;
    const int64_t now   = fix->gps_us;
    const double  lat   = (double)fix->lat_e7 / 1e7;
    const double  lon   = (double)fix->lon_e7 / 1e7;
    const double  v1    = (double)fix->gspeed_mms / 1000.0;

    if (L->mode == LAP_MODE_CREATE) {
        handle_create_mode(L, fix, out, cap, n);
        return;
    }

    /* NO_VENUE: scan; if still no venue after the scan, nothing more to do this fix. */
    if (L->state == LAP_ST_NO_VENUE) {
        scan_for_venue(L, fix, out, cap, n);
        if (L->state == LAP_ST_NO_VENUE) return;
    }

    /* An invalid fix while a venue is held only sets the GPS-lost flag if a lap is running (§6.4
     * step 1); its position is unusable, so no crossing, distance, prev or leave-timer update. */
    if (!valid) {
        if (L->state == LAP_ST_RUNNING) L->lap_flags |= LAP_F_GPS_LOST;
        return;
    }
    CORE_ASSERT_VOID(v1 >= 0.0, LAP_ASSERT_CODE);   /* ground speed non-negative (stored into prev_speed_mps below) */

    /* Leave-venue watch runs in every venue-bound state (§10.3). */
    if (update_leave_venue(L, lat, lon, now)) {
        reset_to_no_venue(L);
        return;
    }

    /* VENUE_FOUND → ARMED on this same valid fix (§10.3, "immediately"). */
    if (L->state == LAP_ST_VENUE_FOUND) {
        L->state = LAP_ST_ARMED;
        emit(out, cap, n, EV_ARMED, 0, 0, now, fix->mono_us, 0, 0);
    }

    const geo_enu_t cur = geo_to_enu(&L->origin, lat, lon);

    process_segment(L, cur, fix, out, cap, n);

    if (L->state == LAP_ST_RUNNING) update_running_progress(L, fix);

    /* Remember this valid fix as the start of the next segment (§6.4 needs two valid fixes). */
    L->prev_enu = cur;
    L->prev_gps_us = now;
    L->prev_speed_mps = v1;
    L->have_prev_fix = true;
}
