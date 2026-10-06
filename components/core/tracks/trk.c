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
 * Writers: trk_init() runs ONCE, at boot, on whichever task calls app_main() (main/app_main.c's
 * boot_subsystems(), right before logger_start()) -- before the logger task even exists, so
 * trivially before any id has been surfaced to any reader. trk_user_load_venue() (looped by the
 * logger's tracks_load()) then runs, also at boot, but -- since #97, §10.9, review fix round 1
 * (T5-R2) -- directly on the newly-created LOGGER task, inside logger_task()'s own init, before
 * that task's first blocking wait. No request, no queue, no lock needed: app_main() is lower
 * priority than the logger task it just created, so the instant xTaskCreateStaticPinnedToCore()
 * (logger_start()) returns control to the scheduler, the logger task preempts it and runs to its
 * first blocking wait -- well before app_main() can resume and reach pipeline_start() a few lines
 * later, which is the only thing that could create the pipeline task, the ONLY other writer of
 * this table. So every boot-time writer (trk_init(), then every trk_user_load_venue()) completes
 * before the pipeline task -- the only other writer -- even exists; there is nothing yet for it to
 * race. trk_user_add() -- invoked directly by lap.c's finalize_create() on a CREATE-mode S/F
 * crossing, and indirectly via trk_user_add_json() for the CFG_GPS_SIM venue registration at
 * pipeline_init() -- is on the pipeline task (core 1), after boot, and always completes before the
 * event that announces its id: each call site's emit(..., EV_VENUE_FOUND, ...) is sequential code
 * a few lines later in the same function, same task.
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

#define BLOB_VERSION 2

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
/* Shared by both entry points that can install a venue (trk_from_json, via the final
 * trk_validate_venue() call, and trk_user_load()/trk_user_add(), via this function directly), so
 * the two never disagree about what a valid gate line is. */
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
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id >= id) id = (uint16_t)(user[i].id + 1);
    return id;
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

/* Blob v2: u8 version=2 | u8 count | trk_venue_t[count] | u16 crc16 (LE) over every preceding byte.
 * The struct is copied raw, so the blob is only valid for this build; the CRC catches NVS bit rot
 * and every venue is re-validated before it reaches the store. Any failure leaves the store empty. */
