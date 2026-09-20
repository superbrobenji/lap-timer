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
#include "app/lt_nvs.h"
#include "app/lt_sup.h"

#include "build_config.h"          /* CFG_HWID */

#include "esp_app_desc.h"          /* esp_app_get_description, esp_app_desc_t */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"

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

typedef enum { OTA_IDLE = 0, OTA_RECV, OTA_REBOOT } ota_st_t;

static ota_st_t               s_state;
static esp_ota_handle_t       s_handle;
static const esp_partition_t *s_target;
static uint32_t               s_size;                /* declared image size (from OTA_BEGIN) */
static uint32_t               s_recv;                /* bytes written == next expected offset */
static uint8_t                s_sha_want[OTA_SHA_LEN];
static mbedtls_sha256_context s_sha;
static bool                   s_hwid_ok;             /* the §19.3 target check has passed */
static int64_t                s_reboot_at_us;

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* §19.5 battery precondition: OTA proceeds on the charger, or at >= BATT_OTA_MIN_MV (a stand-in
 * for >=50% until the power task provides a real batt_pct). 0 when allowed, E_OTA_PRECOND else. */
static int ota_batt_ok(void)
{
    bool chg = false;
    if (board_charger_present(&chg) == 0 && chg) return 0;
    uint16_t mv = 0;
    if (board_battery_read_mv(&mv) == 0 && mv >= BATT_OTA_MIN_MV) return 0;
    return E_OTA_PRECOND;
}

static void ota_reset(void)
{
    s_state = OTA_IDLE;
    s_handle = 0;
    s_target = NULL;
    s_size = 0;
    s_recv = 0;
    s_hwid_ok = false;
    sys_flags_clear(SYS_OTA_PENDING);
}

/* Abort path (write/hwid/incomplete failures, before esp_ota_end): drop the slot, free the SHA
 * context, reset to IDLE. Returns `code` for the caller to propagate. mbedtls_sha256_free is
 * idempotent, so this is safe even if the context was already finished. */
static int ota_fail(int code)
{
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

int ota_begin(const uint8_t *payload, size_t len)
{
    LT_ASSERT_RET(payload != NULL, OTA_ASSERT_CODE, E_OTA_PRECOND);
    if (s_state != OTA_IDLE) return E_OTA_PRECOND;                  /* an OTA is already in flight */
    if (len < OTA_BEGIN_LEN) return E_OTA_PRECOND;
    if (sys_flags_get() & (1u << SYS_OTA_PENDING)) return E_OTA_PRECOND;
    if (lt_ota_pending_get()) return E_OTA_PRECOND;                /* an image already awaits validation */
    uint32_t size = rd_u32le(payload + OTA_OFF_SIZE);
    if (size == 0 || size > OTA_MAX_IMG_BYTES) return E_OTA_PRECOND;              /* §19.5 slot bound */
    if (memcmp(payload + OTA_OFF_HWID, hwid, sizeof hwid) != 0) return E_OTA_HWID;/* early reject */
    if (ota_batt_ok() != 0) return E_OTA_PRECOND;
    s_target = esp_ota_get_next_update_partition(NULL);
    LT_ASSERT_RET(s_target != NULL, OTA_ASSERT_CODE, E_OTA_PRECOND);
    if (esp_ota_begin(s_target, size, &s_handle) != ESP_OK) { s_handle = 0; s_target = NULL; return E_OTA_WRITE; }
    mbedtls_sha256_init(&s_sha);
    (void)mbedtls_sha256_starts(&s_sha, 0);
    memcpy(s_sha_want, payload + OTA_OFF_SHA, OTA_SHA_LEN);
    s_size = size;
    s_recv = 0;
    s_hwid_ok = false;
    s_state = OTA_RECV;
    sys_flags_set(SYS_OTA_PENDING);
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
    LT_ASSERT_RET(s_recv <= s_size, OTA_ASSERT_CODE, E_OTA_WRITE);  /* bound maintained */
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
    if (s_recv != s_size) return ota_fail(E_OTA_WRITE);            /* incomplete transfer */
    if (!s_hwid_ok) return ota_fail(E_OTA_HWID);                   /* image never reached the 4 KB check */
    uint8_t got[OTA_SHA_LEN];
    (void)mbedtls_sha256_finish(&s_sha, got);
    mbedtls_sha256_free(&s_sha);
    if (memcmp(got, s_sha_want, OTA_SHA_LEN) != 0) { (void)esp_ota_abort(s_handle); ota_reset(); return E_OTA_WRITE; }
    if (ota_batt_ok() != 0) { (void)esp_ota_abort(s_handle); ota_reset(); return E_OTA_PRECOND; }  /* §19.5 re-check */
    esp_err_t e = esp_ota_end(s_handle);                          /* ECDSA signature verify */
    s_handle = 0;                                                 /* esp_ota_end frees the handle regardless */
    if (e != ESP_OK) { ota_reset(); ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(e)); return E_OTA_SIG; }
    if (esp_ota_set_boot_partition(s_target) != ESP_OK) { ota_reset(); return E_OTA_WRITE; }
    if (lt_ota_pending_set() != 0) ESP_LOGE(TAG, "ota_pending persist failed");
    s_state = OTA_REBOOT;
    s_reboot_at_us = esp_timer_get_time() + (int64_t)OTA_REBOOT_DELAY_S * 1000000;
    ESP_LOGW(TAG, "image applied; supervisor reboots in %d s", OTA_REBOOT_DELAY_S);
    return 0;
}

int ota_abort(void)
{
    if (s_state != OTA_RECV) return 0;      /* IDLE: nothing in flight; REBOOT: already applied */
    if (s_handle != 0) (void)esp_ota_abort(s_handle);
    mbedtls_sha256_free(&s_sha);
    ota_reset();                            /* running image untouched (§19.6) */
    return 0;
}

bool ota_in_progress(void) { return s_state != OTA_IDLE; }

bool ota_reboot_due(void)
{
    return s_state == OTA_REBOOT && esp_timer_get_time() >= s_reboot_at_us;
}
