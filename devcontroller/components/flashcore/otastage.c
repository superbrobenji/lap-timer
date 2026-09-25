/* otastage.c -- see include/otastage.h. The exact erase/write/read-back/parse sequence that used
 * to live in webapi.c as s_stage_blk/stage_ctx_t/stage_commit/stage_sink (Plan 5.5 Task 6), moved
 * behind a stable API in Plan 5.6 Task 7 so both POST /api/flash and a later console `flash
 * stage` drive the same code. Single-flight, no heap: one static 4 KB staging block.
 *
 * The size-bound arithmetic (write-overflow and finish-time EXACT/BOUNDED validity) is pure and
 * lives in stage_bounds.c/.h so it can be host-tested directly -- this file only owns the
 * IDF-bound parts (esp_partition, mbedtls) around it. */
#include "otastage.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "esp_partition.h"
#include "mbedtls/sha256.h"

#include "stage_bounds.h"

#define STAGE_BLOCK 4096u   /* flash sector: the erase+write granularity */

typedef struct {
    const esp_partition_t  *part;
    uint32_t                bound;     /* the size/max passed to otastage_begin*: see `mode` */
    stage_bounds_mode_t     mode;      /* EXACT (otastage_begin) or BOUNDED (otastage_begin_bounded) */
    uint32_t                written;   /* bytes committed to flash; always a multiple of STAGE_BLOCK */
    size_t                  blk_len;   /* bytes pending in s_blk */
    bool                    active;
    mbedtls_sha256_context  sha;
    int                     last_image_rc;   /* img_desc_parse's rc, valid after OTASTAGE_E_IMAGE */
} stage_state_t;

static stage_state_t s_stage;
static uint8_t       s_blk[STAGE_BLOCK];

static int stage_begin_common(uint32_t bound, stage_bounds_mode_t mode)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "ota_stage");
    if (!part) return OTASTAGE_E_WRITE;
    assert((part->size % STAGE_BLOCK) == 0u);   /* stage_commit erases whole sectors */

    if (bound == 0u || bound > part->size) return OTASTAGE_E_SIZE;

    memset(&s_stage, 0, sizeof s_stage);
    s_stage.part  = part;
    s_stage.bound = bound;
    s_stage.mode  = mode;

    mbedtls_sha256_init(&s_stage.sha);
    if (mbedtls_sha256_starts(&s_stage.sha, 0) != 0) {
        mbedtls_sha256_free(&s_stage.sha);
        return OTASTAGE_E_WRITE;
    }
    s_stage.active = true;
    return OTASTAGE_OK;
}

int otastage_begin(uint32_t size)
{
    return stage_begin_common(size, STAGE_BOUNDS_EXACT);
}

int otastage_begin_bounded(uint32_t max)
{
    return stage_begin_common(max, STAGE_BOUNDS_BOUNDED);
}

/* Erases the sector at s_stage.written and writes the first `len` bytes of s_blk into it. Lazy
 * per-sector erase: erasing the whole 1.25 MB partition up front would stall the upload for
 * seconds and TCP would time out. */
static int stage_commit(size_t len)
{
    assert(s_stage.active && s_stage.part != NULL);
    assert(len > 0u && len <= (size_t)STAGE_BLOCK);
    assert((s_stage.written % STAGE_BLOCK) == 0u);   /* erase_range needs alignment */

    if (esp_partition_erase_range(s_stage.part, s_stage.written, STAGE_BLOCK) != ESP_OK) return -1;
    if (esp_partition_write(s_stage.part, s_stage.written, s_blk, len) != ESP_OK) return -1;
    s_stage.written += (uint32_t)len;
    return 0;
}

int otastage_write(const uint8_t *p, size_t n)
{
    assert(p != NULL || n == 0u);
    assert(s_stage.active);

    if (n == 0u) return OTASTAGE_OK;
    if (stage_bounds_write_overflows(s_stage.written, s_stage.blk_len, n, s_stage.bound))
        return OTASTAGE_E_SIZE;
    if (mbedtls_sha256_update(&s_stage.sha, p, n) != 0) return OTASTAGE_E_WRITE;

    size_t off = 0;
    while (off < n) {                                             /* bounded by n */
        size_t room = (size_t)STAGE_BLOCK - s_stage.blk_len;
        size_t take = ((n - off) < room) ? (n - off) : room;
        memcpy(s_blk + s_stage.blk_len, p + off, take);
        s_stage.blk_len += take;
        off += take;
        if (s_stage.blk_len == (size_t)STAGE_BLOCK) {
            if (stage_commit((size_t)STAGE_BLOCK) != 0) return OTASTAGE_E_WRITE;
            s_stage.blk_len = 0;
        }
    }
    return OTASTAGE_OK;
}

int otastage_finish(uint8_t sha[32], char ver[IMG_VER_LEN], char hwid[IMG_HWID_LEN + 1],
                    uint32_t *size)
{
    assert(sha != NULL && ver != NULL && hwid != NULL && size != NULL);
    assert(s_stage.active);

    /* Flush BEFORE any size/image check (see otastage.h): even a stage about to be rejected still
     * gets its trailing partial sector committed. Harmless -- the bytes are meaningless once
     * rejected, and the next begin/write erases over them regardless. */
    if (s_stage.blk_len > 0u) {
        if (stage_commit(s_stage.blk_len) != 0) {
            otastage_abort();
            return OTASTAGE_E_WRITE;
        }
        s_stage.blk_len = 0;
    }

    if (!stage_bounds_finish_ok(s_stage.mode, s_stage.written, s_stage.bound)) {
        /* otastage_abort frees the still-live SHA context (fix round 2: this path used to set
         * active=false and return without freeing it -- with CONFIG_MBEDTLS_HARDWARE_SHA=y that
         * leaks the hardware SHA engine lock until reboot, after which every SHA-256 in the
         * firmware silently falls back to software). */
        otastage_abort();
        return OTASTAGE_E_SIZE;
    }

    uint8_t digest[32];
    int src = mbedtls_sha256_finish(&s_stage.sha, digest);
    mbedtls_sha256_free(&s_stage.sha);       /* releases the SHA engine on every path from here on */
    if (src != 0) {
        s_stage.active = false;
        return OTASTAGE_E_WRITE;
    }

    if (s_stage.written < (uint32_t)IMG_DESC_MIN_LEN) {
        s_stage.last_image_rc = -1;          /* same reason img_desc_parse uses for "too short" */
        s_stage.active = false;
        return OTASTAGE_E_IMAGE;
    }

    /* Read the descriptor back OUT of the partition, so ver/hwid describe the bytes that will
     * actually be pushed rather than the bytes we thought we wrote. */
    uint8_t hdr[IMG_DESC_MIN_LEN];
    if (esp_partition_read(s_stage.part, 0, hdr, sizeof hdr) != ESP_OK) {
        s_stage.active = false;
        return OTASTAGE_E_WRITE;
    }
    int prc = img_desc_parse(hdr, sizeof hdr, ver, hwid);
    s_stage.last_image_rc = prc;
    if (prc != 0) {
        s_stage.active = false;
        return OTASTAGE_E_IMAGE;
    }

    memcpy(sha, digest, 32u);
    *size = s_stage.written;
    s_stage.active = false;
    return OTASTAGE_OK;
}

int otastage_last_image_rc(void)
{
    return s_stage.last_image_rc;
}

void otastage_abort(void)
{
    if (s_stage.active) mbedtls_sha256_free(&s_stage.sha);
    memset(&s_stage, 0, sizeof s_stage);
}
