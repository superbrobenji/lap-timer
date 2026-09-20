/* linkhost.c -- Plan 5.5 Task 2 scaffold. Only linkhost_status_decode is implemented for real
 * here (pure, host-testable, no UART -- Task 2 Step 3). The UART1 request/response transport,
 * length-aware demux and stream ring (Task 3) and cmd-OTA flash (Task 6) land as later sessions;
 * every other entry point below is a compiling not-implemented stub until then.
 */
#include "linkhost.h"

#include <assert.h>
#include <string.h>

#include "esp_log.h"

#include "build_config.h"

static const char *TAG = "linkhost";

esp_err_t linkhost_init(void)
{
    /* Task 3 installs the real UART1 driver on DC_LINK_TX/DC_LINK_RX at DC_LINK_BAUD; the
     * scaffold just confirms the build_config.h plumbing reaches this component. */
    ESP_LOGI(TAG, "linkhost_init: UART%d tx=%d rx=%d baud=%d (Task 3 not yet landed)",
             DC_LINK_UART, DC_LINK_TX, DC_LINK_RX, DC_LINK_BAUD);
    return ESP_OK;
}

bool linkhost_status_decode(const uint8_t rec[LT_STATUS_LEN], lt_status_t *out)
{
    assert(rec != NULL);
    assert(out != NULL);
    if (!rec || !out) return false;

    out->proto    = rec[LT_ST_OFF_PROTO];
    out->state    = rec[LT_ST_OFF_STATE];
    out->flags    = (uint16_t)(rec[LT_ST_OFF_FLAGS] | ((uint16_t)rec[LT_ST_OFF_FLAGS + 1] << 8));
    out->batt_pct = rec[LT_ST_OFF_BATT_PCT];
    out->batt_mv  = (uint16_t)(rec[LT_ST_OFF_BATT_MV] | ((uint16_t)rec[LT_ST_OFF_BATT_MV + 1] << 8));
    out->free_kb  = (uint32_t)rec[LT_ST_OFF_FREE_KB]
                  | ((uint32_t)rec[LT_ST_OFF_FREE_KB + 1] << 8)
                  | ((uint32_t)rec[LT_ST_OFF_FREE_KB + 2] << 16)
                  | ((uint32_t)rec[LT_ST_OFF_FREE_KB + 3] << 24);
    out->sessions = (uint16_t)(rec[LT_ST_OFF_SESS] | ((uint16_t)rec[LT_ST_OFF_SESS + 1] << 8));
    memcpy(out->fw, &rec[LT_ST_OFF_FW], 7);
    out->fw[7] = '\0';
    return true;
}

int linkhost_status(lt_status_t *out)
{
    (void)out;
    return LINKHOST_E_NOTCONN;   /* Task 3 wires this to a real `status` UART1 exchange */
}

int linkhost_cmd(const char *cmd, linkhost_frame_t *out)
{
    (void)cmd;
    (void)out;
    return LINKHOST_E_NOTCONN;
}

int linkhost_stream_pop(lt_stream_rec_t *out)
{
    (void)out;
    return LINKHOST_E_NOTCONN;
}

bool linkhost_peer_present(void)
{
    return false;
}

int linkhost_flash(const char *ver, const char *hwid, uint32_t size,
                    const uint8_t sha256[32], flash_progress_cb cb, void *ctx)
{
    (void)ver;
    (void)hwid;
    (void)size;
    (void)sha256;
    (void)cb;
    (void)ctx;
    return LINKHOST_E_NOTCONN;
}

int linkhost_parse_frame(const uint8_t *bytes, size_t n, linkhost_frame_t *out)
{
    (void)bytes;
    (void)n;
    (void)out;
    return LINKHOST_E_PROTO;
}

size_t linkhost_feed(const uint8_t *bytes, size_t n)
{
    (void)bytes;
    return n;   /* Task 3's demux consumes real bytes; the stub reports all bytes consumed */
}
