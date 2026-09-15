#include "core/trk.h"
#include "core/geo.h"
#include "core/ses.h"
#include <math.h>
#include <string.h>

/* Not reentrant: the user store below is module-static, shared by every trk_* entry point.
 * Only the conn task adds/loads/saves venues and only the pipeline task reads them, and the two
 * never overlap (an upload is applied between sessions), so no lock is taken. */
static trk_venue_t user[TRK_MAX_USER];
static uint8_t     user_n;

#define BLOB_VERSION 2

void trk_init(void) { user_n = 0; memset(user, 0, sizeof user); }

int trk_user_count(void) { return user_n; }

static bool pt_finite(const trk_pt_t *p) { return isfinite(p->lat) && isfinite(p->lon); }
static bool line_finite(const trk_line_t *l) { return pt_finite(&l->p1) && pt_finite(&l->p2); }

#define MIN_GATE_LEN_M 1.0        /* a line shorter than this cannot define a crossing direction (§6.4) */
/* Shared by both entry points that can install a venue (trk_from_json, via the final
 * trk_validate_venue() call, and trk_user_load()/trk_user_add(), via this function directly), so
 * the two never disagree about what a valid gate line is. */
static bool line_ok(const trk_line_t *l) { return geo_dist_m(l->p1.lat, l->p1.lon, l->p2.lat, l->p2.lon) >= MIN_GATE_LEN_M; }

int trk_validate_venue(const trk_venue_t *v)
{
    if (v->id == 0) return -1;
    if (v->radius_m < 100 || v->radius_m > 50000) return -1;
    if (v->n_layouts < 1 || v->n_layouts > TRK_MAX_LAYOUTS) return -1;
    if (v->name[sizeof v->name - 1] != '\0') return -1;
    if (!isfinite(v->lat) || !isfinite(v->lon)) return -1;
    if (v->lat < -90.0 || v->lat > 90.0 || v->lon < -180.0 || v->lon > 180.0) return -1;
    for (uint8_t i = 0; i < v->n_layouts; i++) {
        const trk_layout_t *L = &v->layouts[i];
        if (L->id == 0) return -1;
        if (L->dir_sign != 1 && L->dir_sign != -1) return -1;
        if (L->n_sectors > LAP_MAX_SECTORS) return -1;
        if (L->name[sizeof L->name - 1] != '\0') return -1;
        if (!line_finite(&L->sf) || !line_ok(&L->sf)) return -1;
        for (uint8_t s = 0; s < L->n_sectors; s++) if (!line_finite(&L->sectors[s]) || !line_ok(&L->sectors[s])) return -1;
    }
    return 0;
}

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
    if (trk_validate_venue(v) != 0) return -1;
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
    const trk_venue_t *u = user_get(v->id);
    if (u && u != v) return;                                  /* bundled entry shadowed by a user entry */
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

/* Blob v2: u8 version=2 | u8 count | trk_venue_t[count] | u16 crc16 (LE) over every preceding byte.
 * The struct is copied raw, so the blob is only valid for this build; the CRC catches NVS bit rot
 * and every venue is re-validated before it reaches the store. Any failure leaves the store empty. */
int trk_user_load(const uint8_t *blob, size_t n)
{
    trk_init();
    if (n < 4 || blob[0] != BLOB_VERSION) return -1;
    uint8_t cnt = blob[1];
    if (cnt > TRK_MAX_USER) return -1;
    size_t need = 2 + (size_t)cnt * sizeof(trk_venue_t) + 2;
    if (n != need) return -1;
    uint16_t want = (uint16_t)(blob[need - 2] | ((uint16_t)blob[need - 1] << 8));
    if (ses_crc16(blob, need - 2) != want) return -1;
    for (uint8_t i = 0; i < cnt; i++) {
        memcpy(&user[i], blob + 2 + (size_t)i * sizeof(trk_venue_t), sizeof(trk_venue_t));
        if (trk_validate_venue(&user[i]) != 0) { trk_init(); return -1; }
    }
    user_n = cnt;
    return 0;
}

/* Field-by-field copy into an already-zeroed destination: struct assignment (or a raw memcpy) also
 * copies the source's compiler-inserted padding bytes verbatim, which the language never promises
 * are zero, so two structurally identical venues could otherwise CRC differently (§10.1's blob is
 * declared build-specific but should still be deterministic within one build). Every named field is
 * written explicitly; nothing else touches dst, so the gaps between fields stay at the memset zero. */
static void canon_venue(trk_venue_t *dst, const trk_venue_t *src)
{
    memset(dst, 0, sizeof *dst);
    dst->id = src->id;
    memcpy(dst->name, src->name, sizeof dst->name);
    dst->lat = src->lat; dst->lon = src->lon;
    dst->radius_m = src->radius_m;
    dst->flags = src->flags;
    dst->n_layouts = src->n_layouts;
    /* Only the active layouts/sectors (src has already passed trk_validate_venue, so n_layouts and
     * every n_sectors are in range) are copied; slots beyond them are left at the memset zero rather
     * than carrying through whatever unused array content src happened to hold. */
    for (uint8_t i = 0; i < src->n_layouts && i < TRK_MAX_LAYOUTS; i++) {
        const trk_layout_t *sl = &src->layouts[i];
        trk_layout_t       *dl = &dst->layouts[i];
        dl->id = sl->id;
        memcpy(dl->name, sl->name, sizeof dl->name);
        dl->sf = sl->sf;                    /* trk_line_t is four packed doubles: no internal padding */
        dl->dir_sign = sl->dir_sign;
        dl->n_sectors = sl->n_sectors;
        for (uint8_t s = 0; s < sl->n_sectors && s < LAP_MAX_SECTORS; s++) dl->sectors[s] = sl->sectors[s];
        dl->length_m = sl->length_m;
    }
}

int trk_user_save(uint8_t *blob, size_t cap, size_t *n_out)
{
    size_t need = 2 + (size_t)user_n * sizeof(trk_venue_t) + 2;
    if (cap < need) return -1;
    blob[0] = BLOB_VERSION; blob[1] = user_n;
    /* static, not a stack local: this module is already non-reentrant (see the file-top comment),
     * and a ~2.7 KB trk_venue_t on the stack here would eat into the conn task's budget for no
     * reason. */
    static trk_venue_t tmp;
    for (uint8_t i = 0; i < user_n; i++) {
        canon_venue(&tmp, &user[i]);
        memcpy(blob + 2 + (size_t)i * sizeof(trk_venue_t), &tmp, sizeof tmp);
    }
    uint16_t crc = ses_crc16(blob, need - 2);
    blob[need - 2] = (uint8_t)crc; blob[need - 1] = (uint8_t)(crc >> 8);
    *n_out = need;
    return 0;
}
