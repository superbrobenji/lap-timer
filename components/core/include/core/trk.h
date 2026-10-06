#ifndef CORE_TRK_H
#define CORE_TRK_H
#include <stdint.h>
#include <stddef.h>
#include "core/consts.h"
#include "core/types.h"

#define TRK_F_UNVERIFIED 0x01
#define TRK_USER_ID_BASE 1000

typedef struct { double lat, lon; } trk_pt_t;
typedef struct { trk_pt_t p1, p2; } trk_line_t;      /* p1 = left end, p2 = right end in driving direction */
typedef struct {
    uint16_t   id;
    char       name[24];
    trk_line_t sf;
    int8_t     dir_sign;                             /* +1 / -1 (§6.4) */
    uint8_t    n_sectors;                            /* sector gates, excluding S/F */
    trk_line_t sectors[LAP_MAX_SECTORS];
    uint32_t   length_m;
} trk_layout_t;
typedef struct {
    uint16_t     id;                                 /* bundled 1..999, user 1000+ */
    char         name[32];
    double       lat, lon;
    uint32_t     radius_m;
    uint8_t      flags;                              /* TRK_F_* */
    uint8_t      n_layouts;
    trk_layout_t layouts[TRK_MAX_LAYOUTS];
} trk_venue_t;

extern const trk_venue_t trk_bundled[];
extern const uint16_t    trk_bundled_count;

/* The user store is module-static and not protected by a lock.
 *
 * Readers: the pipeline task (core 1) and -- since #98 -- the ui task (core 0), both only through
 * trk_get(); the ui calls it only after receiving the EV_VENUE_FOUND/EV_LAYOUT_LOCKED event that
 * names the id.
 *
 * Writers: trk_init() runs once at boot, in main/app_main.c, BEFORE logger_start() creates the
 * logger task (review fix round 1, T5-R2/#97) -- not inside pipeline_init() any more, and not
 * paired with a cross-task request. trk_user_load_venue() (looped by the logger's tracks_load())
 * then runs, also at boot, on the newly-created LOGGER task, entirely inside logger_task()'s own
 * init section, before that task's first blocking wait -- which is also before app_main() (lower
 * priority, preempted the instant the logger task is created) can reach pipeline_start() and
 * create the pipeline task. So every boot-time writer (trk_init(), then every
 * trk_user_load_venue()) runs strictly before the pipeline task -- the only other writer -- even
 * exists, with no lock and no queue needed: there is nothing yet for it to race. trk_user_add()
 * (direct, or via trk_user_add_json() for the CFG_GPS_SIM venue) is on the pipeline task, after
 * boot, and always completes before the event that surfaces its id is emitted -- the FreeRTOS
 * event queue (g_ui_evt_q) send/receive is the barrier that makes the write visible to the ui
 * task without a lock. One invariant the API itself does not enforce: no writer may rewrite or
 * remove an id the ui may already hold (trk.c has the full reasoning and every live call site's
 * compliance). */
void               trk_init(void);                                   /* clears the user store */
int                trk_validate_venue(const trk_venue_t *v);         /* 0 ok / -1 structurally invalid */
const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out);   /* within radius; user beats bundled on id clash */
const trk_venue_t *trk_get(uint16_t venue_id);
int                trk_user_add(const trk_venue_t *v);              /* replaces same id; -1 if full or invalid */
/* Parses `json` straight into a free (or same-id) user slot -- never through a stack-sized
 * trk_venue_t temporary (Plan 7 Task 1). 0 ok (*venue_id_out, if non-NULL, gets the parsed venue's
 * id -- trk_get(*venue_id_out) then resolves it); -1 on parse error or a full table (`err` filled,
 * see trk_from_json). */