int trk_user_load(const uint8_t *blob, size_t n)
{
    CORE_ASSERT_RET(blob != NULL, TRK_ASSERT_CODE, -1);
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

/* Copies `len` bytes from `src` to `dst + off`. A thin, type-erased wrapper around memcpy so each
 * call site below can express "write this field at its offsetof" without a logic-bearing macro;
 * the offset/pointer/length are computed at the call site (offsetof(T, f), &(src)->f, sizeof
 * (src)->f), so this helper needs no knowledge of the surrounding struct types. */
static inline void put_field(uint8_t *dst, size_t off, const void *src, size_t len)
{
    memcpy(dst + off, src, len);
}

/* Field-by-field copy into an already-zeroed destination: struct assignment (or a raw memcpy of the
 * whole struct) also copies the source's compiler-inserted padding bytes verbatim, which the
 * language never promises are zero, so two structurally identical venues could otherwise CRC
 * differently (§10.1's blob is declared build-specific but should still be deterministic within one
 * build). dst points directly at the destination blob bytes (uint8_t *, possibly unaligned), so each
 * field is written with memcpy at its offsetof rather than through a typed pointer; every named field
 * is written explicitly, and nothing else touches dst, so the gaps between fields stay at the memset
 * zero. */
static void canon_venue(uint8_t *dst, const trk_venue_t *src)
{
    CORE_ASSERT_VOID(dst != NULL, TRK_ASSERT_CODE);
    CORE_ASSERT_VOID(src != NULL, TRK_ASSERT_CODE);
    CORE_ASSERT_VOID(src->n_layouts <= TRK_MAX_LAYOUTS, TRK_ASSERT_CODE); /* every stored venue was already trk_validate_venue()-checked */
    memset(dst, 0, sizeof *src);
    put_field(dst, offsetof(trk_venue_t, id), &src->id, sizeof src->id);
    put_field(dst, offsetof(trk_venue_t, name), &src->name, sizeof src->name);
    put_field(dst, offsetof(trk_venue_t, lat), &src->lat, sizeof src->lat);
    put_field(dst, offsetof(trk_venue_t, lon), &src->lon, sizeof src->lon);
    put_field(dst, offsetof(trk_venue_t, radius_m), &src->radius_m, sizeof src->radius_m);
    put_field(dst, offsetof(trk_venue_t, flags), &src->flags, sizeof src->flags);
    put_field(dst, offsetof(trk_venue_t, n_layouts), &src->n_layouts, sizeof src->n_layouts);
    /* Only the active layouts/sectors (src has already passed trk_validate_venue, so n_layouts and
     * every n_sectors are in range) are copied; slots beyond them are left at the memset zero rather
     * than carrying through whatever unused array content src happened to hold. */
    for (uint8_t i = 0; i < src->n_layouts && i < TRK_MAX_LAYOUTS; i++) {
        const trk_layout_t *sl = &src->layouts[i];
        uint8_t *ld = dst + offsetof(trk_venue_t, layouts) + (size_t)i * sizeof(trk_layout_t);
        put_field(ld, offsetof(trk_layout_t, id), &sl->id, sizeof sl->id);
        put_field(ld, offsetof(trk_layout_t, name), &sl->name, sizeof sl->name);
        put_field(ld, offsetof(trk_layout_t, sf), &sl->sf, sizeof sl->sf);   /* trk_line_t is four packed doubles: no internal padding */
        put_field(ld, offsetof(trk_layout_t, dir_sign), &sl->dir_sign, sizeof sl->dir_sign);
        put_field(ld, offsetof(trk_layout_t, n_sectors), &sl->n_sectors, sizeof sl->n_sectors);
        CORE_ASSERT_VOID(sl->n_sectors <= LAP_MAX_SECTORS, TRK_ASSERT_CODE); /* the sectors[] array's own bound */
        for (uint8_t s = 0; s < sl->n_sectors && s < LAP_MAX_SECTORS; s++) {
            uint8_t *sd = ld + offsetof(trk_layout_t, sectors) + (size_t)s * sizeof(trk_line_t);
            memcpy(sd, &sl->sectors[s], sizeof sl->sectors[s]);
        }
        put_field(ld, offsetof(trk_layout_t, length_m), &sl->length_m, sizeof sl->length_m);
    }
}

int trk_user_save(uint8_t *blob, size_t cap, size_t *n_out)
{
    CORE_ASSERT_RET(blob != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(n_out != NULL, TRK_ASSERT_CODE, -1);
    CORE_ASSERT_RET(user_n <= TRK_MAX_USER, TRK_ASSERT_CODE, -1);
    size_t need = 2 + (size_t)user_n * sizeof(trk_venue_t) + 2;
    CORE_ASSERT_RET(need >= 4, TRK_ASSERT_CODE, -1); /* version + count + crc16, even with zero venues */
    if (cap < need) return -1;
    blob[0] = BLOB_VERSION; blob[1] = user_n;
    for (uint8_t i = 0; i < user_n; i++) canon_venue(blob + 2 + (size_t)i * sizeof(trk_venue_t), &user[i]);
    uint16_t crc = ses_crc16(blob, need - 2);
    blob[need - 2] = (uint8_t)crc; blob[need - 1] = (uint8_t)(crc >> 8);
    *n_out = need;
    return 0;
}

/* ---- per-venue variable-length record (review fix round 1, #97, T5-R4) ----
 *
 * Small bounds-checked append/consume cursor helpers, shared by trk_user_save_venue (rec_put_*)
 * and trk_user_load_venue (rec_get_*): each advances *off by the field width and fails (false)
 * rather than writing/reading past cap/n, so a short destination buffer or a truncated/corrupt
 * record is caught at the point of the overrun, not after. f64 fields are copied via memcpy of
 * the double's raw bytes (same build-local-representation contract trk_user_save's canon_venue
 * already relies on -- this blob was never claimed portable across builds, only across reboots of
 * the same image). */
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

int trk_user_load_venue(const uint8_t *buf, size_t n)
{
    CORE_ASSERT_RET(buf != NULL, TRK_ASSERT_CODE, -1);
    trk_venue_t v;
    memset(&v, 0, sizeof v);
    size_t  off = 0;
    uint8_t n_layouts = 0;
    if (!rec_get_u16(buf, n, &off, &v.id)) return -1;
    if (!rec_get_bytes(buf, n, &off, v.name, sizeof v.name)) return -1;
    v.name[sizeof v.name - 1] = '\0';                   /* defensive: trk_validate_venue requires it */
    if (!rec_get_f64(buf, n, &off, &v.lat)) return -1;
    if (!rec_get_f64(buf, n, &off, &v.lon)) return -1;
    if (!rec_get_u32(buf, n, &off, &v.radius_m)) return -1;
    if (!rec_get_u8(buf, n, &off, &v.flags)) return -1;
    if (!rec_get_u8(buf, n, &off, &n_layouts)) return -1;
    if (n_layouts > TRK_MAX_LAYOUTS) return -1;
    CORE_ASSERT_RET(n_layouts <= TRK_MAX_LAYOUTS, TRK_ASSERT_CODE, -1);   /* postcondition of the check above */
    v.n_layouts = n_layouts;
    for (uint8_t i = 0; i < n_layouts; i++) {
        if (!rec_get_layout(buf, n, &off, &v.layouts[i])) return -1;
    }
    if (off != n) return -1;                            /* no trailing garbage in the record */
    if (trk_validate_venue(&v) != 0) return -1;
    return trk_user_add(&v);   /* replace-same-id dedupe + the TRK_MAX_USER bound, reused not duplicated */
}
