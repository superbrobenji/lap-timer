#ifndef CORE_CFG_H
#define CORE_CFG_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CFG_VERSION 2
enum { CFG_UNITS_KMH = 0, CFG_UNITS_MPH = 1 };
enum { CFG_DIST_M = 0, CFG_DIST_FT = 1 };
enum { CFG_MODE_LAP = 0, CFG_MODE_DRAG = 1 };
#define CFG_MAX_DEFAULT_LAYOUTS 8
#define CFG_MAX_BENCHES 4

typedef struct {
    uint8_t version;
    uint8_t units;                 /* CFG_UNITS_* */
    uint8_t mode;                  /* CFG_MODE_* */
    struct {
        uint16_t min_lap_s, max_lap_s, gate_rearm_m;
        uint8_t  pit_speed_kmh, pit_time_s;
        uint8_t  n_default_layout;
        struct { uint16_t venue, layout; } default_layout[CFG_MAX_DEFAULT_LAYOUTS];
    } lap;
    struct {
        uint16_t benches_kmh[CFG_MAX_BENCHES], benches_mph[CFG_MAX_BENCHES];
        uint8_t  n_kmh, n_mph;
        bool     rollout;
        uint8_t  launch_g_e2;
    } drag;
    struct { uint16_t pit_after_s, park_after_s, shutdown_mv, conn_idle_s; } power;
    struct { bool live_clock; uint8_t full_every; uint8_t rotation; bool invert; } display;
    struct { uint16_t adc_mv[2], true_mv[2]; } battery;   /* two-point calibration; identity when adc==true */
    struct { char name[16]; uint16_t adv_s; } ble;
    struct { uint8_t fused_hz; } log;
    struct { uint8_t dyn_model, rate_hz; } gps;
    struct { uint8_t mot_thr, mot_dur_ms; } imu;
    /* #96, v2: deliberately APPENDED, not grouped next to `units` above -- cfg_t is the on-flash
     * blob payload (core/cfg_blob.h) and blob_unwrap() repopulates every field by a single raw
     * memcpy at fixed offsets. Confirmed by sizeof/offsetof probe: today's cfg_t already carries
     * one byte of compiler padding (alignment of `lap`'s first uint16_t forces the 3-byte
     * version/units/mode header out to 4), so inserting a field ANYWHERE before `mode` does not
     * grow sizeof(cfg_t) at all -- it just eats that padding byte -- which means lt_cfg_load's
     * `sz != sizeof(buf)` guard would NOT catch a real v1-saved blob, and cfg_blob_unwrap's
     * version-mismatch migrate path WOULD run on it. Inserted there, the raw memcpy would then
     * silently shift every byte from the insertion point through `mode` by one: `mode`'s stored
     * value lands in the new field instead, and the always-zero padding byte lands in `mode` --
     * i.e. every v1 device in DRAG mode would silently come back as LAP after this update, with
     * cfg_validate() reporting zero corrections (mode=0 is in-range). Appending here instead means
     * v1's byte image is an exact prefix of v2's (same probe: offsetof(imu) unchanged, sizeof
     * unchanged -- this field lands on the struct's own *trailing* alignment pad) -- no other
     * field's offset moves, so nothing upstream of it can be corrupted by the migrate path.
     * Review finding m6 (Task 3 fix round 1): the trailing pad byte's CONTENT is not why this is
     * safe -- C leaves padding content unspecified, it only reads as zero here because
     * cfg_defaults()'s memset put it there. The actual reason the stored byte is irrelevant is
     * that cfg_migrate() (cfg.c) writes dist_units unconditionally on every v1->v2 migrate,
     * regardless of what was sitting in that byte. See the Task 3 report's blob-migration
     * analysis, and the _Static_assert just below for the layout guarantee itself. */
    uint8_t dist_units;            /* CFG_DIST_*: DIST gate labels only; 1/8 and 1/4 mile keep their names */
} cfg_t;

/* Review finding I1 (Task 3 fix round 1): the comment above is an argument; this is the
 * compile-time proof of the one fact it rests on. Checked by every host build AND the ESP32
 * selftest app build (xtensa ABI, not just the host probe this was originally verified with). If
 * a future field ever needs to grow cfg_t again, keep appending after this one -- anything that
 * moves dist_units off the last byte breaks the v1->v2 migrate path's byte-for-byte-prefix
 * guarantee and this assert catches it at build time instead of silently on a real device. */
_Static_assert(offsetof(cfg_t, dist_units) == sizeof(cfg_t) - 1u,
               "dist_units must be cfg_t's LAST byte: a stored v1 blob's payload has to stay a "
               "byte-for-byte prefix of v2's for cfg_blob_unwrap's raw memcpy migrate (#96)");

/* Hardware-profile defaults. The app calls cfg_apply_profile() at boot right after cfg_defaults()
 * and before loading the NVS blob, with values from build_config.h and the MAC-derived BLE name. */
typedef struct { bool display_live_clock; uint8_t log_fused_hz; const char *ble_name; } cfg_profile_t;
int cfg_apply_profile(cfg_t *c, const cfg_profile_t *p);      /* 0 ok / -1 bad name length */

int cfg_defaults(cfg_t *c);
int cfg_validate(cfg_t *c);                       /* clamps; returns number of corrected fields */
int cfg_from_json(cfg_t *c, const char *json, size_t n, char *err, size_t err_cap);   /* merge; "version" is ignored (owned by firmware); arrays longer than capacity are rejected; 0 ok / -1 error with err */
int cfg_to_json(const cfg_t *c, char *out, size_t cap);                             /* bytes written or -1 */
int cfg_migrate(cfg_t *c, uint8_t from_version);                                    /* 0 ok / -1 unknown version; v1->v2 sets dist_units = CFG_DIST_M (#96) */
bool cfg_migrate_supported(uint8_t from_version);      /* true for exactly the versions cfg_migrate() accepts */
#endif
