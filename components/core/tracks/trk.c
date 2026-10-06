#include "core/trk.h"
#include "core/geo.h"
#include "core/ses.h"
#include "core/core.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/* Power of 10 rule 5 (spec §17.9, design doc §3): this module's assertions report TRK_ASSERT_CODE.
 * They guard genuine anomalies -- NULL params and internal invariants on the module-static user[]
 * store (it must never hold more than TRK_MAX_USER entries) -- never the rejection of an untrusted
 * venue/blob, which stays the existing plain `return -1` (that path is routine, exercised by real
 * uploads/loads, and must not fire the fault hook). */
#define TRK_ASSERT_CODE 0x0A80

/* Not reentrant: the user store below is module-static, shared by every trk_* entry point.
 *
 * Writers at boot, same task, strictly sequenced, no concurrency premise needed (final review
 * F-4/I-4, #97): main/app_main.c's boot_subsystems() calls trk_init() and then
 * logger_load_tracks() (app/logger.h -- implemented here, in logger.c, since it only reads) one
 * after the other, on app_main's own task, BEFORE logger_start() creates the logger task a few
 * lines later and well before pipeline_start() creates the pipeline task further down still. This
 * is ordinary single-threaded sequencing, not a scheduler-preemption argument: neither task
 * exists yet when either write runs, so there is nothing to race. (The previous version of this
 * comment relied on the logger task preempting app_main the instant xTaskCreateStaticPinnedToCore()
 * returned, which rested on two premises the comment never stated and nothing in the tree
 * enforced -- same-core task affinity, and the load helper never yielding -- so the final review
 * moved the load here instead of shoring up that proof; see logger.c's logger_load_tracks() for
 * where it now runs.) trk_user_add() -- invoked directly by lap.c's finalize_create() on a
 * CREATE-mode S/F crossing, and indirectly via trk_user_add_json() for the CFG_GPS_SIM venue
 * registration at pipeline_init() -- is on the pipeline task (core 1), after boot, and always
 * completes before the event that announces its id: each call site's emit(..., EV_VENUE_FOUND,
 * ...) is sequential code a few lines later in the same function, same task.
 *
 * Readers: the pipeline task itself (already ordered above, same task, no barrier needed), and --
 * since #98 -- the ui task (core 0), through trk_get() in handle_venue_found()/
 * handle_layout_locked()/build_menu()/menu_do_layout() (components/app/ui/ui.c). The ui only calls
 * trk_get() on an id after receiving the EV_VENUE_FOUND/EV_LAYOUT_LOCKED event that names it; the
 * FreeRTOS cross-core queue (g_ui_evt_q) send/receive is a full memory barrier, so every write
 * above is guaranteed visible by the time the ui's read runs. No lock is taken.
 *
 * This rests on one invariant the API itself does NOT enforce: no writer may rewrite or remove an
 * id the ui may already hold. trk_user_add()'s same-id "replace" branch is real and live -- it is
 * only safe today because every live call site supplies either a boot-time id (before any id is
 * surfaced to a reader) or a fresh one (lap.c's trk_next_user_id(), always one past every existing
 * id, so it can never collide with an id already seen). A future writer that could replace an id
 * already surfaced to the ui in the same session (a second track load after boot, a revived BLE
 * upload, ...) must run strictly before that id's first EV_VENUE_FOUND/EV_LAYOUT_LOCKED of the
 * session, or take a lock/seqlock (pipeline.c's seq_enter()/seq_leave() around s_best is the
 * existing pattern for this shape of problem) -- "no lock is taken" above holds only as long as
 * that ordering does. */
static trk_venue_t user[TRK_MAX_USER];
static uint8_t     user_n;

void trk_init(void) { user_n = 0; memset(user, 0, sizeof user); }

int trk_user_count(void)
{
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, 0);
    return user_n;
}

static bool pt_finite(const trk_pt_t *p)
{
    CORE_ASSERT_RET(p != NULL, TRK_ASSERT_CODE, false);
    return isfinite(p->lat) && isfinite(p->lon);
}
static bool line_finite(const trk_line_t *l)
{
    CORE_ASSERT_RET(l != NULL, TRK_ASSERT_CODE, false);
    return pt_finite(&l->p1) && pt_finite(&l->p2);
}

