#ifndef CORE_CFG_BLOB_H
#define CORE_CFG_BLOB_H
#include "core/cfg.h"
#include <stddef.h>
#include <stdint.h>

/* On-flash cfg blob = core/blob.h framing of cfg_t with cfg_t.version as the frame's version
 * byte: [version][cfg_t bytes 1..][crc16 LE] == sizeof(cfg_t) + 2 bytes -- today's bytes,
 * unchanged (debt sweep A final review, C1 / Ruling F-1). This header is the pure, host-tested
 * extraction of the framing + version/migrate decision that used to live inline in
 * lt_nvs.c's lt_cfg_load/lt_cfg_save; lt_nvs.c now does only the NVS I/O around it. */
#define CFG_BLOB_LEN (sizeof(cfg_t) + 2u)

size_t cfg_blob_wrap(const cfg_t *c, uint8_t *out, size_t cap);   /* CFG_BLOB_LEN written, or 0 (cap too small) */

/* >= 0: loaded ok -- the return value is the number of corrections cfg_validate() made. -1:
 * size or CRC mismatch. -3: a stored version this firmware does not accept. `*c` is left
 * UNTOUCHED on -1/-3 -- a caller's own cfg_t (defaults/profile already applied before the load)
 * survives a corrupt or foreign blob unchanged. A stored version != CFG_VERSION is accepted only
 * when cfg_migrate_supported() says so: unwrap at that version, cfg_migrate() it forward to
 * CFG_VERSION, then cfg_validate(). */
int cfg_blob_unwrap(const uint8_t *in, size_t n, cfg_t *c);

#endif
