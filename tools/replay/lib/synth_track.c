#include "replay/synth.h"
#include "core/consts.h"
#include "core/geo.h"
#include "core/trk.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Closed-form circuit and run model (spec §22.2, plan 02 session 2.1).
 *
 * The lap is a scaled N-gon whose corners are circular arcs of cfg.corner_radius_m. Lap-frame arc
 * length s runs 0 .. length_m starting at the end of the arc at vertex 0 (= the start of straight 0);
 * the driving order is straight 0, arc 1, straight 1, arc 2, ... straight N-1, arc 0. Every piece has
 * constant acceleration, so position, speed and time invert in closed form and gate crossing times
 * are exact. Each lap table scales both cfg.v_corner_mps and cfg.v_max_mps by one draw
 * k = 1 + lap_var*U(-1,1), so a lap is driven faster or slower as a whole and lap times differ even
 * on a track whose straights never reach the cruise cap. */

#define TWO_PI      (2.0 * GEO_PI)
#define RAD_PER_DEG (GEO_PI / 180.0)
#define DEG_PER_RAD (180.0 / GEO_PI)

/* A vertex whose turn is smaller than this has an arc shorter than a micrometre: the piece table
 * needs len > 0 everywhere, so such a configuration is rejected instead of silently degenerate. */
static const double MIN_TURN_RAD = 1e-6;
/* atan2 returns (-pi, pi]; a turn of pi is a hairpin back along the same edge and tan(pi/2) = inf. */
static const double MAX_TURN_RAD = GEO_PI - 1e-9;
/* Beyond this the radius factor 1 + irregularity*U(-1,1) can reach 0 and the polygon collapses. */
static const double MAX_IRREGULARITY = 0.9;
/* Same reason for the per-lap speed scale 1 + lap_var*U(-1,1): k stays in [0.5, 1.5]. */
static const double MAX_LAP_VAR = 0.5;
/* trk_validate_venue rejects a gate line shorter than 1 m (§10.1), so each half must reach 0.5 m. */
static const double MIN_GATE_HALF_M = 0.5;
/* Two gates are "the same point" when their adjusted arc lengths agree to within a nanometre. */
static const double SAME_POINT_M = 1e-9;

/* ---------------------------------------------------------------------------
 * Private RNG. splitmix64, deliberately NOT synth_rng_* from synth_gps.h: that stream belongs to
 * synth_gps.c and this file must not depend on it. Both are deterministic; they need not agree.
 * ------------------------------------------------------------------------- */
static void sr_seed(uint64_t *s, uint32_t seed) { *s = 0x9E3779B97F4A7C15ull ^ (uint64_t)seed; }

