/* sup_errlog.c -- error-logging glue for the supervisor (spec §17.9).
 *
 * Installs the core assertion hook so a failing CORE_ASSERT_* in components/core (which never
 * aborts on target) is recorded into the NVS error ring (§15.2/§17.7). Host tests install their
 * own recording hook; the default hook is silent.
 */
#include "app/lt_nvs.h"

#include "core/core.h"
#include "esp_log.h"

static const char *TAG = "assert";

static void app_assert_hook(uint16_t code, const char *file, int line)
{
    /* This is the installed core_assert_hook_t: it MUST NOT call an LT_ASSERT_ or CORE_ASSERT_
     * macro itself (a failing assertion in here would recurse back into this same hook via
     * core_assert_fail). */
    /* It also must accept every (code, file, line) a failing assertion anywhere in the codebase
     * can produce -- there is no invalid input to reject. */
    ESP_LOGE(TAG, "core assert 0x%04x at %s:%d", code, file ? file : "?", line);
    (void)errlog_add(code, (uint32_t)line);
}

void sup_install_assert_hook(void)
{
    core_set_assert_hook(app_assert_hook);
}
