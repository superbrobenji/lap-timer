/* ota.c -- WEAK OTA receive-side stub (Plan 5 sub-project A, session 5.1 foundation).
 *
 * Session 5.4 replaces this with the real esp_ota state machine (esp_ota_begin/write/end,
 * signature + hwid + SHA-256 + §19.5 precondition checks, set_boot_partition + ota_pending).
 * Until then the CMD_OTA_* ops link and report E_OTA_PRECOND ("not built"). The weak attribute
 * lets 5.4's strong definitions override these regardless of link order. */
#include "app/ota.h"
#include "app/lt_err.h"

__attribute__((weak)) int  ota_begin(const uint8_t *payload, size_t len) { (void)payload; (void)len; return E_OTA_PRECOND; }
__attribute__((weak)) int  ota_data(const uint8_t *payload, size_t len)  { (void)payload; (void)len; return E_OTA_PRECOND; }
__attribute__((weak)) int  ota_end(void)                                 { return E_OTA_PRECOND; }
__attribute__((weak)) int  ota_abort(void)                               { return 0; }
__attribute__((weak)) bool ota_in_progress(void)                         { return false; }
