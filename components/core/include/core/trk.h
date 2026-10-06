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
 * Writers: trk_init()/trk_user_load() at boot, before the ui holds any id (today, since #97,
 * trk_user_load() itself runs once at boot on the logger task, as part of the pipeline's own
 * synchronous LOGGER_LOAD_TRACKS request -- see trk.c for why that still needs no lock); and
 * trk_user_add() on the pipeline task, which always completes before the event that surfaces its
 * id is emitted -- the FreeRTOS event queue (g_ui_evt_q) send/receive is the barrier that makes
 * the write visible to the ui task without a lock. One invariant the API itself does not enforce:
 * no writer may rewrite or remove an id the ui may already hold (trk.c has the full reasoning and
 * every live call site's compliance). */
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
 * version, count, size or CRC and any venue failing trk_validate_venue, leaving the store empty. */
int                trk_user_load(const uint8_t *blob, size_t n);
int                trk_user_save(uint8_t *blob, size_t cap, size_t *n_out);
int                trk_from_json(trk_venue_t *out, const char *json, size_t n, char *err, size_t err_cap);
int                trk_to_json(const trk_venue_t *v, char *out, size_t cap);
#endif
