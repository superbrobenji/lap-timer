/* app/sup_policy.h -- the pure, host-testable decisions behind the §17.2 supervisor loop and the
 * §19.5 OTA preconditions (audit fix wave 2026-10-13, rulings R1/R2/R6/R9).
 *
 * IDF-free on purpose (stdint.h/stdbool.h + core/core.h via app/lt_assert.h + app/lt_consts.h),
 * exactly like app/lt_proto.h: this TU compiles into the firmware (components/app globs it), into
 * the host harness (test/test_sup_policy.c) and onto the Xtensa toolchain
 * (test_apps/core_selftest), so every threshold and every precondition ordering rule is proven by
 * a real test instead of by a bench observation that cannot be scripted. The IDF-bound callers
 * (supervisor/sup.c, ota/ota.c) gather inputs and perform the side effects a decision asks for;
 * the policy they keep is hardware-bound (sup.c's validation health predicate and once-per-boot
 * stack latch, ota.c's CFG_CHRG battery skip and early hwid reject). No statics live here -- each decision is a function of
 * its arguments plus the caller's own state struct (sup_stall_t), so the same function can be
 * driven for HB_COUNT tasks without cross-talk. */
#ifndef APP_SUP_POLICY_H
#define APP_SUP_POLICY_H

#include <stdbool.h>
#include <stdint.h>

enum { SUP_STALL_LOG = 0x1u, SUP_STALL_RESTART = 0x2u };
typedef struct { uint32_t last_hb; uint32_t stalled_s; bool msg_logged; } sup_stall_t;
uint8_t sup_stall_step(sup_stall_t *st, uint32_t cur_hb, uint32_t stall_s, uint32_t period_s,
                       bool restartable);
enum { SUP_HEAP_NONE = 0u, SUP_HEAP_ENTER = 1u, SUP_HEAP_CLEAR = 2u };
uint8_t sup_heap_eval(uint32_t free_b, bool flagged);
bool    sup_stack_low(uint32_t high_water_b);
uint8_t sup_flag_next_change(uint32_t prev, uint32_t cur, uint8_t from);
enum { OTA_PRE_OK = 0u, OTA_PRE_REFUSE = 1u, OTA_PRE_CLOSE_SESSION = 2u, OTA_PRE_CLEAR_STALE = 3u };
typedef struct {
    uint32_t still_ms;             /* pipeline_still_for_ms() */
    uint32_t size;                 /* declared image size from the OTA_BEGIN payload */
    bool     in_flight;            /* an OTA receive is already open */
    bool     session_open;         /* a logger session is still open (true until a close acks) */
    bool     batt_ok;              /* §19.5 battery/charger gate passed */
    bool     flag_pending;         /* lt_ota_pending_get() */
    bool     slot_pending_verify;  /* the RUNNING slot is ESP_OTA_IMG_PENDING_VERIFY */
} ota_pre_t;
/* Why a push was refused: the errlog arg of E_OTA_PRECOND (§19.5). The evaluator reports the first
 * five; ota.c adds the two refusals that are not pure decisions (short payload, session close not
 * acked). OTA_WHY_NONE means "not refused". */
enum { OTA_WHY_NONE = 0u, OTA_WHY_IN_FLIGHT = 1u, OTA_WHY_SIZE = 2u, OTA_WHY_STILL = 3u,
       OTA_WHY_BATT = 4u, OTA_WHY_SLOT_PENDING = 5u, OTA_WHY_LEN = 6u, OTA_WHY_CLOSE = 7u };
/* `why` (may be NULL; the caller initialises it) receives the refusing OTA_WHY_* when the result is
 * OTA_PRE_REFUSE, and OTA_WHY_NONE otherwise. */
uint8_t ota_precond_eval(const ota_pre_t *in, uint8_t *why);
bool    ota_recv_stale(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_s);

#endif /* APP_SUP_POLICY_H */
