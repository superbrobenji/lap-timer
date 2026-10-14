/* ota.c -- OTA receive-side state machine (spec §19.4), Plan 5 sub-project A, session 5.4.
 *
 * Replaces the 5.1 weak stub with the real esp_ota state machine: OTA_BEGIN parses the header,
 * checks the §19.5 preconditions and opens the next slot; OTA_DATA streams the signed image with a
 * running SHA-256 and, once the first 4 KB are in, verifies the target's project_name + hwid
 * against the running image (§19.3); OTA_END checks the SHA, runs esp_ota_end (ECDSA signature
 * verify), sets the boot slot, persists ota_pending, and arms a reboot the supervisor performs
 * (§19.4). ota_abort leaves the running image untouched. No secure boot -- signed images only.
 *
 * Power of 10: bounded, no dynamic memory, no new function pointer, >=2 assertions per >20-line
 * function on the state-machine invariants (slot handle non-zero, received bytes within the
 * declared size). Client-supplied fields (offset, sizes, sha, hwid) are UNTRUSTED and get graceful
 * E_OTA_* returns, not assertions. The hwid symbol below is placed right after esp_app_desc so it
 * lands at image offset 0x120 (§19.3); `used` keeps it against --gc-sections. */
#include "app/ota.h"

#include "app/lt_assert.h"
#include "app/lt_consts.h"
#include "app/lt_err.h"
#include "app/lt_ipc.h"             /* g_ui_evt_q (ota_ui_post, ui-only EV_OTA) */
#include "app/lt_nvs.h"
#include "app/lt_sup.h"
#include "app/pipeline.h"
#include "app/sup_policy.h"

#include "build_config.h"          /* CFG_HWID */

#include "core/event.h"            /* EV_OTA, EV_OTA_ABORTED */
#include "core/ses.h"              /* SES_END_RESTART */
#include "core/ui/model.h"         /* OTA_PHASE_RECEIVING/VERIFYING/REBOOTING */

#include "esp_app_desc.h"          /* esp_app_get_description, esp_app_desc_t */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hal/board.h"             /* board_charger_present, board_battery_read_mv */

#include "mbedtls/sha256.h"

#include <string.h>

static const char *TAG = "ota";

#define OTA_ASSERT_CODE 0x0D10

#define OTA_HWID_LEN 24

/* §19.3 hwid custom descriptor: 24 bytes placed immediately after the 256 B esp_app_desc_t so it
 * sits at image offset 0x120 in the running image and every future image. `used` keeps the linker
 * from garbage-collecting it. */
static const char __attribute__((section(".rodata_custom_desc"), used)) hwid[OTA_HWID_LEN] = CFG_HWID;

/* OTA_BEGIN payload layout (§19.4): size u32 LE | sha[32] | ver[16] | hwid[24] = 76 B. */
#define OTA_BEGIN_LEN 76
#define OTA_OFF_SIZE  0
#define OTA_OFF_SHA   4
#define OTA_OFF_HWID  52
#define OTA_SHA_LEN   32

/* Image layout (§19.3): esp_app_desc_t at 0x20, hwid custom desc at 0x120; read once the first
 * 4 KB of the target are written. */
#define IMG_DESC_OFF      0x20
#define IMG_HWID_OFF      0x120
#define OTA_HWID_CHECK_AT 4096

/* §19.3 is an image-LAYOUT contract, so assert both halves of it at build time and prove the
 * running half once at boot. There was no check of either kind: a changed esp_app_desc_t size
 * would silently move hwid off 0x120 and make every ota_verify_hwid compare the wrong bytes. */
_Static_assert(sizeof(esp_app_desc_t) == 256, "§19.3: hwid lands at image offset 0x120 only if esp_app_desc_t is 256 B");
_Static_assert(IMG_HWID_OFF == IMG_DESC_OFF + 256, "§19.3: hwid offset must follow the app descriptor");

typedef enum { OTA_IDLE = 0, OTA_RECV, OTA_REBOOT, OTA_VERIFY } ota_st_t;   /* VERIFY: ota_end owns the machine */

