/* app/ota.h -- OTA receive-side interface (spec §19.4), Plan 5 sub-project A.
 *
 * The peer (dev controller, or a serial/BLE client) pushes a signed image via the CMD_OTA_*
 * ops; these drive the esp_ota state machine. Each returns 0 on success or an E_OTA_* code (> 0)
 * to report back as an ERROR chunk. A weak stub links until session 5.4 provides the real
 * implementation (esp_ota_begin/write/end + signature/hwid/SHA/precondition checks). */
#ifndef APP_OTA_H
#define APP_OTA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

int  ota_begin(const uint8_t *payload, size_t len);   /* {size u32 | sha[32] | ver | hwid[24]} (§19.4) */
int  ota_data(const uint8_t *payload, size_t len);     /* {offset u32 | bytes[]} */
int  ota_end(void);                                    /* SHA + signature verify -> set_boot -> pending -> restart */
int  ota_abort(void);                                  /* esp_ota_abort; running image untouched */
bool ota_in_progress(void);                            /* true between a successful ota_begin and end/abort */

#endif /* APP_OTA_H */