static uint64_t sr_next(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* U(-1, 1) with 53 random bits. */
static double sr_sym(uint64_t *s)
{
    double u = (double)(sr_next(s) >> 11) * (1.0 / 9007199254740992.0);   /* [0, 1) */
    return 2.0 * u - 1.0;
}

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */
static double wrap_s(double s, double len)
{
    double w = fmod(s, len);
    if (w < 0.0) w += len;
    if (w >= len) w = 0.0;                 /* fmod can return len after rounding a tiny negative */
    return w;
}

static double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

static int fail(char *err, size_t err_cap, const char *msg)
{
    if (err && err_cap) snprintf(err, err_cap, "%s", msg);
    return -1;
}

/* Build-time geometry: everything the piece tables and the gates are cut from. Not stored in
 * synth_run_t (the header fixes that layout) and not global (the tools keep no mutable globals). */
typedef struct {
    int    n;
    double sx[SYNTH_MAX_VERTICES], sy[SYNTH_MAX_VERTICES];   /* ENU point where straight k starts */
    double ax[SYNTH_MAX_VERTICES], ay[SYNTH_MAX_VERTICES];   /* ENU point where arc k starts */
    double head[SYNTH_MAX_VERTICES];                          /* compass heading of edge k, radians */
    double st_s0[SYNTH_MAX_VERTICES];                         /* lap-frame arc length where straight k starts */
} synth_geom_t;

/* ---------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */
void synth_cfg_defaults(synth_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->n_vertices       = 12;
    c->length_m         = 2500.0;
    c->corner_radius_m  = 40.0;
    c->irregularity     = 0.0;
    c->clockwise        = true;
    c->seed             = 1;
    c->v_corner_mps     = 15.0;
    c->v_max_mps        = 50.0;
    c->a_acc_mps2       = 3.0;
    c->a_brk_mps2       = 6.0;
    c->lap_var          = 0.03;
    c->laps             = 10;
    c->start_before_m   = 300.0;
    c->stop_after_m     = 200.0;
    c->sf_frac          = 0.5 / 12.0;      /* half of the first straight of the default 12-gon */
    c->n_sector_gates   = 2;
    c->gate_half_width_m = GATE_HALF_WIDTH_M;
    c->origin_lat_deg   = -34.0300;
    c->origin_lon_deg   = 18.7300;
    c->venue_id         = TRK_USER_ID_BASE;
}

static int check_cfg(const synth_cfg_t *c, char *err, size_t err_cap)
{
    if (c->n_vertices < 3 || c->n_vertices > SYNTH_MAX_VERTICES) return fail(err, err_cap, "n_vertices out of range");
    if (c->laps < 1 || c->laps > SYNTH_MAX_LAPS) return fail(err, err_cap, "laps out of range");
    if (c->n_sector_gates < 0 || c->n_sector_gates > LAP_MAX_SECTORS) return fail(err, err_cap, "n_sector_gates out of range");
    if (!isfinite(c->length_m) || c->length_m <= 0.0) return fail(err, err_cap, "length_m must be positive");
    if (!isfinite(c->corner_radius_m) || c->corner_radius_m <= 0.0) return fail(err, err_cap, "corner_radius_m must be positive");
    if (!isfinite(c->irregularity) || c->irregularity < 0.0 || c->irregularity > MAX_IRREGULARITY) return fail(err, err_cap, "irregularity out of range");
    if (!isfinite(c->lap_var) || c->lap_var < 0.0 || c->lap_var > MAX_LAP_VAR) return fail(err, err_cap, "lap_var out of range");
    if (!isfinite(c->v_corner_mps) || c->v_corner_mps <= 0.0) return fail(err, err_cap, "v_corner_mps must be positive");
    /* lap_var scales both speeds by the same k, so this ordering holds in every table. */
    if (!isfinite(c->v_max_mps) || c->v_max_mps <= c->v_corner_mps) return fail(err, err_cap, "v_max_mps must exceed v_corner_mps");
    if (!isfinite(c->a_acc_mps2) || c->a_acc_mps2 <= 0.0) return fail(err, err_cap, "a_acc_mps2 must be positive");
    if (!isfinite(c->a_brk_mps2) || c->a_brk_mps2 <= 0.0) return fail(err, err_cap, "a_brk_mps2 must be positive");
    if (!isfinite(c->start_before_m) || c->start_before_m < 0.0) return fail(err, err_cap, "start_before_m must be >= 0");
    if (!isfinite(c->stop_after_m) || c->stop_after_m < 0.0) return fail(err, err_cap, "stop_after_m must be >= 0");
    if (c->start_before_m + c->stop_after_m >= c->length_m) return fail(err, err_cap, "start_before_m + stop_after_m >= length_m");
    if (!isfinite(c->sf_frac) || c->sf_frac < 0.0 || c->sf_frac >= 1.0) return fail(err, err_cap, "sf_frac out of range");
    if (!isfinite(c->gate_half_width_m) || c->gate_half_width_m < MIN_GATE_HALF_M) return fail(err, err_cap, "gate_half_width_m too small");
    if (!isfinite(c->origin_lat_deg) || c->origin_lat_deg < -89.0 || c->origin_lat_deg > 89.0) return fail(err, err_cap, "origin_lat_deg out of range");
    if (!isfinite(c->origin_lon_deg) || c->origin_lon_deg < -180.0 || c->origin_lon_deg > 180.0) return fail(err, err_cap, "origin_lon_deg out of range");
    if (c->venue_id == 0) return fail(err, err_cap, "venue_id must be non-zero");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Geometry
 * ------------------------------------------------------------------------- */
static int build_geometry(synth_run_t *r, synth_geom_t *g, uint64_t *rs, char *err, size_t err_cap)
{
    const synth_cfg_t *c = &r->cfg;
    const int n = c->n_vertices;
    double ux[SYNTH_MAX_VERTICES], uy[SYNTH_MAX_VERTICES];   /* unit polygon (circumradius ~1) */
    double dx[SYNTH_MAX_VERTICES], dy[SYNTH_MAX_VERTICES];   /* unit direction of edge k */
    double ulen[SYNTH_MAX_VERTICES];                          /* unit-polygon length of edge k */
    double tan_len[SYNTH_MAX_VERTICES];                       /* tangent length taken off each side of vertex k */

    /* Vertex k sits at compass bearing 2*pi*k/n from the centre, walked clockwise when cfg.clockwise,
     * so driving k = 0, 1, ... runs the requested way round and every corner is a right-hand turn. */
    for (int k = 0; k < n; k++) {
        double rho = 1.0 + c->irregularity * sr_sym(rs);
        double phi = TWO_PI * (double)k / (double)n;
        double bearing = c->clockwise ? phi : -phi;
        ux[k] = rho * sin(bearing);
        uy[k] = rho * cos(bearing);
    }
    for (int k = 0; k < n; k++) {
        int k1 = (k + 1) % n;
        double ex = ux[k1] - ux[k], ey = uy[k1] - uy[k];
        double L = sqrt(ex * ex + ey * ey);
        if (!(L > 0.0)) return fail(err, err_cap, "degenerate polygon edge");
        ulen[k] = L;
        dx[k] = ex / L;
        dy[k] = ey / L;
        g->head[k] = atan2(ex, ey);         /* compass heading: east over north */
    }

    double sum_u = 0.0, sum_t = 0.0, sum_arc = 0.0;
    for (int k = 0; k < n; k++) {
        int km = (k + n - 1) % n;
        double cr = dx[km] * dy[k] - dy[km] * dx[k];      /* + = left turn (CCW in ENU) */
        double dt = dx[km] * dx[k] + dy[km] * dy[k];
        double turn = atan2(cr, dt);
        if (fabs(turn) < MIN_TURN_RAD) return fail(err, err_cap, "a vertex is collinear (no corner)");
        if (fabs(turn) > MAX_TURN_RAD) return fail(err, err_cap, "a vertex turns back on itself");
        r->turn_rad[k] = turn;
        r->arc_len[k]  = c->corner_radius_m * fabs(turn);
        tan_len[k]     = c->corner_radius_m * tan(fabs(turn) / 2.0);
        sum_u   += ulen[k];
        sum_t   += tan_len[k];
        sum_arc += r->arc_len[k];
    }

    /* Driven length P(S) = S*sum_u - 2*sum_t + sum_arc; solve P(S) = cfg.length_m. */
    double S = (c->length_m + 2.0 * sum_t - sum_arc) / sum_u;
    if (!(S > 0.0)) return fail(err, err_cap, "corner_radius_m too large for length_m");

    double total = 0.0;
    for (int k = 0; k < n; k++) {
        r->ve[k] = S * ux[k];
        r->vn[k] = S * uy[k];
    }
    for (int k = 0; k < n; k++) {
        int k1 = (k + 1) % n;
        double edge = S * ulen[k];
        if (tan_len[k] + tan_len[k1] >= edge) return fail(err, err_cap, "corner arcs do not fit on an edge");
        r->straight_len[k] = edge - tan_len[k] - tan_len[k1];
        if (r->straight_len[k] < 2.0 * SYNTH_GATE_MARGIN_M) return fail(err, err_cap, "a straight is shorter than 2*SYNTH_GATE_MARGIN_M");
        total += r->straight_len[k] + r->arc_len[k];
    }
    r->length_m   = total;
    r->n_vertices = n;

    /* Anchor points: straight k starts tan_len[k] past vertex k along edge k; arc k starts
     * tan_len[k] before vertex k along edge k-1, which is exactly where straight k-1 ends. */
    g->n = n;
    for (int k = 0; k < n; k++) {
        int km = (k + n - 1) % n;
        g->sx[k] = r->ve[k] + tan_len[k] * dx[k];
        g->sy[k] = r->vn[k] + tan_len[k] * dy[k];
        g->ax[k] = r->ve[k] - tan_len[k] * dx[km];
        g->ay[k] = r->vn[k] - tan_len[k] * dy[km];
    }
    g->st_s0[0] = 0.0;
    for (int k = 1; k < n; k++) g->st_s0[k] = g->st_s0[k - 1] + r->straight_len[k - 1] + r->arc_len[k];
    return 0;
}

/* ---------------------------------------------------------------------------
 * Lap tables
 * ------------------------------------------------------------------------- */
static void push(synth_lap_t *tb, synth_piece_kind_t kind, double s0, double len, double t0, double dur,
                 double e0, double n0, double head0, double v0, double a,
                 double ce, double cn, double turn)
{
    synth_piece_t *p = &tb->pieces[tb->n_pieces++];
    p->kind = kind;
    p->s0 = s0; p->len = len;
    p->t0 = t0; p->dur = dur;
    p->e0 = e0; p->n0 = n0;
    p->head0_rad = head0;
    p->v0 = v0; p->a = a;
    p->ce = ce; p->cn = cn; p->turn_rad = turn;
}

/* One lap driven at speed scale k: both the arc speed and the cruise cap are cfg's values times k,
 * so every table has a distinct lap time whether or not its straights reach the cap. */
static void build_table(synth_lap_t *tb, const synth_run_t *r, const synth_geom_t *g, double k_speed)
{
    const synth_cfg_t *c = &r->cfg;
    const double vc = c->v_corner_mps * k_speed, vmax = c->v_max_mps * k_speed;
    const double aa = c->a_acc_mps2, ab = c->a_brk_mps2, rc = c->corner_radius_m;
    double s = 0.0, t = 0.0;

    tb->n_pieces = 0;
    tb->v_corner_mps = vc;
    tb->v_max_mps = vmax;
    for (int j = 0; j < g->n; j++) {
        const double L = r->straight_len[j];
        const double h = g->head[j], sh = sin(h), ch = cos(h);

        /* Peak speed if the straight were pure accelerate-then-brake (no cruise):
         * v_p^2 = (2*a_acc*a_brk*L + v_c^2*(a_acc + a_brk)) / (a_acc + a_brk). */
        double vp2 = (2.0 * aa * ab * L + vc * vc * (aa + ab)) / (aa + ab);
        double vp = sqrt(vp2);
        double d_acc, d_cru, d_brk, v_top;
        if (vp <= vmax) {
            v_top = vp;
            d_acc = (vp2 - vc * vc) / (2.0 * aa);
            d_cru = 0.0;
            d_brk = L - d_acc;                       /* closes the straight exactly */
        } else {
            v_top = vmax;
            d_acc = (vmax * vmax - vc * vc) / (2.0 * aa);
            d_brk = (vmax * vmax - vc * vc) / (2.0 * ab);
            d_cru = L - d_acc - d_brk;
        }
        double t_acc = (v_top - vc) / aa;
        double t_brk = (v_top - vc) / ab;

        push(tb, SYNTH_P_ACCEL, s, d_acc, t, t_acc, g->sx[j], g->sy[j], h, vc, aa, 0.0, 0.0, 0.0);
        s += d_acc; t += t_acc;
        if (d_cru > 0.0) {
            push(tb, SYNTH_P_CRUISE, s, d_cru, t, d_cru / v_top, g->sx[j] + d_acc * sh, g->sy[j] + d_acc * ch,
                 h, v_top, 0.0, 0.0, 0.0, 0.0);
            s += d_cru; t += d_cru / v_top;
        }
        push(tb, SYNTH_P_BRAKE, s, d_brk, t, t_brk, g->sx[j] + (d_acc + d_cru) * sh, g->sy[j] + (d_acc + d_cru) * ch,
             h, v_top, -ab, 0.0, 0.0, 0.0);
        s += d_brk; t += t_brk;

        /* The arc at vertex j+1 closes this edge; arc 0 is last and closes the lap. */
        int ka = (j + 1) % g->n;
        double turn = r->turn_rad[ka], alen = r->arc_len[ka];
        /* Centre lies corner_radius_m to the inside of the turn, perpendicular to the entry heading:
         * left normal of compass heading h is (-cos h, sin h), right normal is (cos h, -sin h). */
        double nx = (turn > 0.0) ? -ch : ch;
        double ny = (turn > 0.0) ?  sh : -sh;
        push(tb, SYNTH_P_ARC, s, alen, t, alen / vc, g->ax[ka], g->ay[ka], h, vc, 0.0,
             g->ax[ka] + rc * nx, g->ay[ka] + rc * ny, turn);
        s += alen; t += alen / vc;
    }
    tb->lap_time_s = t;
}

/* Position and compass heading ds metres into a piece. */
static void piece_point(const synth_piece_t *p, double ds, double *e, double *nn, double *head_rad)
{
    if (p->kind == SYNTH_P_ARC) {
        double rot = p->turn_rad * (ds / p->len);      /* CCW-positive rotation of the ENU radius vector */
        double vx = p->e0 - p->ce, vy = p->n0 - p->cn;
        double cs = cos(rot), sn = sin(rot);
        *e  = p->ce + vx * cs - vy * sn;
        *nn = p->cn + vx * sn + vy * cs;
        *head_rad = p->head0_rad - rot;                /* compass angle runs the opposite way to ENU */
    } else {
        *e  = p->e0 + ds * sin(p->head0_rad);
        *nn = p->n0 + ds * cos(p->head0_rad);
        *head_rad = p->head0_rad;
    }
}

static int piece_by_s(const synth_lap_t *tb, double s)
{
    for (int i = 0; i < tb->n_pieces; i++) if (s < tb->pieces[i].s0 + tb->pieces[i].len) return i;
    return tb->n_pieces - 1;
}

static int piece_by_t(const synth_lap_t *tb, double t)
{
    for (int i = 0; i < tb->n_pieces; i++) if (t < tb->pieces[i].t0 + tb->pieces[i].dur) return i;
    return tb->n_pieces - 1;
}

/* Lap-frame time at lap-frame arc length s. */
static double lap_time_at_s(const synth_lap_t *tb, double s)
{
    int j = piece_by_s(tb, s);
    const synth_piece_t *p = &tb->pieces[j];
    double ds = clampd(s - p->s0, 0.0, p->len);
    double tau;
    if (p->a != 0.0) {
        double disc = p->v0 * p->v0 + 2.0 * p->a * ds;
        if (disc < 0.0) disc = 0.0;
        tau = (sqrt(disc) - p->v0) / p->a;
    } else {
        tau = ds / p->v0;
    }
    return p->t0 + tau;
}

/* Geometry at lap-frame arc length s. Table 0's pieces carry the same path as every other table
 * (only the piece boundaries move with the table's speeds). */
static void geom_at_s(const synth_run_t *r, double s, double *e, double *nn, double *head_rad)
{
    const synth_lap_t *tb = &r->tables[0];
    int j = piece_by_s(tb, s);
    piece_point(&tb->pieces[j], clampd(s - tb->pieces[j].s0, 0.0, tb->pieces[j].len), e, nn, head_rad);
}

static double head_deg_norm(double head_rad)
{
    double d = fmod(head_rad * DEG_PER_RAD, 360.0);
    if (d < 0.0) d += 360.0;
    if (d >= 360.0) d = 0.0;
    return d;
}

/* ---------------------------------------------------------------------------
 * Gates
 * ------------------------------------------------------------------------- */
/* First arc length at or after s that is on a straight and at least SYNTH_GATE_MARGIN_M from both of
 * its ends (session ruling: gates live on straights only). */
static double first_admissible(const synth_run_t *r, const synth_geom_t *g, double s)
{
    const double m = SYNTH_GATE_MARGIN_M;
    for (int k = 0; k < g->n; k++) {
        double lo = g->st_s0[k] + m, hi = g->st_s0[k] + r->straight_len[k] - m;
        if (s >= lo && s <= hi) return s;
    }
    double best = 0.0, best_d = -1.0;
    for (int k = 0; k < g->n; k++) {
        double lo = wrap_s(g->st_s0[k] + m, r->length_m);
        double d  = wrap_s(lo - s, r->length_m);
        if (best_d < 0.0 || d < best_d) { best_d = d; best = lo; }
    }
    return best;
}

static void fill_gate(const synth_run_t *r, synth_gate_t *gate, double s)
{
    double e, nn, head;
    geom_at_s(r, s, &e, &nn, &head);
    double sh = sin(head), ch = cos(head);
    double half = r->cfg.gate_half_width_m;
    /* Left normal of compass heading h in ENU is (-cos h, sin h): with p1 = centre + half*left and
     * p2 = centre - half*left, sign(cross(p2 - p1, motion)) = +1, which is the dir_sign of §6.4. */
    gate->s_m = s;
    gate->e_m = e;
    gate->n_m = nn;
    gate->heading_deg = head_deg_norm(head);
    synth_enu_to_ll(r->cfg.origin_lat_deg, r->cfg.origin_lon_deg, e + half * -ch, nn + half * sh,
                    &gate->line.p1.lat, &gate->line.p1.lon);
    synth_enu_to_ll(r->cfg.origin_lat_deg, r->cfg.origin_lon_deg, e - half * -ch, nn - half * sh,
                    &gate->line.p2.lat, &gate->line.p2.lon);
}

static int build_gates(synth_run_t *r, const synth_geom_t *g, char *err, size_t err_cap)
{
    const synth_cfg_t *c = &r->cfg;
    r->n_gates = 1 + c->n_sector_gates;
    double s_sf = first_admissible(r, g, wrap_s(c->sf_frac * r->length_m, r->length_m));
    fill_gate(r, &r->gates[0], s_sf);
    double step = r->length_m / (double)(c->n_sector_gates + 1);
    for (int i = 1; i <= c->n_sector_gates; i++) {
        double want = wrap_s(s_sf + (double)i * step, r->length_m);
        fill_gate(r, &r->gates[i], first_admissible(r, g, want));
    }
    for (int i = 0; i < r->n_gates; i++)
        for (int j = i + 1; j < r->n_gates; j++)
            if (fabs(r->gates[i].s_m - r->gates[j].s_m) < SAME_POINT_M)
                return fail(err, err_cap, "two gates adjusted onto the same point");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Build
 * ------------------------------------------------------------------------- */
int synth_run_build(synth_run_t *r, const synth_cfg_t *cfg, char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (check_cfg(cfg, err, err_cap) != 0) return -1;

    memset(r, 0, sizeof *r);
    r->cfg = *cfg;

    uint64_t rs;
    sr_seed(&rs, cfg->seed);                       /* vertex radii first, then one speed scale per table */

    synth_geom_t g;
    memset(&g, 0, sizeof g);
    if (build_geometry(r, &g, &rs, err, err_cap) != 0) return -1;

    r->n_tables = cfg->laps + 3;                   /* out-lap, the laps themselves, and the run-out */
    double acc = 0.0;
    for (int i = 0; i < r->n_tables; i++) {
        r->table_t0[i] = acc;
        build_table(&r->tables[i], r, &g, 1.0 + cfg->lap_var * sr_sym(&rs));
        acc += r->tables[i].lap_time_s;
    }

    if (build_gates(r, &g, err, err_cap) != 0) return -1;

    r->s_total_start = wrap_s(r->gates[0].s_m - cfg->start_before_m, r->length_m);   /* in table 0 */
    r->t_offset_s    = r->table_t0[0] + lap_time_at_s(&r->tables[0], r->s_total_start);
    r->s_total_end   = synth_run_sf_s_total(r, cfg->laps) + cfg->stop_after_m;
    r->duration_s    = synth_run_time_at_s(r, r->s_total_end);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Run queries
 * ------------------------------------------------------------------------- */
double synth_run_sf_s_total(const synth_run_t *r, int n)
{
    double s_sf = r->gates[0].s_m;
    /* The run starts start_before_m before S/F; when that wraps behind S/F the first crossing is one
     * lap further along the unwrapped axis. */
    double extra = (r->s_total_start > s_sf) ? r->length_m : 0.0;
    return s_sf + (double)n * r->length_m + extra;
}

double synth_run_time_at_s(const synth_run_t *r, double s_total_m)
{
    double s_tot = clampd(s_total_m, r->s_total_start, r->s_total_end);
    int i = (int)floor(s_tot / r->length_m);
    if (i < 0) i = 0;
    if (i > r->n_tables - 1) i = r->n_tables - 1;
    double s = s_tot - (double)i * r->length_m;
    return r->table_t0[i] + lap_time_at_s(&r->tables[i], s) - r->t_offset_s;
}

void synth_run_state_at(const synth_run_t *r, double t_s, synth_state_t *out)
{
    double T = clampd(t_s, 0.0, r->duration_s) + r->t_offset_s;
    int i = 0;
    for (int k = 0; k < r->n_tables; k++) { if (r->table_t0[k] <= T) i = k; else break; }
    const synth_lap_t *tb = &r->tables[i];
    int j = piece_by_t(tb, T - r->table_t0[i]);
    const synth_piece_t *p = &tb->pieces[j];
    double tau = clampd(T - r->table_t0[i] - p->t0, 0.0, p->dur);
    double ds = p->v0 * tau + 0.5 * p->a * tau * tau;
    double v  = p->v0 + p->a * tau;

    double e, nn, head;
    piece_point(p, ds, &e, &nn, &head);
    double s_m = p->s0 + ds;
    int table = i;
    if (s_m >= r->length_m && table + 1 < r->n_tables) { s_m -= r->length_m; table++; }
    s_m = clampd(s_m, 0.0, r->length_m);

    out->table        = table;
    out->s_m          = s_m;
    out->s_total_m    = (double)table * r->length_m + s_m;
    out->e_m          = e;
    out->n_m          = nn;
    out->heading_deg  = head_deg_norm(head);
    out->v_mps        = v;
    out->a_lon_mps2   = p->a;
    out->on_arc       = (p->kind == SYNTH_P_ARC);
    if (out->on_arc) {
        double a_lat = v * v / (r->cfg.corner_radius_m * G_MPS2);     /* in g */
        double sign  = (p->turn_rad < 0.0) ? 1.0 : -1.0;              /* + to the right (core/types.h) */
        out->g_lat        = sign * a_lat;
        out->lean_deg     = sign * atan(a_lat) * DEG_PER_RAD;
        out->yaw_rate_dps = p->turn_rad / p->dur * DEG_PER_RAD;       /* + for a left turn */
    } else {
        out->g_lat = 0.0;
        out->lean_deg = 0.0;
        out->yaw_rate_dps = 0.0;
    }
}

int synth_run_lap_crossings(const synth_run_t *r, int lap_no, double *out, size_t out_cap)
{
    if (out == NULL) return -1;
    if (lap_no < 1 || lap_no > r->cfg.laps) return -1;
    if (out_cap < (size_t)(r->n_gates + 1)) return -1;
    double base = synth_run_sf_s_total(r, lap_no - 1);
    double s_sf = r->gates[0].s_m;
    out[0] = synth_run_time_at_s(r, base);
    for (int g = 1; g < r->n_gates; g++)
        out[g] = synth_run_time_at_s(r, base + wrap_s(r->gates[g].s_m - s_sf, r->length_m));
    out[r->n_gates] = synth_run_time_at_s(r, base + r->length_m);
    return r->n_gates + 1;
}

double synth_run_lap_time(const synth_run_t *r, int lap_no)
{
    double t[SYNTH_MAX_GATES + 1];
    int n = synth_run_lap_crossings(r, lap_no, t, sizeof t / sizeof t[0]);
    if (n < 0) return 0.0;
    return t[n - 1] - t[0];
}

void synth_run_venue(const synth_run_t *r, trk_venue_t *v)
{
    memset(v, 0, sizeof *v);
    v->id = r->cfg.venue_id;
    snprintf(v->name, sizeof v->name, "Synthetic");
    v->lat = r->cfg.origin_lat_deg;
    v->lon = r->cfg.origin_lon_deg;
    v->radius_m = VENUE_RADIUS_DEFAULT_M;
    v->flags = TRK_F_UNVERIFIED;                 /* generated lines, never surveyed on site (§10.1) */
    v->n_layouts = 1;
    trk_layout_t *L = &v->layouts[0];
    L->id = 1;
    snprintf(L->name, sizeof L->name, "Full");
    L->dir_sign = 1;                             /* p1/p2 are ordered left/right in the driving sense */
    L->sf = r->gates[0].line;
    L->n_sectors = (uint8_t)(r->n_gates - 1);
    for (int i = 1; i < r->n_gates; i++) L->sectors[i - 1] = r->gates[i].line;
    L->length_m = (uint32_t)llround(r->length_m);
}

void synth_enu_to_ll(double lat0_deg, double lon0_deg, double e_m, double n_m, double *lat_deg, double *lon_deg)
{
    double cos_lat0 = cos(lat0_deg * RAD_PER_DEG);
    *lat_deg = lat0_deg + (n_m / GEO_EARTH_R_M) * DEG_PER_RAD;
    *lon_deg = lon0_deg + (e_m / (GEO_EARTH_R_M * cos_lat0)) * DEG_PER_RAD;
}
