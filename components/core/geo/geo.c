#include "core/geo.h"
#include <math.h>

#define DEG2RAD (GEO_PI / 180.0)

void geo_origin_set(geo_origin_t *o, double lat_deg, double lon_deg)
{
    o->lat0_rad = lat_deg * DEG2RAD;
    o->lon0_rad = lon_deg * DEG2RAD;
    o->cos_lat0 = cos(o->lat0_rad);
}

geo_enu_t geo_to_enu(const geo_origin_t *o, double lat_deg, double lon_deg)
{
    geo_enu_t e;
    e.x = (lon_deg * DEG2RAD - o->lon0_rad) * o->cos_lat0 * GEO_EARTH_R_M;
    e.y = (lat_deg * DEG2RAD - o->lat0_rad) * GEO_EARTH_R_M;
    return e;
}

double geo_dist_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
    double p1 = lat1_deg * DEG2RAD, p2 = lat2_deg * DEG2RAD;
    double dp = p2 - p1, dl = (lon2_deg - lon1_deg) * DEG2RAD;
    double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2.0 * GEO_EARTH_R_M * atan2(sqrt(a), sqrt(1.0 - a));
}

static double cross2(geo_enu_t u, geo_enu_t v) { return u.x * v.y - u.y * v.x; }

int geo_segment_cross(geo_enu_t a, geo_enu_t b, geo_enu_t p, geo_enu_t q, double *t_out, int *dir_sign_out)
{
    geo_enu_t r = { b.x - a.x, b.y - a.y };
    geo_enu_t s = { q.x - p.x, q.y - p.y };
    double den = cross2(r, s);
    if (fabs(den) < 1e-9) return 0;
    geo_enu_t qp = { p.x - a.x, p.y - a.y };
    double t = cross2(qp, s) / den;
    double u = cross2(qp, r) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    *t_out = t;
    *dir_sign_out = (cross2(s, r) > 0.0) ? 1 : -1;
    return 1;
}

double geo_dist_point_segment(geo_enu_t x, geo_enu_t p, geo_enu_t q)
{
    double vx = q.x - p.x, vy = q.y - p.y;
    double wx = x.x - p.x, wy = x.y - p.y;
    double len2 = vx * vx + vy * vy;
    double t = len2 > 0.0 ? (wx * vx + wy * vy) / len2 : 0.0;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    double cx = p.x + t * vx - x.x, cy = p.y + t * vy - x.y;
    return sqrt(cx * cx + cy * cy);
}

double geo_interp_time(double d, double v0, double v1, double dt)
{
    if (dt <= 0.0) return 0.0;
    if (d <= 0.0) return 0.0;
    double acc = (v1 - v0) / dt;
    double tau;
    if (fabs(acc) < 0.01) {
        tau = (v0 > 1e-6) ? d / v0 : dt;
    } else {
        double disc = v0 * v0 + 2.0 * acc * d;
        if (disc < 0.0) disc = 0.0;
        tau = (-v0 + sqrt(disc)) / acc;
    }
    if (tau < 0.0) tau = 0.0;
    if (tau > dt) tau = dt;
    return tau;
}
