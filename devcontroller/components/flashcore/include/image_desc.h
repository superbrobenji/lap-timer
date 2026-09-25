/* devcontroller/components/flashcore/include/image_desc.h -- a PURE parser for the lap-timer app
 * image header (Plan 5.5 sub-project B, Task 6; moved into flashcore in Plan 5.6 Task 7 so both
 * POST /api/flash and the console's `flash stage` share it).
 *
 * `ota recv <size> <sha> <ver> <hwid>` needs the version and hardware id of the image being
 * pushed. Both sit at fixed offsets in every ESP-IDF app image (spec 19.3), so B reads them out of
 * the staged bytes instead of asking the operator. Same logic as tools/ota_push.py
 * extract_ver_hwid. IDF-free on purpose (no esp_app_desc.h): parse by offset so
 * devcontroller/test/test_image_desc.c links it directly on the host.
 */
#ifndef IMAGE_DESC_H
#define IMAGE_DESC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes of image needed to read both fields: through the end of the 24 B hwid custom descriptor
 * at 0x120. */
#define IMG_DESC_MIN_LEN 0x138
#define IMG_VER_LEN      32    /* esp_app_desc_t.version field width (and this buffer's size) */
#define IMG_HWID_LEN     24    /* spec 19.3 hwid custom descriptor width */

/* Parses the first `n` bytes of an app image. Layout (components/app/ota/ota.c, spec 19.3):
 * esp_app_desc_t at 0x20 with magic 0xABCD5432 (u32 LE), version[32] at 0x30; the 24 B hwid
 * custom descriptor at 0x120.
 *
 * On success `ver` and `hwid` are NUL-terminated (a version of 31+ characters is truncated to fit
 * -- harmless: the device's OTA_BEGIN ver field is only 16 B wide and it ignores the value).
 * Returns 0, or:
 *   -1  short buffer or bad esp_app_desc magic (not an ESP32 app image)
 *   -2  bad version   (empty, or not a single printable console token)
 *   -3  bad hwid      (empty, or not a single printable console token)
 * Both fields ride an `ota recv` console line on the lap-timer, so whitespace, control bytes,
 * quotes and backslashes are rejected rather than escaped. */
int img_desc_parse(const uint8_t *hdr, size_t n, char ver[IMG_VER_LEN], char hwid[IMG_HWID_LEN + 1]);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_DESC_H */