static volatile ota_st_t      s_state;             /* volatile: read by the supervisor task in ota_reboot_due */
static esp_ota_handle_t       s_handle;
static const esp_partition_t *s_target;
static uint32_t               s_size;                /* declared image size (from OTA_BEGIN) */
static uint32_t               s_recv;                /* bytes written == next expected offset */
static uint8_t                s_sha_want[OTA_SHA_LEN];
static mbedtls_sha256_context s_sha;
static bool                   s_hwid_ok;             /* the §19.3 target check has passed */
static int64_t                s_reboot_at_us = INT64_MAX;   /* armed by ota_end; INT64_MAX = not due (reboot-arm race) */
static uint8_t s_ui_pct_step;          /* last pct/5 step posted to the ui (throttle, ota_ui_post) */
static volatile uint32_t s_last_data_ms;  /* §19.6: esp_timer ms of the last accepted OTA_DATA.
                                           * A single u32 word, so the console/cmd writer and the
                                           * supervisor reader need no lock (an int64 on 32-bit
                                           * Xtensa would not be a naturally-atomic load/store). */

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Tell the ui (ui-only EV_OTA, never emit_event(): not logged, not streamed -- same contract as
 * EV_CFG_CHANGED). Non-blocking: a full ui queue drops the update, the next one catches up. */
