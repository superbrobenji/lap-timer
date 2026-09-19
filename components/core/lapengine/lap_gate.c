#include "core/lap.h"
#include "core/core.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Power of 10 rule 5: shared lapengine assertion code; file:line at the hook pins the exact check. */
#define LAP_ASSERT_CODE 0x0A20

/* Pure gate geometry for on-device track creation (§10.9) and the auto reverse layout (§10.2). No
 * state, no allocation. Kept separate from lap.c so the state machine and the geometry test each build
 * on their own. */

#define DEG2RAD (GEO_PI / 180.0)
#define RAD2DEG (180.0 / GEO_PI)

/* Build a line perpendicular to a compass heading through (lat,lon), reaching GATE_HALF_WIDTH_M each
 * side (a 30 m gate). Heading is compass degrees (0 = north, clockwise positive), matching
 * gps_fix_t.head. In ENU (east, north) the motion unit vector is (sin h, cos h) and its left normal is
 * (-cos h, sin h); with p1 = centre + half*left and p2 = centre - half*left (the right end),
 * sign(cross(p2 - p1, motion)) = +1, so the layout's dir_sign is +1 and the current motion is
 * accepted (§6.4 step 3). Endpoints are offset in the local tangent plane the same way geo_to_enu
 * inverts, so the engine's ENU crossing test is exact. */
trk_line_t lap_gate_line(double lat_deg, double lon_deg, double heading_deg)
{
    CORE_ASSERT_RET(lat_deg >= -90.0 && lat_deg <= 90.0, LAP_ASSERT_CODE, (trk_line_t){0});
    CORE_ASSERT_RET(lon_deg >= -180.0 && lon_deg <= 180.0, LAP_ASSERT_CODE, (trk_line_t){0});
    CORE_ASSERT_RET(heading_deg >= 0.0 && heading_deg <= 360.0, LAP_ASSERT_CODE, (trk_line_t){0});
    const double h  = heading_deg * DEG2RAD;
    const double lx = -cos(h);      /* left-normal east component (unit) */
    const double ly = sin(h);       /* left-normal north component (unit) */
    const double cos_lat0 = cos(lat_deg * DEG2RAD);
    const double dlat = (GATE_HALF_WIDTH_M * ly / GEO_EARTH_R_M) * RAD2DEG;
    const double dlon = (GATE_HALF_WIDTH_M * lx / (GEO_EARTH_R_M * cos_lat0)) * RAD2DEG;

    trk_line_t line;
    line.p1.lat = lat_deg + dlat;   /* left end  = centre + half*left */
    line.p1.lon = lon_deg + dlon;
    line.p2.lat = lat_deg - dlat;   /* right end = centre - half*left */
    line.p2.lon = lon_deg - dlon;
    return line;
}

/* Derive the reverse layout of `fwd` into `rev`: same S/F line, the sector gates in reverse driving
 * order (each gate line is unchanged; the reverse layout crosses it the other way, so dir_sign is
 * negated and the §6.4 test still accepts it), id = fwd->id + 1, name "<fwd name> Reverse". */
void lap_layout_reverse(const trk_layout_t *fwd, trk_layout_t *rev)
{
    CORE_ASSERT_VOID(fwd != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(rev != NULL, LAP_ASSERT_CODE);
    CORE_ASSERT_VOID(fwd->n_sectors <= LAP_MAX_SECTORS, LAP_ASSERT_CODE);   /* sector reverse loop bound */
    *rev = *fwd;
    rev->id       = (uint16_t)(fwd->id + 1);
    rev->dir_sign = (int8_t)(-fwd->dir_sign);
    rev->sf       = fwd->sf;
    rev->n_sectors = fwd->n_sectors;
    for (uint8_t i = 0; i < fwd->n_sectors; i++)
        rev->sectors[i] = fwd->sectors[fwd->n_sectors - 1 - i];

    /* name[] is 24 bytes; " Reverse" is 8 chars, so keep at most (24 - 8 - 1) of the base name and the
     * suffix always fits (no format truncation). */
    char base[sizeof fwd->name];
    memcpy(base, fwd->name, sizeof base);
    base[sizeof base - 1] = '\0';
    snprintf(rev->name, sizeof rev->name, "%.*s Reverse", (int)(sizeof rev->name - 9), base);
}
