#ifndef CORE_GEO_H
#define CORE_GEO_H
#include "core/consts.h"

#define GEO_PI        3.14159265358979323846
#define GEO_EARTH_R_M EARTH_R_M

typedef struct { double x, y; } geo_enu_t;                       /* metres east, north */
typedef struct { double lat0_rad, lon0_rad, cos_lat0; } geo_origin_t;

void      geo_origin_set(geo_origin_t *o, double lat_deg, double lon_deg);
geo_enu_t geo_to_enu(const geo_origin_t *o, double lat_deg, double lon_deg);
double    geo_dist_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);
/* Returns 1 if segment a→b properly crosses gate p→q. t_out = fraction along a→b (0..1).
 * dir_sign_out = sign(cross(q−p, b−a)): +1 when p is the left end of the gate seen from the motion. */
int       geo_segment_cross(geo_enu_t a, geo_enu_t b, geo_enu_t p, geo_enu_t q, double *t_out, int *dir_sign_out);
double    geo_dist_point_segment(geo_enu_t x, geo_enu_t p, geo_enu_t q);
/* Time τ (0..dt) to travel distance d along a segment entered at speed v0 and left at v1 after dt,
 * assuming constant acceleration. (§6.4 step 4) */
double    geo_interp_time(double d, double v0, double v1, double dt);
#endif
