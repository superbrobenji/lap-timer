#include "core/trk.h"
#include "core/geo.h"
#include <string.h>

static trk_venue_t user[TRK_MAX_USER];
static uint8_t     user_n;

void trk_init(void) { user_n = 0; memset(user, 0, sizeof user); }

int trk_user_count(void) { return user_n; }

static const trk_venue_t *user_get(uint16_t id)
{
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id == id) return &user[i];
    return NULL;
}

const trk_venue_t *trk_get(uint16_t venue_id)
{
    const trk_venue_t *u = user_get(venue_id);
    if (u) return u;
    for (uint16_t i = 0; i < trk_bundled_count; i++) if (trk_bundled[i].id == venue_id) return &trk_bundled[i];
    return NULL;
}

int trk_user_add(const trk_venue_t *v)
{
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id == v->id) { user[i] = *v; return 0; }
    if (user_n >= TRK_MAX_USER) return -1;
    user[user_n++] = *v;
    return 0;
}

uint16_t trk_next_user_id(void)
{
    uint16_t id = TRK_USER_ID_BASE;
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id >= id) id = (uint16_t)(user[i].id + 1);
    return id;
}

static void consider(const trk_venue_t *v, double lat, double lon, const trk_venue_t **best, double *best_d)
{
    if (user_get(v->id) && v != user_get(v->id)) return;      /* bundled entry shadowed by a user entry */
    double d = geo_dist_m(lat, lon, v->lat, v->lon);
    if (d <= (double)v->radius_m && d < *best_d) { *best = v; *best_d = d; }
}

const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out)
{
    const trk_venue_t *best = NULL; double best_d = 1e12;
    for (uint8_t i = 0; i < user_n; i++) consider(&user[i], lat, lon, &best, &best_d);
    for (uint16_t i = 0; i < trk_bundled_count; i++) consider(&trk_bundled[i], lat, lon, &best, &best_d);
    if (best && dist_m_out) *dist_m_out = (uint32_t)best_d;
    return best;
}

int trk_user_load(const uint8_t *blob, size_t n)
{
    if (n < 2 || blob[0] != 1) return -1;
    uint8_t cnt = blob[1];
    if (cnt > TRK_MAX_USER || n != 2 + (size_t)cnt * sizeof(trk_venue_t)) return -1;
    memcpy(user, blob + 2, (size_t)cnt * sizeof(trk_venue_t));
    user_n = cnt;
    return 0;
}

int trk_user_save(uint8_t *blob, size_t cap, size_t *n_out)
{
    size_t need = 2 + (size_t)user_n * sizeof(trk_venue_t);
    if (cap < need) return -1;
    blob[0] = 1; blob[1] = user_n;
    memcpy(blob + 2, user, (size_t)user_n * sizeof(trk_venue_t));
    *n_out = need;
    return 0;
}