int                trk_user_add_json(const char *json, size_t n, uint16_t *venue_id_out, char *err, size_t err_cap);
int                trk_user_count(void);
uint16_t           trk_next_user_id(void);
/* Blob v2: u8 version(2) | u8 count | trk_venue_t[count] | u16 crc16 LE. Load rejects a wrong
 * version, count, size or CRC and any venue failing trk_validate_venue, leaving the store empty.
 * Whole-table, fixed-stride (every venue costs sizeof(trk_venue_t) regardless of its actual
 * layout/sector counts) -- kept for its own tested contract (test_trk.c) but, since the review
 * fix round 1 (#97, T5-R4), no longer the logger's persistence format: at TRK_MAX_USER == 4 this
 * blob (up to ~11 KB) does not fit the logger's 3840 B scratch batch. See trk_user_save_venue/
 * trk_user_load_venue below for the format that does. */
int                trk_user_load(const uint8_t *blob, size_t n);
int                trk_user_save(uint8_t *blob, size_t cap, size_t *n_out);

/* Per-venue variable-length record (review fix round 1, #97, T5-R4): the logger's actual
 * persistence format (components/app/logger/logger.c tracks_save/tracks_load), streamed one
 * record at a time through its own small scratch buffer instead of one whole-table blob. Payload
 * (exactly what trk_user_save_venue fills into buf / *n_out, and what trk_user_load_venue
 * consumes -- the logger's own file framing, a u16 length prefix + u16 crc16 per record, wraps
 * this but is not part of it):
 *   u16 id | char name[32] | f64 lat | f64 lon | u32 radius_m | u8 flags | u8 n_layouts |
 *   n_layouts x { u16 id | char name[24] | 4 x f64 sf | i8 dir_sign | u8 n_sectors |
 *                 n_sectors x (4 x f64 sector line) | u32 length_m }
 * Only the venue's ACTUAL n_layouts layouts (and each layout's actual n_sectors sectors) are
 * written -- unlike trk_user_save's fixed-stride whole-table blob, an on-device-created venue
 * (n_layouts == 2) costs well under a third of TRK_USER_REC_MAX.
 *
 * trk_user_save_venue: index is 0..trk_user_count()-1 (the live in-RAM order, NOT a venue id);
 * 0 ok (*n_out = bytes written), -1 if index is out of range or buf is too small (cap < need).
 * trk_user_load_venue: validates (trk_validate_venue) then calls trk_user_add() -- so the
 * replace-same-id dedupe and the TRK_MAX_USER bound are reused, not duplicated. 0 ok, -1 on a
 * malformed/short record, an invalid venue, or a full table (trk_user_add's own -1). */
int                trk_user_save_venue(uint8_t index, uint8_t *buf, size_t cap, size_t *n_out);
int                trk_user_load_venue(const uint8_t *buf, size_t n);
/* Worst-case trk_user_save_venue() payload size (every slot full: TRK_MAX_LAYOUTS layouts, each
 * with LAP_MAX_SECTORS sectors) -- the bound the logger's own BATCH_CAP must stay above (a
 * _Static_assert there proves it). Computed from the same field widths the payload comment above
 * lists, not a hand-typed number, so a future consts.h change cannot silently invalidate it. */
#define TRK_USER_VENUE_HDR_LEN  (2 + 32 + 8 + 8 + 4 + 1 + 1)          /* id,name,lat,lon,radius_m,flags,n_layouts */
#define TRK_USER_LAYOUT_HDR_LEN (2 + 24 + 4 * 8 + 1 + 1 + 4)          /* id,name,sf,dir_sign,n_sectors,length_m */
#define TRK_USER_SECTOR_LEN     (4 * 8)                               /* one trk_line_t = 4 x f64 */
#define TRK_USER_REC_MAX \
    (TRK_USER_VENUE_HDR_LEN + \
     TRK_MAX_LAYOUTS * (TRK_USER_LAYOUT_HDR_LEN + LAP_MAX_SECTORS * TRK_USER_SECTOR_LEN))

int                trk_from_json(trk_venue_t *out, const char *json, size_t n, char *err, size_t err_cap);
int                trk_to_json(const trk_venue_t *v, char *out, size_t cap);
#endif