#define MIN_GATE_LEN_M 1.0        /* a line shorter than this cannot define a crossing direction (§6.4) */
/* Shared by every entry point that can install a venue (trk_from_json and trk_user_add(), both
 * via the final trk_validate_venue() call), so they never disagree about what a valid gate line
 * is. */
static bool line_ok(const trk_line_t *l)
{
    CORE_ASSERT_RET(l != NULL, TRK_ASSERT_CODE, false);
    return geo_dist_m(l->p1.lat, l->p1.lon, l->p2.lat, l->p2.lon) >= MIN_GATE_LEN_M;
}

int trk_validate_venue(const trk_venue_t *v)
{
    CORE_ASSERT_RET(v != NULL, TRK_ASSERT_CODE, -1);
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
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, NULL);
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id == id) return &user[i];
    return NULL;
}

const trk_venue_t *trk_get(uint16_t venue_id)
{
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, NULL);
    const trk_venue_t *u = user_get(venue_id);
    if (u) return u;
    for (uint16_t i = 0; i < trk_bundled_count; i++) if (trk_bundled[i].id == venue_id) return &trk_bundled[i];
    return NULL;
}

int trk_user_add(const trk_venue_t *v)
{
    CORE_ASSERT_RET(v != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, -1);
    if (trk_validate_venue(v) != 0) return -1;
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id == v->id) { user[i] = *v; return 0; }
    if (user_n >= TRK_MAX_USER) return -1;
    user[user_n++] = *v;
    return 0;
}

static int fail(char *err, size_t cap, const char *m) { if (err && cap) { strncpy(err, m, cap - 1); err[cap - 1] = '\0'; } return -1; }

/* Landing zone trk_user_add_json parses a venue JSON directly into, so a ~2.7 KB trk_venue_t never
 * has to pass through a stack temporary (Plan 7 Task 1 DRAM reclaim; this mirrors why trk_from_json
 * itself keeps its jsmn scratch array static, not on-stack): the first not-yet-active entry,
 * user[user_n]. NULL once the table is full. Not reentrant, same as the rest of this module. */
static trk_venue_t *user_slot_for_parse(void)
{
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, NULL);
    return (user_n < TRK_MAX_USER) ? &user[user_n] : NULL;
}

int trk_user_add_json(const char *json, size_t n, uint16_t *venue_id_out, char *err, size_t err_cap)
{
    CORE_ASSERT_RET(json != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, -1);   /* venue_id_out may be NULL: not asserted */
    trk_venue_t *slot = user_slot_for_parse();
    if (slot == NULL) return fail(err, err_cap, "user table full");
    if (trk_from_json(slot, json, n, err, err_cap) != 0) { memset(slot, 0, sizeof *slot); return -1; }
    /* Same-id replace / new-entry append: the existing trk_user_add dedupe path, reused rather than
     * duplicated. slot already IS user[user_n] (the parse landed there directly), so the "new
     * entry" branch inside trk_user_add copies it onto itself (a harmless struct self-assignment)
     * before bumping user_n; the "replace" branch copies it into the matching earlier index. Either
     * way this cannot fail: slot already passed trk_from_json's trk_validate_venue, and
     * user_slot_for_parse() just confirmed user_n < TRK_MAX_USER with no other mutator able to run
     * in between (module is not reentrant) -- checked anyway, never taken. */
    if (trk_user_add(slot) != 0) { memset(slot, 0, sizeof *slot); return fail(err, err_cap, "user table full"); }
    if (venue_id_out != NULL) *venue_id_out = slot->id;
    return 0;
}

uint16_t trk_next_user_id(void)
{
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, TRK_USER_ID_BASE);
    uint16_t id = TRK_USER_ID_BASE;
    for (uint8_t i = 0; i < user_n; i++) {
        if (user[i].id == TRK_SIM_VENUE_ID) continue;   /* F-5: reserved -- never counts toward the next id */
        if (user[i].id >= id) id = (uint16_t)(user[i].id + 1);
    }
    if (id == TRK_SIM_VENUE_ID) id++;                    /* F-5: never hand out the reserved id itself */
    return id;
}

/* See core/trk.h's doc comment (F-5/I-5). */
uint16_t trk_user_id_at(uint8_t index)
{
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, 0);
    if (index >= user_n) return 0;
    return user[index].id;
}

