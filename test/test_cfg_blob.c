/* test_cfg_blob.c -- host coverage for core/cfg_blob.h (debt sweep A, final review C1 / Ruling
 * F-1): the cfg blob decision (framing + version/migrate/validate) extracted out of
 * lt_nvs.c's lt_cfg_load/lt_cfg_save into a pure, host-testable helper. Every case here mirrors
 * a decision lt_cfg_load used to make inline.
 */
#include "unity.h"
#include "core/cfg.h"
#include "core/cfg_blob.h"
#include "core/ses.h"      /* ses_crc16 -- hand-building a corrupted/foreign-version frame */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* (1) cfg_defaults -> wrap -> unwrap == 0 and byte-identical. */
static void test_round_trip_is_byte_identical(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));

    cfg_t b; memset(&b, 0xAA, sizeof b);
    TEST_ASSERT_EQUAL_INT(0, cfg_blob_unwrap(buf, sizeof buf, &b));
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

/* (2) a hand-built version-0 frame (unsupported: cfg_migrate_supported(0) == false) -> -3,
 * c left untouched (pre-filled with a sentinel, compared byte for byte). */
static void test_unsupported_version_leaves_c_untouched(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));

    buf[0] = 0;                                       /* stored version 0: unsupported */
    uint16_t crc = ses_crc16(buf, sizeof(cfg_t));      /* version byte + payload, same as blob_wrap */
    buf[sizeof(cfg_t)]     = (uint8_t)(crc & 0xFFu);
    buf[sizeof(cfg_t) + 1] = (uint8_t)(crc >> 8);

    cfg_t sentinel; memset(&sentinel, 0x5A, sizeof sentinel);
    cfg_t c = sentinel;
    TEST_ASSERT_EQUAL_INT(-3, cfg_blob_unwrap(buf, sizeof buf, &c));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &c, sizeof c);
}

/* (3) flip a payload byte -> CRC mismatch -> -1, c untouched. */
static void test_crc_mismatch_leaves_c_untouched(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));
    buf[10] ^= 0x01u;                                  /* well inside the payload */

    cfg_t sentinel; memset(&sentinel, 0x5A, sizeof sentinel);
    cfg_t c = sentinel;
    TEST_ASSERT_EQUAL_INT(-1, cfg_blob_unwrap(buf, sizeof buf, &c));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &c, sizeof c);
}

/* (4) a v1 (== CFG_VERSION) frame whose drag.n_kmh byte is 200 -> unwrap >= 1 (cfg_validate
 * clamped it) and n_kmh == CFG_MAX_BENCHES. The byte is poked directly at its real on-flash
 * offset (offsetof) and the CRC recomputed, rather than going through cfg_defaults -- this is
 * exactly the shape of a corrupted-in-place stored blob. */
static void test_v1_frame_with_bad_n_kmh_is_clamped(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));

    size_t off = offsetof(cfg_t, drag.n_kmh);
    buf[off] = 200;                                    /* buf[k] == cfg_t byte k, for k >= 1 (wrap is a 1:1 copy) */
    uint16_t crc = ses_crc16(buf, sizeof(cfg_t));
    buf[sizeof(cfg_t)]     = (uint8_t)(crc & 0xFFu);
    buf[sizeof(cfg_t) + 1] = (uint8_t)(crc >> 8);

    cfg_t c; memset(&c, 0, sizeof c);
    int rc = cfg_blob_unwrap(buf, sizeof buf, &c);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_UINT8(CFG_MAX_BENCHES, c.drag.n_kmh);
}

/* (5) cfg_migrate_supported is exactly the versions cfg_migrate accepts today. */
static void test_cfg_migrate_supported_versions(void)
{
    TEST_ASSERT_TRUE(cfg_migrate_supported(1));
    TEST_ASSERT_FALSE(cfg_migrate_supported(0));
    TEST_ASSERT_FALSE(cfg_migrate_supported(2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_is_byte_identical);
    RUN_TEST(test_unsupported_version_leaves_c_untouched);
    RUN_TEST(test_crc_mismatch_leaves_c_untouched);
    RUN_TEST(test_v1_frame_with_bad_n_kmh_is_clamped);
    RUN_TEST(test_cfg_migrate_supported_versions);
    return UNITY_END();
}
