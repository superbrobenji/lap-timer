#ifndef CORE_EVENT_H
#define CORE_EVENT_H
#include <stdint.h>

/* Runtime engine event (spec §4.5). Pure C11, no IDF, no allocation.
 *
 * `type` carries an EV_* code. These codes are STABLE integers because they are written verbatim
 * into the EVENT log record (§12.3, `ses_event_t.code`); never renumber them. `flags`/`arg16`/
 * `arg32`/`arg32b` are per-event payloads (see the §4.5 table); `gps_us`/`mono_us` timestamp the
 * event. A producer fills only the fields its event defines and leaves the rest zero. */

typedef struct {
    uint8_t  type;      /* EV_* */
    uint8_t  flags;     /* event-specific (e.g. lap flags on EV_LAP_COMPLETE) */
    uint16_t arg16;     /* event-specific (e.g. venue id, layout id, lap no) */
    int64_t  gps_us;
    int64_t  mono_us;
    uint32_t arg32;     /* event-specific (e.g. lap ms, split ms) */
    uint32_t arg32b;    /* event-specific (e.g. delta ms, speed) */
} event_t;

/* Stable EV_* codes (§4.5). Explicit values: they are logged and must never change. */
enum {
    EV_NONE          = 0,
    EV_VENUE_FOUND   = 1,   /* arg16 = venue id                                   (lapengine) */
    EV_LAYOUT_LOCKED = 2,   /* arg16 = layout id                                  (lapengine, 2.5) */
    EV_ARMED         = 3,   /* —                                                  (lapengine/dragengine) */
    EV_SECTOR        = 4,   /* arg16 = sector idx, arg32 = split ms, arg32b delta (lapengine, 2.5).
                              * arg32b is 0 both for a genuine zero delta and for "no best lap yet"
                              * (§10.7 else-no-delta case) — a consumer cannot tell the two apart. */
    EV_LAP_COMPLETE  = 5,   /* arg16 = lap no, arg32 = lap ms, flags = lap flags  (lapengine) */
    EV_FIX_LOST      = 6,   /* —                                                  (pipeline) */
    EV_FIX_OK        = 7,   /* —                                                  (pipeline) */
    EV_DRAG_ARMED    = 8,   /* —                                                  (dragengine) */
    EV_DRAG_LAUNCH   = 9,   /* —                                                  (dragengine) */
    EV_DRAG_GATE     = 10,  /* arg16 = gate id, arg32 = ms, arg32b = speed cm/s   (dragengine) */
    EV_DRAG_DONE     = 11,  /* arg16 = run no                                     (dragengine) */
    EV_MOTION        = 12,  /* —                                                  (pipeline) */
    EV_STILL         = 13,  /* —                                                  (pipeline) */
    EV_CALIB_DONE    = 14,  /* arg16 = stage                                      (fusion) */
    EV_FAULT         = 15   /* arg16 = error code                                 (supervisor) */
};
#endif