static void consider(const trk_venue_t *v, double lat, double lon, const trk_venue_t **best, double *best_d)
{
    CORE_ASSERT_VOID(v != NULL, TRK_ASSERT_CODE);
    CORE_ASSERT_VOID(best != NULL, TRK_ASSERT_CODE);
    CORE_ASSERT_VOID(best_d != NULL, TRK_ASSERT_CODE);
    const trk_venue_t *u = user_get(v->id);
    if (u && u != v) return;                                  /* bundled entry shadowed by a user entry */
    double d = geo_dist_m(lat, lon, v->lat, v->lon);
    if (d <= (double)v->radius_m && d < *best_d) { *best = v; *best_d = d; }
}

const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out)
{
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, NULL);
    CORE_ASSERT_RET(isfinite(lat), TRK_ASSERT_CODE, NULL);
    CORE_ASSERT_RET(isfinite(lon), TRK_ASSERT_CODE, NULL);
    const trk_venue_t *best = NULL; double best_d = 1e12;
    for (uint8_t i = 0; i < user_n; i++) consider(&user[i], lat, lon, &best, &best_d);
    for (uint16_t i = 0; i < trk_bundled_count; i++) consider(&trk_bundled[i], lat, lon, &best, &best_d);
    if (best && dist_m_out) *dist_m_out = (uint32_t)best_d;
    return best;
}

/* ---- per-venue variable-length record (review fix round 1, #97, T5-R4) ----
 *
 * Small bounds-checked append/consume cursor helpers, shared by trk_user_save_venue (rec_put_*)
 * and trk_user_load_venue (rec_get_*): each advances *off by the field width and fails (false)
 * rather than writing/reading past cap/n, so a short destination buffer or a truncated/corrupt
 * record is caught at the point of the overrun, not after. f64 fields are copied via memcpy of
 * the double's raw bytes (a build-local-representation contract -- this blob was never claimed
 * portable across builds, only across reboots of the same image). */
static bool rec_put_u8(uint8_t *buf, size_t cap, size_t *off, uint8_t v)
{
    if (*off + 1u > cap) return false;
    buf[*off] = v;
    *off += 1u;
    return true;
}
static bool rec_put_u16(uint8_t *buf, size_t cap, size_t *off, uint16_t v)
{
    if (*off + 2u > cap) return false;
    buf[*off] = (uint8_t)v;
    buf[*off + 1u] = (uint8_t)(v >> 8);
    *off += 2u;
    return true;
}
static bool rec_put_u32(uint8_t *buf, size_t cap, size_t *off, uint32_t v)
{
    if (*off + 4u > cap) return false;
    for (int i = 0; i < 4; i++) buf[*off + (size_t)i] = (uint8_t)(v >> (8 * i));
    *off += 4u;
    return true;
}
static bool rec_put_f64(uint8_t *buf, size_t cap, size_t *off, double v)
{
    if (*off + 8u > cap) return false;
    memcpy(buf + *off, &v, 8u);
    *off += 8u;
    return true;
}
static bool rec_put_bytes(uint8_t *buf, size_t cap, size_t *off, const void *src, size_t n)
{
    CORE_ASSERT_RET(src != NULL, TRK_ASSERT_CODE, false);
    if (*off + n > cap) return false;
    memcpy(buf + *off, src, n);
    *off += n;
    return true;
}
static bool rec_get_u8(const uint8_t *buf, size_t n, size_t *off, uint8_t *out)
{
    if (*off + 1u > n) return false;
    *out = buf[*off];
    *off += 1u;
    return true;
}
static bool rec_get_i8(const uint8_t *buf, size_t n, size_t *off, int8_t *out)
{
    uint8_t raw;
    if (!rec_get_u8(buf, n, off, &raw)) return false;
    *out = (int8_t)raw;
    return true;
}
static bool rec_get_u16(const uint8_t *buf, size_t n, size_t *off, uint16_t *out)
{
    if (*off + 2u > n) return false;
    *out = (uint16_t)(buf[*off] | ((uint16_t)buf[*off + 1u] << 8));
    *off += 2u;
    return true;
}
static bool rec_get_u32(const uint8_t *buf, size_t n, size_t *off, uint32_t *out)
{
    if (*off + 4u > n) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)buf[*off + (size_t)i] << (8 * i);
    *out = v;
    *off += 4u;
    return true;
}
static bool rec_get_f64(const uint8_t *buf, size_t n, size_t *off, double *out)
{
    if (*off + 8u > n) return false;
    memcpy(out, buf + *off, 8u);
    *off += 8u;
    return true;
}
static bool rec_get_bytes(const uint8_t *buf, size_t n, size_t *off, void *dst, size_t len)
{
    CORE_ASSERT_RET(dst != NULL, TRK_ASSERT_CODE, false);
    if (*off + len > n) return false;
    memcpy(dst, buf + *off, len);
    *off += len;
    return true;
}

