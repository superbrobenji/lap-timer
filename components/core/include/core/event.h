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
    EV_FAULT         = 15,  /* arg16 = error code                                 (supervisor) */

    /* #87: ui-only codes. Never passed to emit_event() -- posted straight to g_ui_evt_q with a
     * direct xQueueSend() -- so, unlike every code above, they are never logged to an EVENT
     * record and never streamed to a peer (only emit_event() reaches those sinks). */
    EV_CFG_CHANGED   = 16,  /* ui-only: posted straight to g_ui_evt_q by cmd.c, never via emit_event(), never logged/streamed */
    EV_LAP_RESET     = 17,  /* ui-only: posted straight to g_ui_evt_q by the pipeline on CMD_RESET_ENGINE */
    EV_OTA           = 18,  /* ui-only: posted straight to g_ui_evt_q by ota.c / the supervisor.
                             * flags = phase: OTA_PHASE_RECEIVING/VERIFYING/REBOOTING (core/ui/model.h)
                             * or EV_OTA_ABORTED / EV_OTA_ROLLED_BACK below; arg16 = percent 0..100 */
    EV_CREATE        = 19   /* ui-only: posted straight to g_ui_evt_q by the pipeline (#97, §10.9).
                             * flags = EV_CREATE_* phase below; arg16 = gate index (GATE_SET) or
                             * reason (FAILED, always 0 today) */
};
/* Terminal EV_OTA phases (never rendered as a status line): the transfer failed/was aborted, or
 * the bootloader reverted the previous image (spec §19.4: "UPDATE FAILED, REVERTED" for 3 s). */
#define EV_OTA_ABORTED     3u
#define EV_OTA_ROLLED_BACK 4u

/* EV_CREATE phases (#97, §10.9): CMD_CREATE_BEGIN armed the engine (BEGUN), a short-MODE
 * CMD_MARK_GATE landed (GATE_SET, arg16 = the gate index just set) or was refused (FAILED --
 * arg16 = EV_CREATE_FAIL_* reason below), or CMD_CREATE_CANCEL aborted (CANCELLED). The engine's
 * own finish (the next S/F crossing after every gate is marked) is EV_VENUE_FOUND, not one of
 * these -- ui.c's existing EV_VENUE_FOUND handling closes the NEW TRACK one-shot. */
#define EV_CREATE_BEGUN     0u
#define EV_CREATE_GATE_SET  1u
#define EV_CREATE_FAILED    2u
#define EV_CREATE_CANCELLED 3u

/* EV_CREATE_FAILED's arg16 (review fix round 1, M5): pipeline.c determines the reason from its
 * own s_create_next_gate counter, not from lap_mark_gate()'s plain 0/-1 (the engine exposes no
 * reason code). FULL means every sector is already marked -- the ui leaves create_step untouched
 * rather than clobbering the already-correct "Cross S/F to finish" sub-line with "No fix / not
 * moving" (core/ui/screens_moto.c's newtrack_subline()). "Out of order" never reaches the ui as a
 * distinct reason: the pipeline always marks the gate index it itself expects next (never trusts
 * a command's own arg8), so that lap_mark_gate() refusal is structurally unreachable here. */
#define EV_CREATE_FAIL_NOFIX 0u
#define EV_CREATE_FAIL_FULL  1u
#endif