static void ota_ui_post(uint8_t phase, uint8_t pct)
{
    LT_ASSERT_VOID(pct <= 100u, OTA_ASSERT_CODE);
    if (g_ui_evt_q == NULL) return;    /* ui not started (recovery mode) */
    event_t ev = { .type = EV_OTA, .flags = phase, .arg16 = pct, .mono_us = esp_timer_get_time() };
    /* M2 (fix round 1): every caller here (ota_begin/data/end/fail/abort) runs inside an `ota recv`
     * or a framed CMD_OTA_* transfer, both of which quiet logging to ESP_LOG_NONE for the duration
     * (export_serial.c, ruling R9 -- it was ESP_LOG_ERROR, which still let the assert sink, the
     * supervisor's stall line and storage errors inject bytes into the OTA token/frame stream on
     * the same UART, app:M12/drv:M6) -- so WARN never prints here. A drop is therefore silent on the
     * bench by design -- not a bug to chase -- and kept at WARN (not ERROR) so it still surfaces
     * normally from any future caller that runs outside a quiesced transfer. */
    if (xQueueSend(g_ui_evt_q, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "ota ui: queue full");
}

/* §19.5 battery precondition.
 *
 * drv:M11 / #54: on this prototype there is no power hardware. CHRG is GPIO 39 -- input-only with
 * no internal pull on classic ESP32 -- so an unwired pin floats, a float reading low used to make
 * board_charger_present() answer "charging", and that answer bypassed this whole check: an OTA
 * (and its 1.25 MB slot erase) could proceed on a nearly empty pack. board_charger_present() now
 * returns false unless CFG_CHRG_WIRED == 1 (hal/board.h's documented contract), and the battery
 * ADC tap is unpopulated on the same board, so on such a build NEITHER input is trustworthy.
 *
 * Rather than let a float decide, the check is SKIPPED when the power hardware is absent and the
 * skip is recorded once per boot as E_OTA_BATT_UNWIRED. That keeps the bench OTA path working
 * (the only path by which this device is flashed) and makes the deviation auditable, which is
 * strictly better than the status quo where the same outcome was produced invisibly by a floating
 * pin. With CFG_CHRG_WIRED == 1 both inputs are real and §19.5 is enforced as written. */
static int ota_batt_ok(void)
{
#if !CFG_CHRG_WIRED
    static bool s_batt_skip_logged;     /* E_OTA_BATT_UNWIRED is logged once per boot (function-local so
                                         * the CFG_CHRG_WIRED == 1 arm has no unused variable) */
    if (!s_batt_skip_logged) { s_batt_skip_logged = true; (void)errlog_add(E_OTA_BATT_UNWIRED, 0); }
    return 0;
#else
    bool chg = false;
    if (board_charger_present(&chg) == 0 && chg) return 0;
    uint16_t mv = 0;
    if (board_battery_read_mv(&mv) == 0 && mv >= BATT_OTA_MIN_MV) return 0;
    return E_OTA_PRECOND;
#endif
}

static void ota_reset(void)
{
    s_state = OTA_IDLE;
    s_handle = 0;
    s_target = NULL;
    s_size = 0;
    s_recv = 0;
    s_hwid_ok = false;
    s_reboot_at_us = INT64_MAX;             /* disarm: no reboot is due in IDLE (reboot-arm race) */
    sys_flags_clear(SYS_OTA_PENDING);
}

/* Abort path (write/hwid/incomplete failures, before esp_ota_end): drop the slot, free the SHA
 * context, reset to IDLE. Returns `code` for the caller to propagate. mbedtls_sha256_free is
 * idempotent, so this is safe even if the context was already finished. */
static int ota_fail(int code)
{
    ota_ui_post(EV_OTA_ABORTED, 0);
    if (s_handle != 0) (void)esp_ota_abort(s_handle);
    mbedtls_sha256_free(&s_sha);
    ota_reset();
    return code;
}

/* §19.3 target check, run once the first 4 KB are written: the target's project_name
 * (esp_app_desc_t @0x20) must match the running image's, and its hwid (@0x120) must equal this
 * image's hwid. 0 on match; E_OTA_HWID on mismatch; E_OTA_WRITE on a flash read error. */
static int ota_verify_hwid(void)
{
    LT_ASSERT_RET(s_target != NULL, OTA_ASSERT_CODE, E_OTA_WRITE);
    /* §19.3: read OUR OWN hwid back out of OUR OWN running slot at 0x120 before trusting the same
     * offset in the target. If the linker ever stops placing it there, this fails on the first
     * push with E_OTA_WRITE and an explicit log line, instead of silently comparing the target's
     * padding bytes against our hwid and rejecting every legitimate image as E_OTA_HWID. */
    const esp_partition_t *self = esp_ota_get_running_partition();
    char selfhwid[OTA_HWID_LEN];
    if (self == NULL || esp_partition_read(self, IMG_HWID_OFF, selfhwid, sizeof selfhwid) != ESP_OK ||
        memcmp(selfhwid, hwid, sizeof selfhwid) != 0) {
        ESP_LOGE(TAG, "§19.3 layout check failed: hwid is not at image offset 0x%03x", IMG_HWID_OFF);
        return E_OTA_WRITE;
    }
    esp_app_desc_t desc;
    char thwid[OTA_HWID_LEN];
    if (esp_partition_read(s_target, IMG_DESC_OFF, &desc, sizeof desc) != ESP_OK) return E_OTA_WRITE;
    if (esp_partition_read(s_target, IMG_HWID_OFF, thwid, sizeof thwid) != ESP_OK) return E_OTA_WRITE;
    const esp_app_desc_t *run = esp_app_get_description();
    LT_ASSERT_RET(run != NULL, OTA_ASSERT_CODE, E_OTA_WRITE);
    if (strncmp(desc.project_name, run->project_name, sizeof desc.project_name) != 0) return E_OTA_HWID;
    if (memcmp(thwid, hwid, sizeof thwid) != 0) return E_OTA_HWID;
    return 0;
}

/* §19.5's "Logger session closed (LOGGER_CLOSE_SESSION acked)" (app:H1: nothing ever closed the
 * session for an OTA, so ~500 KB of esp_ota_write interleaved with the logger's 4 KB batches and
 * .sum rewrites on the same flash, each erase stalling BOTH cores' icache mid-lap, §17.8).
 * Bounded by OTA_CLOSE_TIMEOUT_MS: a logger that cannot ack in time means the precondition is NOT
 * met, so the push is refused rather than racing it. */
static int ota_close_session(void)
{
    log_request_t req = { .type = LOGGER_CLOSE_SESSION, .reason = SES_END_RESTART, .gps_us = 0 };
    int rc = logger_request_sync(&req, OTA_CLOSE_TIMEOUT_MS);
    if (rc != 0) {
        ESP_LOGE(TAG, "session close for OTA rc %d", rc);
        (void)errlog_add(E_LOG_CLOSE_TIMEOUT, SES_END_RESTART);
    }
    return rc;
}

/* R1: the RUNNING slot's own state, which is the authority on whether an image really awaits
 * validation. A set ota_pend flag with a non-pending running slot is a STALE flag (an NVS GC, a
 * power-cut between ota_end's persist and set_boot, or a completed rollback), and refusing on it
 * alone was the app:C1 dead end that made the device un-OTA-able until a USB flash. */
static bool ota_slot_pending_verify(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    return run != NULL && esp_ota_get_state_partition(run, &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

int ota_begin(const uint8_t *payload, size_t len)
{
    LT_ASSERT_RET(payload != NULL, OTA_ASSERT_CODE, E_OTA_PRECOND);
    if (len < OTA_BEGIN_LEN) return E_OTA_PRECOND;
    if (sys_flags_get() & (1u << SYS_OTA_PENDING)) return E_OTA_PRECOND;
    uint32_t size = rd_u32le(payload + OTA_OFF_SIZE);
    if (memcmp(payload + OTA_OFF_HWID, hwid, sizeof hwid) != 0) return E_OTA_HWID;   /* early reject */
    /* §19.5 (R2): the decision is pure and host-tested (app/sup_policy.h ota_precond_eval); this
     * function only gathers the inputs and performs the side effects it asks for. session_open
     * starts true -- assume a session is open until a close actually acks -- so the evaluator can
     * enforce the ordering rule that the close runs LAST, after every cheap refusal. */
    ota_pre_t pre = {
        .still_ms            = pipeline_still_for_ms(),
        .size                = size,
        .in_flight           = (s_state != OTA_IDLE),
        .session_open        = true,
        .batt_ok             = (ota_batt_ok() == 0),
        .flag_pending        = lt_ota_pending_get(),
        .slot_pending_verify = ota_slot_pending_verify(),
    };
    uint8_t act = ota_precond_eval(&pre);
    if (act == OTA_PRE_REFUSE) return E_OTA_PRECOND;
    if (act == OTA_PRE_CLOSE_SESSION) {
        if (ota_close_session() != 0) return E_OTA_PRECOND;
        pre.session_open = false;
        act = ota_precond_eval(&pre);          /* terminates: only OK / CLEAR_STALE can remain */
    }
    if (act == OTA_PRE_CLEAR_STALE) {
        lt_ota_pending_clear();
        (void)errlog_add(E_OTA_ROLLBACK, 1);   /* arg 1 == attributed at ota_begin, not at boot */
        ESP_LOGW(TAG, "stale ota_pending cleared (no pending-verify slot)");
    }
    /* Postcondition of the two evaluations above: nothing but a go-ahead reaches the flash. */
    LT_ASSERT_RET(act == OTA_PRE_OK || act == OTA_PRE_CLEAR_STALE, OTA_ASSERT_CODE, E_OTA_PRECOND);
    s_target = esp_ota_get_next_update_partition(NULL);
    LT_ASSERT_RET(s_target != NULL, OTA_ASSERT_CODE, E_OTA_PRECOND);
    if (esp_ota_begin(s_target, size, &s_handle) != ESP_OK) { s_handle = 0; s_target = NULL; return E_OTA_WRITE; }
    mbedtls_sha256_init(&s_sha);
    (void)mbedtls_sha256_starts(&s_sha, 0);
    memcpy(s_sha_want, payload + OTA_OFF_SHA, OTA_SHA_LEN);
    s_size = size;
    s_recv = 0;
    s_hwid_ok = false;
    s_last_data_ms = (uint32_t)(esp_timer_get_time() / 1000);   /* §19.6: arm the receive timeout */
    s_state = OTA_RECV;
    sys_flags_set(SYS_OTA_PENDING);
    s_ui_pct_step = 0;
    ota_ui_post(OTA_PHASE_RECEIVING, 0);
    ESP_LOGI(TAG, "begin %u B -> %s", (unsigned)size, s_target->label);
    return 0;
}

int ota_data(const uint8_t *payload, size_t len)
{
    LT_ASSERT_RET(payload != NULL, OTA_ASSERT_CODE, E_OTA_WRITE);
    if (s_state != OTA_RECV) return E_OTA_PRECOND;
    LT_ASSERT_RET(s_handle != 0, OTA_ASSERT_CODE, E_OTA_WRITE);     /* invariant while receiving */
    if (len < 4) return E_OTA_WRITE;
    uint32_t off = rd_u32le(payload);
    const uint8_t *data = payload + 4;
    size_t n = len - 4;
    if (n == 0) return E_OTA_WRITE;
    if (off != s_recv) return E_OTA_WRITE;                          /* sequential writes only */
    if ((uint64_t)s_recv + n > s_size) return E_OTA_WRITE;          /* never exceed the declared size */
    if (esp_ota_write(s_handle, data, n) != ESP_OK) return ota_fail(E_OTA_WRITE);
    (void)mbedtls_sha256_update(&s_sha, data, n);
    s_recv += (uint32_t)n;
    s_last_data_ms = (uint32_t)(esp_timer_get_time() / 1000);   /* §19.6: the peer is alive */
    LT_ASSERT_RET(s_recv <= s_size, OTA_ASSERT_CODE, E_OTA_WRITE);  /* bound maintained */
    uint8_t pct  = (uint8_t)((uint64_t)s_recv * 100u / s_size);   /* s_size > 0 while OTA_RECV */
    uint8_t step = (uint8_t)(pct / 5u);
    if (step != s_ui_pct_step) { s_ui_pct_step = step; ota_ui_post(OTA_PHASE_RECEIVING, pct); }
    if (!s_hwid_ok && s_recv >= OTA_HWID_CHECK_AT) {
        int hrc = ota_verify_hwid();
        if (hrc != 0) return ota_fail(hrc);
        s_hwid_ok = true;
    }
    return 0;
}

int ota_end(void)
{
    if (s_state != OTA_RECV) return E_OTA_PRECOND;
    LT_ASSERT_RET(s_handle != 0, OTA_ASSERT_CODE, E_OTA_WRITE);
    LT_ASSERT_RET(s_target != NULL, OTA_ASSERT_CODE, E_OTA_WRITE);
    /* §19.6: ota_end runs SHA finish, esp_ota_end (ECDSA over ~1.2 MB) and NVS/boot writes, far
     * longer than the idle stamp tolerates. Refresh the stamp, THEN publish OTA_VERIFY (release):
     * ota_poll only acts in OTA_RECV, so it can no longer abort a live ota_end. Every exit below
     * ends in IDLE (ota_fail / ota_reset) or REBOOT. */
    s_last_data_ms = (uint32_t)(esp_timer_get_time() / 1000);
    __atomic_store_n(&s_state, OTA_VERIFY, __ATOMIC_RELEASE);
    if (s_recv != s_size) return ota_fail(E_OTA_WRITE);            /* incomplete transfer */
    if (!s_hwid_ok) return ota_fail(E_OTA_HWID);                   /* image never reached the 4 KB check */
    uint8_t got[OTA_SHA_LEN];
    (void)mbedtls_sha256_finish(&s_sha, got);
    mbedtls_sha256_free(&s_sha);
    if (memcmp(got, s_sha_want, OTA_SHA_LEN) != 0) return ota_fail(E_OTA_WRITE);
    if (ota_batt_ok() != 0) return ota_fail(E_OTA_PRECOND);   /* §19.5 re-check */
    ota_ui_post(OTA_PHASE_VERIFYING, 100);
    esp_err_t e = esp_ota_end(s_handle);                          /* ECDSA signature verify */
    s_handle = 0;                                                 /* esp_ota_end frees the handle regardless */
    if (e != ESP_OK) {
        ota_ui_post(EV_OTA_ABORTED, 0); ota_reset(); ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(e)); return E_OTA_SIG;
    }
    /* §19.4 (H3): persist the pending-verify flag BEFORE arming the boot slot, and treat a persist
     * failure as fatal for the update. esp_ota_end already froze the slot; simply not calling
     * set_boot leaves the running image in place, so aborting here is safe. If the flag cannot be
     * written we must NOT activate the new image -- it would otherwise boot unvalidated with no
     * record that an OTA was in flight. The running partition's PENDING_VERIFY state is the
     * supervisor's real authority (it validates/rolls back on that state unconditionally), so a
     * flag lost to a power-cut in the tiny window below still resolves correctly. The OTHER
     * direction of that window is NOT free, and is accepted: a cut BETWEEN lt_ota_pending_set() and
     * esp_ota_set_boot_partition() leaves flag = 1 with the old, non-pending image running, which
     * ota_boot_decide attributes as a bootloader rollback -- one spurious E_OTA_ROLLBACK entry and
     * one LT_CTR_OTA_ROLLBACK bump for an update that was never activated. R1's stale-flag clear in
     * ota_begin means this no longer blocks the next push, which is what actually mattered (app:C1,
     * false-claim 13). */
    if (lt_ota_pending_set() != 0) {
        ota_ui_post(EV_OTA_ABORTED, 0); ESP_LOGE(TAG, "ota_pending persist failed"); ota_reset(); return E_OTA_WRITE;
    }
    if (esp_ota_set_boot_partition(s_target) != ESP_OK) {
        ota_ui_post(EV_OTA_ABORTED, 0); lt_ota_pending_clear(); ota_reset(); return E_OTA_WRITE;
    }
    /* Arm the deadline FIRST, then publish OTA_REBOOT with a release store: the supervisor's
     * ota_reboot_due() pairs an acquire load of s_state with this release, so it can never observe
     * OTA_REBOOT while s_reboot_at_us still holds its INT64_MAX init (which would reboot instantly,
     * truncating the settle delay and losing the OTA-END ack). */
    s_reboot_at_us = esp_timer_get_time() + (int64_t)OTA_REBOOT_DELAY_S * 1000000;
    __atomic_store_n(&s_state, OTA_REBOOT, __ATOMIC_RELEASE);
    ESP_LOGW(TAG, "image applied; supervisor reboots in %d s", OTA_REBOOT_DELAY_S);
    ota_ui_post(OTA_PHASE_REBOOTING, 100);
    return 0;
}

int ota_abort(void)
{
    if (s_state != OTA_RECV) return 0;      /* IDLE: nothing in flight; REBOOT: already applied */
    ota_ui_post(EV_OTA_ABORTED, 0);
    if (s_handle != 0) (void)esp_ota_abort(s_handle);
    mbedtls_sha256_free(&s_sha);
    ota_reset();                            /* running image untouched (§19.6) */
    return 0;
}

bool ota_in_progress(void) { return s_state != OTA_IDLE; }

bool ota_reboot_due(void)
{
    if (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) != OTA_REBOOT) return false;
    return esp_timer_get_time() >= s_reboot_at_us;   /* acquire above pairs with ota_end's release */
}

/* §19.6, called once per supervisor tick (app:M8: nothing ever timed out OTA_RECV, so a peer that
 * vanished mid-push left s_state == OTA_RECV and SYS_OTA_PENDING set until a reboot, and every
 * later ota_begin returned E_OTA_PRECOND). Covers BOTH transports: the framed CMD_OTA_* path had
 * no timeout at all, and `ota recv`'s own chunk-read timeout only covers the window its task is
 * blocked in -- a host that stops mid-stream after a successful chunk leaves nobody watching.
 *
 * Cross-task safety: this runs on the supervisor while the console/cmd task owns the state
 * machine. It can only act after OTA_RECV_TIMEOUT_S with no OTA_DATA, and a producing task
 * anywhere inside ota_data() refreshes the stamp -- so the only way both could be inside the
 * machine at once is (a) a task blocked for > 30 s in a single esp_ota_write (a 4 KB program is
 * ~10-40 ms), or (b) the instruction-wide window in which a poll has loaded OTA_RECV and a stale
 * stamp just before ota_end's refresh-then-OTA_VERIFY transition. ota_end itself is safe from
 * this poll: OTA_VERIFY is not OTA_RECV. §19.6's "abort on disconnect" is satisfied by THIS
 * timeout and deliberately not by the link DETECT edge: a detect flap during a healthy push would
 * abort a good transfer, and the console `ota recv` path does not use DETECT at all (see the
 * §19.6 amendment, Task 10). */
void ota_poll(void)
{
    if (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) != OTA_RECV) return;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (!ota_recv_stale(now_ms, s_last_data_ms, (uint32_t)OTA_RECV_TIMEOUT_S)) return;
    ESP_LOGE(TAG, "recv timeout after %d s at %u/%u B", OTA_RECV_TIMEOUT_S,
             (unsigned)s_recv, (unsigned)s_size);
    (void)errlog_add(E_CONN_XFER_ABORT, s_recv);
    (void)ota_fail(E_OTA_WRITE);          /* esp_ota_abort + SHA free + reset to IDLE (§19.6) */
}