/* One layout's record (id, name, sf, dir_sign, n_sectors, its sectors, length_m -- the field
 * order the header comment documents). Shared by the save and load sides below. */
static bool rec_put_layout(uint8_t *buf, size_t cap, size_t *off, const trk_layout_t *l)
{
    CORE_ASSERT_RET(l != NULL, TRK_ASSERT_CODE, false);
    CORE_ASSERT_RET(l->n_sectors <= LAP_MAX_SECTORS, TRK_ASSERT_CODE, false);
    if (!rec_put_u16(buf, cap, off, l->id)) return false;
    if (!rec_put_bytes(buf, cap, off, l->name, sizeof l->name)) return false;
    if (!rec_put_f64(buf, cap, off, l->sf.p1.lat)) return false;
    if (!rec_put_f64(buf, cap, off, l->sf.p1.lon)) return false;
    if (!rec_put_f64(buf, cap, off, l->sf.p2.lat)) return false;
    if (!rec_put_f64(buf, cap, off, l->sf.p2.lon)) return false;
    if (!rec_put_u8(buf, cap, off, (uint8_t)l->dir_sign)) return false;
    if (!rec_put_u8(buf, cap, off, l->n_sectors)) return false;
    for (uint8_t s = 0; s < l->n_sectors; s++) {
        const trk_line_t *ln = &l->sectors[s];
        if (!rec_put_f64(buf, cap, off, ln->p1.lat)) return false;
        if (!rec_put_f64(buf, cap, off, ln->p1.lon)) return false;
        if (!rec_put_f64(buf, cap, off, ln->p2.lat)) return false;
        if (!rec_put_f64(buf, cap, off, ln->p2.lon)) return false;
    }
    return rec_put_u32(buf, cap, off, l->length_m);
}

static bool rec_get_layout(const uint8_t *buf, size_t n, size_t *off, trk_layout_t *out)
{
    CORE_ASSERT_RET(out != NULL, TRK_ASSERT_CODE, false);
    memset(out, 0, sizeof *out);
    int8_t  dir = 0;
    uint8_t n_sectors = 0;
    if (!rec_get_u16(buf, n, off, &out->id)) return false;
    if (!rec_get_bytes(buf, n, off, out->name, sizeof out->name)) return false;
    out->name[sizeof out->name - 1] = '\0';             /* defensive: trk_validate_venue requires it */
    if (!rec_get_f64(buf, n, off, &out->sf.p1.lat)) return false;
    if (!rec_get_f64(buf, n, off, &out->sf.p1.lon)) return false;
    if (!rec_get_f64(buf, n, off, &out->sf.p2.lat)) return false;
    if (!rec_get_f64(buf, n, off, &out->sf.p2.lon)) return false;
    if (!rec_get_i8(buf, n, off, &dir)) return false;
    if (!rec_get_u8(buf, n, off, &n_sectors)) return false;
    if (n_sectors > LAP_MAX_SECTORS) return false;
    CORE_ASSERT_RET(n_sectors <= LAP_MAX_SECTORS, TRK_ASSERT_CODE, false);   /* postcondition of the check above */
    out->dir_sign  = dir;
    out->n_sectors = n_sectors;
    for (uint8_t s = 0; s < n_sectors; s++) {
        trk_line_t *ln = &out->sectors[s];
        if (!rec_get_f64(buf, n, off, &ln->p1.lat)) return false;
        if (!rec_get_f64(buf, n, off, &ln->p1.lon)) return false;
        if (!rec_get_f64(buf, n, off, &ln->p2.lat)) return false;
        if (!rec_get_f64(buf, n, off, &ln->p2.lon)) return false;
    }
    return rec_get_u32(buf, n, off, &out->length_m);
}

