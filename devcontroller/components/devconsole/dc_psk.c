/* dc_psk.c -- R16 (dk:H1): the dev-kit's WPA2 passphrase. NVS namespace "dc", key "psk": a
 * DC_PSK_LEN-character value generated ONCE on the first boot and reused forever after, so it is
 * not a function of anything an attacker can read off the air (the old PSK was "ltdev-" + the
 * eFuse MAC, and the SoftAP BSSID in every beacon is a published ESP-IDF function of that same
 * base MAC). A stored value that does not pass dc_psk_valid (absent, truncated, or the old
 * MAC-derived form) is replaced. Entropy: esp_random()'s HWRNG is only fully seeded once RF is
 * running (esp_random.h), and the first-boot generation happens BEFORE esp_wifi_start(), so that
 * path brackets the fill with bootloader_random_enable()/_disable() (bootloader_random.h: the SAR
 * ADC entropy source, which MUST be disabled again before RF/ADC init -- it is, inside this
 * function, before wifi_ap_start() goes on to esp_wifi_init()). dc_psk_reset() runs with the AP
 * already up and uses esp_fill_random() directly.
 *
 * Module-static cache (mandatory deviation from the task brief, F3 Session 1 controller ruling):
 * dc_psk_get returns a cached value immediately once one exists for this boot, instead of reaching
 * NVS/RNG again on every call. Without this, `dc status` (which calls dc_psk_get on every
 * invocation) would, on a device whose boot-time NVS store failed, re-enter psk_fill(...,
 * rf_running=false) -- and so bootloader_random_enable() -- with RF already up, which
 * bootloader_random.h explicitly forbids (the SAR ADC entropy source must be disabled before
 * RF/ADC init, not re-enabled after). The cache makes that path unreachable after boot, and also
 * guarantees `dc status` always reports the same passphrase the live AP is actually using. */
#include "dc_psk.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"

static const char *TAG = "dc_psk";

#define DC_PSK_NS  "dc"
#define DC_PSK_KEY "psk"

/* Populated by the first successful dc_psk_get or dc_psk_reset this boot; every later dc_psk_get
 * call returns this instead of touching NVS/RNG again (see the file header). s_psk[0] == '\0'
 * means "nothing cached yet". */
static char s_psk[DC_PSK_LEN + 1];
static bool s_persisted;

static void psk_fill(char *out, size_t out_cap, bool rf_running)
{
    uint8_t rnd[DC_PSK_LEN];
    if (!rf_running) bootloader_random_enable();
    esp_fill_random(rnd, sizeof rnd);
    if (!rf_running) bootloader_random_disable();
    dc_psk_format(rnd, sizeof rnd, out, out_cap);
}

/* Persists `psk` under dc/psk. 0 once the commit has landed, <0 on any NVS failure (the caller
 * then keeps using the value in RAM for this boot and says so). Same open/set/commit/close shape
 * the lap-timer's lt_nvs.c uses, except the handle is not cached: this runs twice in the life of a
 * device (first boot, `dc psk reset`), so holding a handle open forever buys nothing. */
static int psk_store(const char *psk)
{
    assert(psk != NULL);
    nvs_handle_t h;
    if (nvs_open(DC_PSK_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t e = nvs_set_str(h, DC_PSK_KEY, psk);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "psk store: %s", esp_err_to_name(e));
        return -1;
    }
    return 0;
}

/* Caches psk (already NUL-terminated by the caller) and its persisted flag, then copies it into
 * the caller's out buffer. out_cap >= DC_PSK_LEN + 1 is the caller's job to have checked. */
static void psk_cache_and_copy(const char *psk, bool persisted, char *out, size_t out_cap)
{
    assert(out_cap >= DC_PSK_LEN + 1u);
    memcpy(s_psk, psk, DC_PSK_LEN + 1u);
    s_persisted = persisted;
    snprintf(out, out_cap, "%s", psk);
}

int dc_psk_get(char *out, size_t out_cap)
{
    assert(out != NULL);
    if (out_cap < DC_PSK_LEN + 1u) return -1;
    if (s_psk[0] != '\0') {
        snprintf(out, out_cap, "%s", s_psk);
        return s_persisted ? 0 : 1;
    }

    char psk[DC_PSK_LEN + 1];
    nvs_handle_t h;
    if (nvs_open(DC_PSK_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof psk;
        esp_err_t e = nvs_get_str(h, DC_PSK_KEY, psk, &len);
        nvs_close(h);
        if (e == ESP_OK && dc_psk_valid(psk)) {
            psk_cache_and_copy(psk, true, out, out_cap);
            return 0;
        }
    }
    psk_fill(psk, sizeof psk, /*rf_running*/false);
    bool persisted = (psk_store(psk) == 0);
    psk_cache_and_copy(psk, persisted, out, out_cap);
    return persisted ? 0 : 1;       /* 1: usable this boot, not persisted */
}

int dc_psk_reset(char *out, size_t out_cap)
{
    assert(out != NULL);
    if (out_cap < DC_PSK_LEN + 1u) return -1;
    char psk[DC_PSK_LEN + 1];
    psk_fill(psk, sizeof psk, /*rf_running*/true);
    bool persisted = (psk_store(psk) == 0);
    psk_cache_and_copy(psk, persisted, out, out_cap);
    return persisted ? 0 : 1;
}