int trk_user_save_venue(uint8_t index, uint8_t *buf, size_t cap, size_t *n_out)
{
    CORE_ASSERT_RET(buf != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(n_out != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, -1);
    if (index >= user_n) return -1;
    const trk_venue_t *v = &user[index];
    CORE_ASSERT_RET(v->n_layouts <= TRK_MAX_LAYOUTS, TRK_ASSERT_CODE, -1);
    size_t off = 0;
    if (!rec_put_u16(buf, cap, &off, v->id)) return -1;
    if (!rec_put_bytes(buf, cap, &off, v->name, sizeof v->name)) return -1;
    if (!rec_put_f64(buf, cap, &off, v->lat)) return -1;
    if (!rec_put_f64(buf, cap, &off, v->lon)) return -1;
    if (!rec_put_u32(buf, cap, &off, v->radius_m)) return -1;
    if (!rec_put_u8(buf, cap, &off, v->flags)) return -1;
    if (!rec_put_u8(buf, cap, &off, v->n_layouts)) return -1;
    for (uint8_t i = 0; i < v->n_layouts; i++) {
        if (!rec_put_layout(buf, cap, &off, &v->layouts[i])) return -1;
    }
    *n_out = off;
    return 0;
}

/* Final review I-1: decodes straight into the module-static landing slot
 * user_slot_for_parse() already hands trk_user_add_json() -- never through an on-stack
 * trk_venue_t (~2.7 KB; this is the exact pattern trk.h:137's rule documents and trk_user_add_json
 * already follows). Power-of-10 rule 1 forbids a goto to a shared cleanup label, so this is the
 * same straight-line `if (ok) ok = step(...)` chain epd_partial_refresh() (display_epaper.c) uses
 * for the same reason: only the FIRST failure survives into `ok`, and a single unconditional
 * cleanup (memset the slot on any failure) runs once at the end instead of being repeated at N
 * early returns. */
int trk_user_load_venue(const uint8_t *buf, size_t n)
{
    CORE_ASSERT_RET(buf != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, -1);
    trk_venue_t *slot = user_slot_for_parse();
    if (slot == NULL) return -1;
    memset(slot, 0, sizeof *slot);

    size_t  off = 0;
    uint8_t n_layouts = 0;
    bool ok = rec_get_u16(buf, n, &off, &slot->id);
    /* F-5 (final review, I-5): a persisted record is never allowed to carry the sim's reserved
     * id -- tracks_save() (logger.c) never writes one, so seeing one here means a corrupt/foreign
     * file, not a legitimate user venue. */
    if (ok && slot->id == TRK_SIM_VENUE_ID) ok = false;
    if (ok) ok = rec_get_bytes(buf, n, &off, slot->name, sizeof slot->name);
    if (ok) slot->name[sizeof slot->name - 1] = '\0';   /* defensive: trk_validate_venue requires it */
    if (ok) ok = rec_get_f64(buf, n, &off, &slot->lat);
    if (ok) ok = rec_get_f64(buf, n, &off, &slot->lon);
    if (ok) ok = rec_get_u32(buf, n, &off, &slot->radius_m);
    if (ok) ok = rec_get_u8(buf, n, &off, &slot->flags);
    if (ok) ok = rec_get_u8(buf, n, &off, &n_layouts);
    if (ok && n_layouts > TRK_MAX_LAYOUTS) ok = false;
    if (ok) CORE_ASSERT_RET(n_layouts <= TRK_MAX_LAYOUTS, TRK_ASSERT_CODE, -1);   /* postcondition: ok implies the check above passed */
    if (ok) {
        slot->n_layouts = n_layouts;
        for (uint8_t i = 0; i < n_layouts && ok; i++) ok = rec_get_layout(buf, n, &off, &slot->layouts[i]);
    }
    if (ok && off != n) ok = false;                     /* no trailing garbage in the record */
    if (ok && trk_validate_venue(slot) != 0) ok = false;
    /* replace-same-id dedupe + the TRK_MAX_USER bound, reused not duplicated */
    if (ok) ok = (trk_user_add(slot) == 0);
    if (!ok) memset(slot, 0, sizeof *slot);
    return ok ? 0 : -1;
}
